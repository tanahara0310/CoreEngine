#include "pch.h"
#include "VertexAnimationDeformer.h"

#include "Graphics/Model/Model.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/Render/Model/VertexAnimation.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
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
        if (graphicsCore_) {
            for (auto& [owner, entry] : entries_) {
                (void)owner;
                if (entry.positions) {
                    graphicsCore_->DeferRelease(entry.positions.Get());
                    entry.positions.Release();
                }
            }
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
            IDxcBlob* csBlob = shaderCompiler_->CompileShader(
                L"Engine/Assets/Shaders/RayTracing/VertexAnimationDeform.CS.hlsl", L"cs_6_0");
            if (!csBlob) {
                log.Warnf(LogCategory::Graphics, "VertexAnimationDeformer: CS のコンパイルに失敗（揺れは影に反映しない）");
                return false;
            }

            reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();
            reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
            reflectionData_ = reflectionBuilder_->BuildFromComputeShader(csBlob, "VertexAnimationDeform");

            // すべてルート引数で渡す（ディスクリプタを確保しない）。
            // 定数: gVertexAnim 8 + gDeform 36、ディスクリプタ: CBV / SRV x2 / UAV で計 52 DWORD（上限 64）
            RootSignatureConfig config;
            config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
            config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
            config.ConfigureResource("gVertexAnim", BindingStrategy::RootConstants);
            config.ConfigureResource("gDeform", BindingStrategy::RootConstants);
            config.ConfigureResource("gSourceVertices", BindingStrategy::RootDescriptor);
            config.ConfigureResource("gIndices", BindingStrategy::RootDescriptor);
            config.ConfigureResource("gOutputPositions", BindingStrategy::RootDescriptor);

            rootSignatureMg_ = std::make_unique<RootSignatureManager>();
            const auto buildResult = rootSignatureMg_->Build(device, *reflectionData_, config);
            if (!buildResult.success) {
                log.Warnf(LogCategory::Graphics, "VertexAnimationDeformer: ルートシグネチャの構築に失敗: {}",
                    buildResult.errorMessage);
                return false;
            }

            pso_ = ComputePipelineUtil::Create(
                device, rootSignatureMg_->GetRootSignature(), csBlob, "VertexAnimationDeform");
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
        log.Infof(LogCategory::Graphics, "VertexAnimationDeformer: 揺れる植物の BLAS 用 CS を構築");
        return true;
    }

    ID3D12Resource* VertexAnimationDeformer::Deform(ID3D12GraphicsCommandList* cmdList, const void* owner,
        const Model& model, const Matrix4x4& world, const VertexAnimationParams& params, uint64_t frame)
    {
        if (!cmdList || !owner || !EnsurePipeline()) {
            return nullptr;
        }
        const ModelResource* resource = model.GetModelResource();
        if (!resource || !resource->IsLoaded() || !resource->GetVertexBuffer() || !resource->GetIndexBuffer()) {
            return nullptr;
        }
        const uint32_t vertexCount = resource->GetVertexCount();
        if (vertexCount == 0 || resource->GetIndexCount() == 0) {
            return nullptr;
        }

        // ===== 持ち主ごとの位置バッファ（頂点数が変わったら張り直す） =====
        Entry& entry = entries_[owner];
        entry.lastUsedFrame = frame;
        if (!entry.positions || entry.vertexCount != vertexCount) {
            if (entry.positions) {
                graphicsCore_->DeferRelease(entry.positions.Get());
                entry.positions.Release();
            }
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = static_cast<UINT64>(vertexCount) * sizeof(float) * 3;
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
                return nullptr;
            }
            buffer->SetName(L"VertexAnimationDeform Positions");
            entry.positions.Reset(buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            entry.vertexCount = vertexCount;
        }

        // ===== サブメッシュ（マテリアル）ごとに、そのインデックスが指す頂点を書く =====
        Barrier::Transition(cmdList, entry.positions, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetComputeRootSignature(rootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(pso_.Get());

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.SetConstants(bindings_[VertexAnimationDeformBind::gVertexAnim], params);
        binder.Set(bindings_[VertexAnimationDeformBind::gSourceVertices], resource->GetVertexBuffer()->GetGPUVirtualAddress());
        binder.Set(bindings_[VertexAnimationDeformBind::gIndices], resource->GetIndexBuffer()->GetGPUVirtualAddress());
        binder.Set(bindings_[VertexAnimationDeformBind::gOutputPositions], entry.positions.GpuAddress());

        DeformConstants constants{};
        constants.world = world;
        constants.worldInverseTranspose = MathCore::Matrix::Transpose(MathCore::Matrix::Inverse(world));
        constants.vertexCount = vertexCount;
        for (const SubMeshData& subMesh : resource->GetSubMeshes()) {
            if (subMesh.indexCount == 0) {
                continue;
            }
            // 揺れないマテリアル（なし・魚）のサブメッシュも、元の位置を書くために回す
            constants.indexStart = subMesh.startIndex;
            constants.indexCount = subMesh.indexCount;
            binder.Set(bindings_[VertexAnimationDeformBind::gMaterial], model.GetMaterialCBVAddress(subMesh.materialIndex));
            binder.SetConstants(bindings_[VertexAnimationDeformBind::gDeform], constants);
            binder.ValidateBeforeDraw(bindings_);
            cmdList->Dispatch((subMesh.indexCount + kThreadGroupSize - 1) / kThreadGroupSize, 1, 1);
        }

        // BLAS の入力は NON_PIXEL_SHADER_RESOURCE（UAV の書き込みの完了もこの遷移が保証する）
        Barrier::Transition(cmdList, entry.positions, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        return entry.positions.Get();
    }

    void VertexAnimationDeformer::RetireUnused(uint64_t frame)
    {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.lastUsedFrame + kKeepFrames < frame) {
                if (it->second.positions && graphicsCore_) {
                    graphicsCore_->DeferRelease(it->second.positions.Get());
                    it->second.positions.Release();
                }
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
    }
}
