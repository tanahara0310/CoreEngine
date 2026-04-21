#include "RayTracingShadowManager.h"
#include "AccelerationStructureManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

#include <d3d12.h>
#include <cassert>
#include <cstring>

namespace CoreEngine
{
    // =========================================================================
    // HLSL 側の cbuffer ShadowRayConstants とレイアウトを共有する構造体
    // HLSL: float3 gLightDirection, float gShadowBias,
    //       float gMaxRayDistance, float gLightRadius, int gSoftShadowSamples, uint gFrameIndex
    // =========================================================================
    struct alignas(16) ShadowRayConstants {
        float    lightDir[3];
        float    shadowBias;
        float    maxRayDistance;
        float    lightRadius;
        int      softShadowSamples;
        uint32_t frameIndex;
    };
    static_assert(sizeof(ShadowRayConstants) == 32, "ShadowRayConstants size mismatch with HLSL cbuffer");
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

        // ShaderCompilerでlib_6_6ライブラリとしてコンパイル
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();
        shaderBlob_.Attach(shaderCompiler.CompileShaderLibrary(L"RTShadow.hlsl"));
        if (!shaderBlob_) {
            log.Log("RayTracingShadowManager: Shader compile failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }
        log.Log("RayTracingShadowManager: Shader compiled",
            LogLevel::Info, LogCategory::Graphics);

        // グローバルルートシグネチャを構築
        globalRootSigMgr_
            .AddUAVTable("gShadowOutput", 0)  // u0: シェード出力
            .AddSRVTable("gScene", 0)  // t0: TLAS
            .AddSRVTable("gWorldPosition", 1)  // t1: G-Buffer ワールド座標
            .AddSRVTable("gNormalRoughness", 2)  // t2: G-Buffer 法線
            .AddRootConstants("ShadowRayConstants", 0,
                sizeof(ShadowRayConstants) / sizeof(uint32_t)); // b0: Root Constants
        if (!globalRootSigMgr_.Build(dxCommon_->GetDevice())) {
            log.Log("RayTracingShadowManager: Global root signature build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }
        log.Log("RayTracingShadowManager: Global root signature created",
            LogLevel::Info, LogCategory::Graphics);

        // State Objectを構築
        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob_.Get())
            .AddHitGroup({ L"RTShadowHitGroup", L"RTShadowClosestHit" })
            .SetShaderConfig(sizeof(float))  // ShadowPayload = float 1個
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        if (!pipelineBuilder.Build(dxCommon_->GetDevice(), stateObject_, stateObjectProperties_)) {
            log.Log("RayTracingShadowManager: State object build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }
        log.Log("RayTracingShadowManager: State object created",
            LogLevel::Info, LogCategory::Graphics);

        // Shader Tableを構築
        shaderTableBuilder_
            .SetRayGenShader(L"RTShadowRayGen")
            .AddMissShader(L"RTShadowMiss")
            .AddHitGroup(L"RTShadowHitGroup");
        if (!shaderTableBuilder_.Build(dxCommon_->GetDevice(), stateObjectProperties_.Get())) {
            log.Log("RayTracingShadowManager: Shader table build failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }
        log.Log("RayTracingShadowManager: Shader table created",
            LogLevel::Info, LogCategory::Graphics);

        isInitialized_ = true;
        shaderBlob_.Reset();  // State Object構築後は不要
        log.Log("RayTracingShadowManager: Initialized successfully",
            LogLevel::Info, LogCategory::Graphics);
        return true;
    }

    // =========================================================================
    // 出力テクスチャの確保（リサイズ対応、ビューごと）
    // =========================================================================
    bool RayTracingShadowManager::EnsureOutputTexture(UINT width, UINT height, uint32_t viewIndex, uint32_t lightIndex)
    {
        auto& view = views_[viewIndex][lightIndex];
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

        view.currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        // UAV 作成
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        std::string uavName = "RTShadow_UAV_v" + std::to_string(viewIndex) + "_l" + std::to_string(lightIndex);
        descriptorManager_->CreateUAV(view.texture.Get(), uavDesc,
            view.uavCpuHandle, view.uavHandle, uavName);

        // SRV 作成（DeferredLighting でサンプリング用）
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        std::string srvName = "RTShadow_SRV_v" + std::to_string(viewIndex) + "_l" + std::to_string(lightIndex);
        descriptorManager_->CreateSRV(view.texture.Get(), srvDesc,
            view.srvCpuHandle, view.srvHandle, srvName);

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "RayTracingShadowManager: Output texture[view={} light={}] created ({}x{})",
            viewIndex, lightIndex, width, height);

        return true;
    }

    // =========================================================================
    // アクセサ
    // =========================================================================
    void RayTracingShadowManager::Resize(UINT width, UINT height, ViewID viewId, uint32_t lightIndex)
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        EnsureOutputTexture(width, height, static_cast<uint32_t>(viewId), lightIndex);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetShadowSRVHandle(
        ViewID viewId, uint32_t lightIndex) const
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return views_[static_cast<uint32_t>(viewId)][lightIndex].srvHandle;
    }

    bool RayTracingShadowManager::IsDispatchedThisFrame(
        ViewID viewId, uint32_t lightIndex) const
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return views_[static_cast<uint32_t>(viewId)][lightIndex].dispatchedThisFrame;
    }

    void RayTracingShadowManager::ResetFrameState()
    {
        for (auto& viewRow : views_) {
            for (auto& v : viewRow) {
                v.dispatchedThisFrame = false;
            }
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
        ViewID viewId,
        uint32_t lightIndex)
    {
        if (!isInitialized_ || !asMgr_->IsSupported()) return;
        if (asMgr_->GetBLASCount() == 0) return;

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch START viewId={} lightIndex={} size={}x{} BLAS={}",
                vi, lightIndex, width, height, asMgr_->GetBLASCount());
        }

        // 出力テクスチャの確保（リサイズ対応）
        EnsureOutputTexture(width, height, vi, lightIndex);

        // 定数データを構築（Root Constants でコマンドストリームに直接埋め込む）
        ShadowRayConstants constants{};
        constants.lightDir[0]       = lightDirection.x;
        constants.lightDir[1]       = lightDirection.y;
        constants.lightDir[2]       = lightDirection.z;
        constants.shadowBias        = settings_.shadowBias;
        constants.maxRayDistance    = settings_.maxRayDistance;
        constants.lightRadius       = settings_.lightRadius;
        constants.softShadowSamples = settings_.softShadowSamples;
        constants.frameIndex        = frameIndex_++;

        // CommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) return;

        // パイプライン設定
        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        // PSR → UAV 遷移（履歴が同じ場合は自動スキップ）
        ResourceBarrierHelper::Transition(
            cmdList, view.texture.Get(),
            view.currentState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // ルートパラメータのバインド（インデックスはGlobalRootSignatureManagerから取得）
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gShadowOutput")),
            view.uavHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene")),
            asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWorldPosition")),
            worldPositionSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gNormalRoughness")),
            normalRoughnessSRV);
        // Root Constants: 定数をコマンドストリームに直接埋め込む（バッファ同期問題を回避）
        cmdList->SetComputeRoot32BitConstants(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("ShadowRayConstants")),
            sizeof(ShadowRayConstants) / sizeof(uint32_t),
            &constants,
            0);

        // DispatchRays
        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        cmdList4->DispatchRays(&dispatchDesc);

        // UAV バリア（DispatchRays 完了を保証）
        ResourceBarrierHelper::UAV(cmdList, view.texture.Get());

        // UAV → PIXEL_SHADER_RESOURCE 遷移（DeferredLighting で SRV として読み取るため）
        ResourceBarrierHelper::Transition(
            cmdList, view.texture.Get(),
            view.currentState,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        view.dispatchedThisFrame = true;

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch complete viewId={} lightIndex={} srvHandle=0x{:X}",
                vi, lightIndex, view.srvHandle.ptr);
            ++dispatchLogCount_;
        }
    }
}
