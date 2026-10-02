#include "pch.h"
#include "VertexAnimationDeformer.h"

#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Graphics/Model/VertexData.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

#include <exception>

namespace CoreEngine
{
    namespace
    {
        constexpr uint32_t kThreadGroupSize = 64; // VertexAnimationDeform.CS.hlsl の numthreads
    }

    VertexAnimationDeformer::VertexAnimationDeformer() = default;

    VertexAnimationDeformer::~VertexAnimationDeformer()
    {
        // 実行待ちのフレームがまだ読んでいるかもしれないので、フェンスを待ってから解放する
        for (auto& [owner, entry] : entries_) {
            (void)owner;
            ReleaseEntry(entry);
        }
    }

    void VertexAnimationDeformer::ReleaseEntry(Entry& entry)
    {
        if (!graphicsCore_) {
            return;
        }
        entry.rawSrv.Reset();
        if (entry.vertices) {
            graphicsCore_->DeferRelease(entry.vertices.Get());
            entry.vertices.Release();
        }
    }

    void VertexAnimationDeformer::Initialize(GraphicsCore* graphicsCore)
    {
        graphicsCore_ = graphicsCore;
    }

    bool VertexAnimationDeformer::EnsurePipeline()
    {
        if (pipelineTried_) {
            return pipelineReady_;
        }
        pipelineTried_ = true;

        ID3D12Device* device = graphicsCore_ ? graphicsCore_->GetDevice() : nullptr;
        if (!device) {
            return false;
        }

        Logger& log = Logger::GetInstance();
        try {
            shaderCompiler_ = std::make_unique<ShaderCompiler>();
            shaderCompiler_->Initialize();
            Microsoft::WRL::ComPtr<IDxcBlob> csBlob = shaderCompiler_->CompileShader(
                L"Engine/Assets/Shaders/RayTracing/VertexAnimationDeform.CS.hlsl", L"cs_6_0");
            if (!csBlob) {
                log.Warnf(LogCategory::Graphics, "VertexAnimationDeformer: CS のコンパイルに失敗（揺れは影に反映しない）");
                return false;
            }

            reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();
            reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
            reflectionData_ = reflectionBuilder_->BuildFromComputeShader(csBlob.Get(), "VertexAnimationDeform");

            // すべてルート引数で渡す（ディスクリプタを確保しない）。
            // 定数: gVertexAnim 8 + gDeform 36、ディスクリプタ: CBV / SRV x2 / UAV で計 52 DWORD（上限 64）
            RootSignatureConfig config;
            config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
            config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
            config.ConfigureResource("gVertexAnim", BindingStrategy::RootConstants);
            config.ConfigureResource("gDeform", BindingStrategy::RootConstants);
            config.ConfigureResource("gSourceVertices", BindingStrategy::RootDescriptor);
            config.ConfigureResource("gIndices", BindingStrategy::RootDescriptor);
            config.ConfigureResource("gOutputVertices", BindingStrategy::RootDescriptor);

            rootSignatureMg_ = std::make_unique<RootSignatureManager>();
            const auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
            if (!buildResult.success) {
                log.Warnf(LogCategory::Graphics, "VertexAnimationDeformer: ルートシグネチャの構築に失敗: {}",
                    buildResult.errorMessage);
                return false;
            }

            pso_ = ComputePipelineUtil::Create(
                device, rootSignatureMg_->GetRootSignature(), csBlob.Get(), "VertexAnimationDeform");
            if (!pso_) {
                return false;
            }

            reflectionData_->ValidateAllCBVSizes({
                { "gMaterial", sizeof(MaterialConstants) },
                { "gVertexAnim", sizeof(VertexAnimationParams) },
                { "gDeform", sizeof(DeformConstants) },
                });
            bindings_ = BindingTable::Resolve(*reflectionData_, VertexAnimationDeformBind::kDecls, "VertexAnimationDeform");
        }
        catch (const std::exception& e) {
            // 契約違反の内訳は BindingTable::Resolve が error ログへ出している
            log.Warnf(LogCategory::Graphics, "VertexAnimationDeformer: パイプラインを組めない: {}", e.what());
            return false;
        }

        pipelineReady_ = true;
        log.Infof(LogCategory::Graphics, "VertexAnimationDeformer: 揺れる植物のレイトレーシング用 CS を構築");
        return true;
    }

    VertexAnimationDeformer::Output VertexAnimationDeformer::Deform(ID3D12GraphicsCommandList* cmdList,
        const void* owner, const Model& model, const Matrix4x4& world, const VertexAnimationParams& params,
        uint64_t frame)
    {
        if (!cmdList || !owner || !EnsurePipeline()) {
            return {};
        }
        const ModelResource* resource = model.GetModelResource();
        if (!resource || !resource->IsLoaded() || !resource->GetVertexBuffer() || !resource->GetIndexBuffer()) {
            return {};
        }
        const uint32_t vertexCount = resource->GetVertexCount();
        if (vertexCount == 0 || resource->GetIndexCount() == 0) {
            return {};
        }

        // ===== 持ち主ごとの頂点バッファ（頂点数が変わったら張り直す） =====
        Entry& entry = entries_[owner];
        entry.lastUsedFrame = frame;
        if (!entry.vertices || entry.vertexCount != vertexCount) {
            ReleaseEntry(entry);
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = static_cast<UINT64>(vertexCount) * sizeof(VertexData);
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer = ResourceFactory::CreateTextureResource(
                graphicsCore_->GetDevice(), desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            if (!buffer) {
                entries_.erase(owner);
                return {};
            }
            buffer->SetName(L"VertexAnimationDeform Vertices");
            entry.vertices.Reset(buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            entry.vertexCount = vertexCount;

            // 水面の映り込みのヒットシェーディングが読む ByteAddressBuffer SRV（静止形の BLAS の頂点と同じ作り）
            if (DescriptorAllocator* descriptors = graphicsCore_->GetDescriptorAllocator()) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Buffer.FirstElement = 0;
                srvDesc.Buffer.NumElements = static_cast<UINT>(desc.Width / sizeof(uint32_t));
                srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                entry.rawSrv = UniqueDescriptor(*descriptors,
                    descriptors->CreateSRV(buffer.Get(), srvDesc, "VertexAnimationDeform VerticesRawSRV"));
            }
        }

        // ===== サブメッシュ（マテリアル）ごとに、そのインデックスが指す頂点を書く =====
        Barrier::Transition(cmdList, entry.vertices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetComputeRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(pso_.Get());

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.SetConstants(bindings_[VertexAnimationDeformBind::gVertexAnim], params);
        binder.Set(bindings_[VertexAnimationDeformBind::gSourceVertices], resource->GetVertexBuffer()->GetGPUVirtualAddress());
        binder.Set(bindings_[VertexAnimationDeformBind::gIndices], resource->GetIndexBuffer()->GetGPUVirtualAddress());
        binder.Set(bindings_[VertexAnimationDeformBind::gOutputVertices], entry.vertices.GpuAddress());

        DeformConstants constants{};
        constants.world = world;
        constants.worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(world));
        constants.vertexCount = vertexCount;
        for (const SubMeshData& subMesh : resource->GetSubMeshes()) {
            if (subMesh.indexCount == 0) {
                continue;
            }
            // 揺れないマテリアル（なし・魚）のサブメッシュも、元の頂点を書くために回す
            constants.indexStart = subMesh.startIndex;
            constants.indexCount = subMesh.indexCount;
            binder.Set(bindings_[VertexAnimationDeformBind::gMaterial], model.GetMaterialCBVAddress(subMesh.materialIndex));
            binder.SetConstants(bindings_[VertexAnimationDeformBind::gDeform], constants);
            binder.ValidateBeforeDraw(bindings_);
            cmdList->Dispatch((subMesh.indexCount + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);
        }

        // BLAS の入力とレイトレーシングのシェーダーからの読み取りは NON_PIXEL_SHADER_RESOURCE
        // （UAV の書き込みの完了もこの遷移が保証する）
        Barrier::Transition(cmdList, entry.vertices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Output output;
        output.vertices = entry.vertices.Get();
        output.vertexBufferIndex = entry.rawSrv.IsValid() ? entry.rawSrv.Index() : UINT32_MAX;
        return output;
    }

    void VertexAnimationDeformer::RetireUnused(uint64_t frame)
    {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.lastUsedFrame + kKeepFrames < frame) {
                ReleaseEntry(it->second);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }
}
