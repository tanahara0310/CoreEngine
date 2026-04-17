#include "RayTracingShadowManager.h"
#include "AccelerationStructureManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Utility/Logger/Logger.h"

#include <dxcapi.h>
#include <d3d12.h>
#include <cassert>
#include <filesystem>
#include <vector>
#include <cstring>

#pragma comment(lib, "dxcompiler.lib")

namespace CoreEngine
{
    // =========================================================================
    // 定数
    // =========================================================================
    static constexpr UINT kShaderTableAlignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    static constexpr UINT kShaderRecordAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

    static const wchar_t* kRayGenName   = L"RTShadowRayGen";
    static const wchar_t* kMissName     = L"RTShadowMiss";
    static const wchar_t* kHitGroupName = L"RTShadowHitGroup";
    static const wchar_t* kClosestHitName = L"RTShadowClosestHit";

    // =========================================================================
    // ヘルパー: サイズを alignment の倍数に切り上げ
    // =========================================================================
    static UINT64 Align(UINT64 size, UINT64 alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    // =========================================================================
    // Initialize
    // =========================================================================
    bool RayTracingShadowManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        dxCommon_ = dxCommon;
        descriptorManager_ = descriptorManager;
        asMgr_ = asMgr;

        Logger& log = Logger::GetInstance();

        if (!asMgr_ || !asMgr_->IsSupported()) {
            log.Log("RayTracingShadowManager: DXR not supported, skipping",
                LogLevel::Warn, LogCategory::Graphics);
            return false;
        }

        if (!CompileShader()) return false;
        log.Log("RayTracingShadowManager: Shader compiled",
            LogLevel::Info, LogCategory::Graphics);

        if (!CreateRootSignature()) return false;
        log.Log("RayTracingShadowManager: Root signature created",
            LogLevel::Info, LogCategory::Graphics);

        if (!CreateStateObject()) return false;
        log.Log("RayTracingShadowManager: State object created",
            LogLevel::Info, LogCategory::Graphics);

        if (!CreateShaderTable()) return false;
        log.Log("RayTracingShadowManager: Shader table created",
            LogLevel::Info, LogCategory::Graphics);

        // 定数バッファの作成
        constantBuffer_ = ResourceFactory::CreateBufferResource(
            dxCommon_->GetDevice(), sizeof(float) * 8);

        isInitialized_ = true;
        log.Log("RayTracingShadowManager: Initialized successfully",
            LogLevel::Info, LogCategory::Graphics);
        return true;
    }

    // =========================================================================
    // シェーダーコンパイル（lib_6_6 ライブラリとして）
    // =========================================================================
    bool RayTracingShadowManager::CompileShader()
    {
        Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
        Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
        Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;

        HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
        if (FAILED(hr)) return false;
        hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
        if (FAILED(hr)) return false;
        hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
        if (FAILED(hr)) return false;

        // シェーダーファイルのパスを解決
        std::string narrowPath = "RTShadow.hlsl";
        std::string assetPath = AssetDatabase::GetInstance().FindAssetPath(narrowPath);
        if (assetPath.empty()) assetPath = narrowPath;
        std::wstring shaderPath = std::filesystem::path(assetPath).wstring();

        // ファイル読み込み
        IDxcBlobEncoding* sourceBlob = nullptr;
        hr = dxcUtils->LoadFile(shaderPath.c_str(), nullptr, &sourceBlob);
        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "Failed to load RT shader: {}", assetPath);
            return false;
        }

        DxcBuffer sourceBuffer;
        sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
        sourceBuffer.Size = sourceBlob->GetBufferSize();
        sourceBuffer.Encoding = DXC_CP_UTF8;

        // DXR シェーダーは lib_6_6 でコンパイル（エントリーポイント指定なし）
        LPCWSTR arguments[] = {
            shaderPath.c_str(),
            L"-T", L"lib_6_6",
            L"-Zi",
            L"-Od",
            L"-Zpr",
            L"-I", L"Engine/Assets/Shaders",
        };

        IDxcResult* result = nullptr;
        hr = dxcCompiler->Compile(&sourceBuffer, arguments, _countof(arguments),
            includeHandler.Get(), IID_PPV_ARGS(&result));

        // エラーチェック
        IDxcBlobUtf8* errors = nullptr;
        result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors && errors->GetStringLength() > 0) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "RT Shader compile errors:\n{}", errors->GetStringPointer());
        }

        HRESULT status;
        result->GetStatus(&status);
        if (FAILED(status)) return false;

        IDxcBlob* shaderBlob = nullptr;
        result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
        // IDxcBlob → ID3DBlob にキャスト（同一 COM インターフェース）
        if (shaderBlob) {
            shaderBlob->QueryInterface(IID_PPV_ARGS(&shaderBlob_));
            shaderBlob->Release();
        }

        sourceBlob->Release();
        result->Release();
        if (errors) errors->Release();

        return shaderBlob_ != nullptr;
    }

    // =========================================================================
    // グローバルルートシグネチャ
    // =========================================================================
    bool RayTracingShadowManager::CreateRootSignature()
    {
        // ルートパラメータ:
        // [0] UAV  u0: gShadowOutput
        // [1] SRV  t0: gScene (TLAS)
        // [2] SRV  t1: gWorldPosition
        // [3] SRV  t2: gNormalRoughness
        // [4] CBV  b0: ShadowRayConstants

        D3D12_DESCRIPTOR_RANGE1 uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE1 srvRange0{};
        srvRange0.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange0.NumDescriptors = 1;
        srvRange0.BaseShaderRegister = 0;
        srvRange0.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE1 srvRange1{};
        srvRange1.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange1.NumDescriptors = 1;
        srvRange1.BaseShaderRegister = 1;
        srvRange1.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE1 srvRange2{};
        srvRange2.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange2.NumDescriptors = 1;
        srvRange2.BaseShaderRegister = 2;
        srvRange2.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER1 params[5] = {};

        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &uavRange;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &srvRange0;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[2].DescriptorTable.NumDescriptorRanges = 1;
        params[2].DescriptorTable.pDescriptorRanges = &srvRange1;
        params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3].DescriptorTable.NumDescriptorRanges = 1;
        params[3].DescriptorTable.pDescriptorRanges = &srvRange2;
        params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[4].Descriptor.ShaderRegister = 0;
        params[4].Descriptor.RegisterSpace = 0;
        params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = _countof(params);
        desc.Desc_1_1.pParameters = params;
        desc.Desc_1_1.NumStaticSamplers = 0;
        desc.Desc_1_1.pStaticSamplers = nullptr;
        desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&desc,
            &signatureBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                    "Root signature error: {}",
                    static_cast<const char*>(errorBlob->GetBufferPointer()));
            }
            return false;
        }

        hr = dxCommon_->GetDevice()->CreateRootSignature(
            0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
            IID_PPV_ARGS(&globalRootSignature_));
        return SUCCEEDED(hr);
    }

    // =========================================================================
    // State Object (RTPSO) の構築
    // =========================================================================
    bool RayTracingShadowManager::CreateStateObject()
    {
        // サブオブジェクトを動的に構築
        std::vector<D3D12_STATE_SUBOBJECT> subobjects;
        subobjects.reserve(8);

        // 1. DXIL Library
        D3D12_DXIL_LIBRARY_DESC libDesc{};
        libDesc.DXILLibrary.pShaderBytecode = shaderBlob_->GetBufferPointer();
        libDesc.DXILLibrary.BytecodeLength = shaderBlob_->GetBufferSize();
        libDesc.NumExports = 0;  // エクスポート全て

        D3D12_STATE_SUBOBJECT libSubobject{};
        libSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        libSubobject.pDesc = &libDesc;
        subobjects.push_back(libSubobject);

        // 2. Hit Group
        D3D12_HIT_GROUP_DESC hitGroupDesc{};
        hitGroupDesc.HitGroupExport = kHitGroupName;
        hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitGroupDesc.ClosestHitShaderImport = kClosestHitName;
        hitGroupDesc.AnyHitShaderImport = nullptr;
        hitGroupDesc.IntersectionShaderImport = nullptr;

        D3D12_STATE_SUBOBJECT hitGroupSubobject{};
        hitGroupSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        hitGroupSubobject.pDesc = &hitGroupDesc;
        subobjects.push_back(hitGroupSubobject);

        // 3. Shader Config (payload + attribute size)
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes = sizeof(float);  // ShadowPayload
        shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;  // BuiltInTriangleIntersectionAttributes

        D3D12_STATE_SUBOBJECT shaderConfigSubobject{};
        shaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        shaderConfigSubobject.pDesc = &shaderConfig;
        subobjects.push_back(shaderConfigSubobject);

        // 4. Global Root Signature
        D3D12_GLOBAL_ROOT_SIGNATURE globalRS{};
        globalRS.pGlobalRootSignature = globalRootSignature_.Get();

        D3D12_STATE_SUBOBJECT globalRSSubobject{};
        globalRSSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        globalRSSubobject.pDesc = &globalRS;
        subobjects.push_back(globalRSSubobject);

        // 5. Pipeline Config
        D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
        pipelineConfig.MaxTraceRecursionDepth = 1;  // シャドウレイのみ、再帰なし

        D3D12_STATE_SUBOBJECT pipelineConfigSubobject{};
        pipelineConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        pipelineConfigSubobject.pDesc = &pipelineConfig;
        subobjects.push_back(pipelineConfigSubobject);

        // State Object の作成
        D3D12_STATE_OBJECT_DESC stateObjectDesc{};
        stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        stateObjectDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
        stateObjectDesc.pSubobjects = subobjects.data();

        Microsoft::WRL::ComPtr<ID3D12Device5> device5;
        HRESULT hr = dxCommon_->GetDevice()->QueryInterface(IID_PPV_ARGS(&device5));
        if (FAILED(hr)) return false;

        hr = device5->CreateStateObject(&stateObjectDesc, IID_PPV_ARGS(&stateObject_));
        if (FAILED(hr)) {
            Logger::GetInstance().Log("Failed to create RT state object",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        hr = stateObject_->QueryInterface(IID_PPV_ARGS(&stateObjectProperties_));
        return SUCCEEDED(hr);
    }

    // =========================================================================
    // Shader Table の構築
    // =========================================================================
    bool RayTracingShadowManager::CreateShaderTable()
    {
        // 各レコードは ShaderIdentifier (32 bytes) のみ
        // ただし各テーブルの StartAddress は 64 バイトアライメントが必要
        shaderTableRecordSize_ = Align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                                       kShaderRecordAlignment);

        // 各テーブルセクションを 64 バイト境界に配置
        UINT64 rayGenSectionSize = Align(shaderTableRecordSize_, kShaderTableAlignment);
        UINT64 missSectionSize = Align(shaderTableRecordSize_, kShaderTableAlignment);
        UINT64 hitGroupSectionSize = Align(shaderTableRecordSize_, kShaderTableAlignment);
        shaderTableSize_ = rayGenSectionSize + missSectionSize + hitGroupSectionSize;

        shaderTable_ = ResourceFactory::CreateBufferResource(
            dxCommon_->GetDevice(), static_cast<size_t>(shaderTableSize_));

        // マップして書き込み
        uint8_t* mapped = nullptr;
        shaderTable_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memset(mapped, 0, static_cast<size_t>(shaderTableSize_));

        // RayGen (オフセット 0)
        void* rayGenId = stateObjectProperties_->GetShaderIdentifier(kRayGenName);
        std::memcpy(mapped, rayGenId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        // Miss (オフセット rayGenSectionSize)
        void* missId = stateObjectProperties_->GetShaderIdentifier(kMissName);
        std::memcpy(mapped + rayGenSectionSize, missId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        // HitGroup (オフセット rayGenSectionSize + missSectionSize)
        void* hitGroupId = stateObjectProperties_->GetShaderIdentifier(kHitGroupName);
        std::memcpy(mapped + rayGenSectionSize + missSectionSize, hitGroupId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        shaderTable_->Unmap(0, nullptr);

        // DispatchRays 用のオフセットを保存
        rayGenOffset_ = 0;
        missOffset_ = rayGenSectionSize;
        hitGroupOffset_ = rayGenSectionSize + missSectionSize;
        sectionSize_ = rayGenSectionSize;  // 各セクション同サイズ

        return true;
    }

    // =========================================================================
    // シャドウ出力テクスチャの作成（リサイズ対応、ビューごと）
    // =========================================================================
    bool RayTracingShadowManager::CreateOutputTexture(UINT width, UINT height, uint32_t viewIndex)
    {
        auto& view = views_[viewIndex];
        if (width == view.width && height == view.height && view.texture) {
            return true;  // サイズ変更なし
        }

        view.width = width;
        view.height = height;

        // R32_FLOAT UAV テクスチャ
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R32_FLOAT;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, IID_PPV_ARGS(&view.texture));
        if (FAILED(hr)) return false;

        // UAV 作成
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        std::string uavName = "RTShadow_UAV_" + std::to_string(viewIndex);
        descriptorManager_->CreateUAV(view.texture.Get(), uavDesc,
            view.uavCpuHandle, view.uavHandle, uavName);

        // SRV 作成（DeferredLighting でサンプリング用）
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        std::string srvName = "RTShadow_SRV_" + std::to_string(viewIndex);
        descriptorManager_->CreateSRV(view.texture.Get(), srvDesc,
            view.srvCpuHandle, view.srvHandle, srvName);

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "RayTracingShadowManager: Output texture[{}] created ({}x{})", viewIndex, width, height);

        return true;
    }

    // =========================================================================
    // アクセサ
    // =========================================================================
    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetShadowSRVHandle(ViewID viewId) const
    {
        return views_[static_cast<uint32_t>(viewId)].srvHandle;
    }

    bool RayTracingShadowManager::IsDispatchedThisFrame(ViewID viewId) const
    {
        return views_[static_cast<uint32_t>(viewId)].dispatchedThisFrame;
    }

    void RayTracingShadowManager::ResetFrameState()
    {
        for (auto& v : views_) {
            v.dispatchedThisFrame = false;
        }
    }

    // =========================================================================
    // Dispatch（毎フレーム呼び出し）
    // =========================================================================
    void RayTracingShadowManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        const Vector3& lightDirection,
        UINT width, UINT height,
        ViewID viewId)
    {
        if (!isInitialized_ || !asMgr_->IsSupported()) return;
        if (asMgr_->GetBLASCount() == 0) return;

        uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi];

        static uint32_t dispatchLogCount = 0;
        if (dispatchLogCount < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch START viewId={} size={}x{} BLAS={} needsTransition={}",
                vi, width, height, asMgr_->GetBLASCount(), view.needsTransitionToUAV);
        }

        // 出力テクスチャの確保（リサイズ対応）
        CreateOutputTexture(width, height, vi);

        // 定数バッファ更新
        struct alignas(16) ShadowRayConstants {
            float lightDir[3];
            float shadowBias;
            float maxRayDistance;
            float padding[3];
        };
        ShadowRayConstants constants{};
        constants.lightDir[0] = lightDirection.x;
        constants.lightDir[1] = lightDirection.y;
        constants.lightDir[2] = lightDirection.z;
        constants.shadowBias = shadowBias_;
        constants.maxRayDistance = maxRayDistance_;

        void* mapped = nullptr;
        constantBuffer_->Map(0, nullptr, &mapped);
        std::memcpy(mapped, &constants, sizeof(constants));
        constantBuffer_->Unmap(0, nullptr);

        // CommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) return;

        // パイプライン設定
        cmdList4->SetComputeRootSignature(globalRootSignature_.Get());
        cmdList4->SetPipelineState1(stateObject_.Get());

        // 前フレームで PIXEL_SHADER_RESOURCE に遷移済みの場合、UAV に戻す
        if (view.texture && view.needsTransitionToUAV) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = view.texture.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmdList->ResourceBarrier(1, &barrier);
        }

        // ルートパラメータのバインド
        cmdList->SetComputeRootDescriptorTable(0, view.uavHandle);             // u0: gShadowOutput
        cmdList->SetComputeRootDescriptorTable(1, asMgr_->GetTLASSRVHandle()); // t0: gScene
        cmdList->SetComputeRootDescriptorTable(2, worldPositionSRV);           // t1: gWorldPosition
        cmdList->SetComputeRootDescriptorTable(3, normalRoughnessSRV);         // t2: gNormalRoughness
        cmdList->SetComputeRootConstantBufferView(4, constantBuffer_->GetGPUVirtualAddress()); // b0

        // DispatchRays
        D3D12_DISPATCH_RAYS_DESC dispatchDesc{};

        D3D12_GPU_VIRTUAL_ADDRESS tableBase = shaderTable_->GetGPUVirtualAddress();

        // RayGen
        dispatchDesc.RayGenerationShaderRecord.StartAddress = tableBase + rayGenOffset_;
        dispatchDesc.RayGenerationShaderRecord.SizeInBytes = shaderTableRecordSize_;

        // Miss
        dispatchDesc.MissShaderTable.StartAddress = tableBase + missOffset_;
        dispatchDesc.MissShaderTable.SizeInBytes = sectionSize_;
        dispatchDesc.MissShaderTable.StrideInBytes = shaderTableRecordSize_;

        // HitGroup
        dispatchDesc.HitGroupTable.StartAddress = tableBase + hitGroupOffset_;
        dispatchDesc.HitGroupTable.SizeInBytes = sectionSize_;
        dispatchDesc.HitGroupTable.StrideInBytes = shaderTableRecordSize_;

        dispatchDesc.Width = width;
        dispatchDesc.Height = height;
        dispatchDesc.Depth = 1;

        cmdList4->DispatchRays(&dispatchDesc);

        // UAV バリア（Dispatch 完了を保証）
        D3D12_RESOURCE_BARRIER uavBarrier{};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = view.texture.Get();
        cmdList->ResourceBarrier(1, &uavBarrier);

        // UAV → PIXEL_SHADER_RESOURCE に遷移（DeferredLighting で SRV として読み取るため）
        D3D12_RESOURCE_BARRIER srvBarrier{};
        srvBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        srvBarrier.Transition.pResource = view.texture.Get();
        srvBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        srvBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        srvBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &srvBarrier);

        view.needsTransitionToUAV = true;
        view.dispatchedThisFrame = true;

        if (dispatchLogCount < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch complete viewId={} srvHandle=0x{:X} uavHandle=0x{:X}",
                vi, view.srvHandle.ptr, view.uavHandle.ptr);
            ++dispatchLogCount;
        }
    }
}
