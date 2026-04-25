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
    //       float gMaxRayDistance, float gLightRadius, int gSoftShadowSamples,
    //       uint gFrameIndex, float gHistoryAlpha, float3 gPadding_
    // =========================================================================
    struct alignas(16) ShadowRayConstants {
        float    lightDir[3];        // offset  0
        float    shadowBias;         // offset 12  → row1 終了(16)
        float    maxRayDistance;     // offset 16
        float    lightRadius;        // offset 20
        int      softShadowSamples;  // offset 24
        uint32_t frameIndex;         // offset 28  → row2 終了(32)
        float    historyAlpha;       // offset 32
        float    screenWidth;        // offset 36
        float    screenHeight;       // offset 40
        float    padding;            // offset 44  → row3 終了(48)
    };
    static_assert(sizeof(ShadowRayConstants) == 48, "ShadowRayConstants size mismatch with HLSL cbuffer");
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
            .AddUAVTable("gShadowOutput", 0)      // u0: シャドウ出力
            .AddSRVTable("gScene", 0)              // t0: TLAS
            .AddSRVTable("gWorldPosition", 1)      // t1: G-Buffer ワールド座標
            .AddSRVTable("gNormalRoughness", 2)    // t2: G-Buffer 法線
            .AddSRVTable("gHistoryShadow", 3)      // t3: テンポラル蓄積履歴
            .AddSRVTable("gMotionVector", 4)       // t4: モーションベクター
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

        // =========================================================
        // A-Trous デノイズ用コンピュートパイプライン構築
        // =========================================================
        {
            // ルートシグネチャ: t0=InputShadow, t1=Normal, t2=WorldPos, u0=Output, b0=Constants
            D3D12_DESCRIPTOR_RANGE srvRanges[3]{};
            srvRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRanges[0].NumDescriptors = 1;
            srvRanges[0].BaseShaderRegister = 0; // t0
            srvRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRanges[1].NumDescriptors = 1;
            srvRanges[1].BaseShaderRegister = 1; // t1
            srvRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRanges[2].NumDescriptors = 1;
            srvRanges[2].BaseShaderRegister = 2; // t2

            D3D12_DESCRIPTOR_RANGE uavRange{};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 1;
            uavRange.BaseShaderRegister = 0; // u0

            D3D12_ROOT_PARAMETER rootParams[5]{};
            rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[0].DescriptorTable.pDescriptorRanges = &srvRanges[0];
            rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[1].DescriptorTable.pDescriptorRanges = &srvRanges[1];
            rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[2].DescriptorTable.pDescriptorRanges = &srvRanges[2];
            rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[3].DescriptorTable.pDescriptorRanges = &uavRange;
            rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            rootParams[4].Constants.ShaderRegister = 0;
            rootParams[4].Constants.RegisterSpace = 0;
            rootParams[4].Constants.Num32BitValues = 8; // DenoiseConstants
            rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 5;
            rsDesc.pParameters = rootParams;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            Microsoft::WRL::ComPtr<ID3DBlob> rsBlob, rsError;
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError);
            dxCommon_->GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                rsBlob->GetBufferSize(), IID_PPV_ARGS(&denoiseRootSignature_));

            // コンピュートシェーダーをコンパイル
            ShaderCompiler denoiseCompiler;
            denoiseCompiler.Initialize();
            Microsoft::WRL::ComPtr<IDxcBlob> csBlob;
            csBlob.Attach(denoiseCompiler.CompileShader(L"RTShadowDenoise.hlsl", L"cs_6_6"));

            if (csBlob && csBlob->GetBufferSize() > 0)
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = denoiseRootSignature_.Get();
                psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
                psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
                HRESULT psoHr = dxCommon_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&denoisePipelineState_));
                if (SUCCEEDED(psoHr)) {
                    denoiseInitialized_ = true;
                    log.Log("RayTracingShadowManager: A-Trous denoise pipeline created",
                        LogLevel::Info, LogCategory::Graphics);
                } else {
                    log.Log("RayTracingShadowManager: A-Trous denoise PSO creation failed",
                        LogLevel::Error, LogCategory::Graphics);
                }
            } else
            {
                log.Log("RayTracingShadowManager: A-Trous denoise shader compile failed, denoising disabled",
                    LogLevel::Warn, LogCategory::Graphics);
            }
        }

        // =========================================================
        // テンポラル蓄積用コンピュートパイプライン構築
        // =========================================================
        {
            // t0=RawShadow, t1=Normal, t2=WorldPos, t3=History, t4=MotionVector, u0=Output, b0=Constants
            D3D12_DESCRIPTOR_RANGE srvRanges[5]{};
            for (UINT i = 0; i < 5; ++i) {
                srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                srvRanges[i].NumDescriptors = 1;
                srvRanges[i].BaseShaderRegister = i;
            }

            D3D12_DESCRIPTOR_RANGE uavRange{};
            uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRange.NumDescriptors = 1;
            uavRange.BaseShaderRegister = 0;

            D3D12_ROOT_PARAMETER rootParams[7]{};
            for (UINT i = 0; i < 5; ++i) {
                rootParams[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                rootParams[i].DescriptorTable.NumDescriptorRanges = 1;
                rootParams[i].DescriptorTable.pDescriptorRanges = &srvRanges[i];
                rootParams[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            }
            rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[5].DescriptorTable.pDescriptorRanges = &uavRange;
            rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            rootParams[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            rootParams[6].Constants.ShaderRegister = 0;
            rootParams[6].Constants.Num32BitValues = 8; // TemporalConstants (int,int,float,float + 4 pad)
            rootParams[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc{};
            rsDesc.NumParameters = 7;
            rsDesc.pParameters = rootParams;
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

            Microsoft::WRL::ComPtr<ID3DBlob> rsBlob, rsError;
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError);
            dxCommon_->GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(),
                rsBlob->GetBufferSize(), IID_PPV_ARGS(&temporalRootSignature_));

            ShaderCompiler temporalCompiler;
            temporalCompiler.Initialize();
            Microsoft::WRL::ComPtr<IDxcBlob> csBlob;
            csBlob.Attach(temporalCompiler.CompileShader(L"RTShadowTemporal.CS.hlsl", L"cs_6_6"));

            if (csBlob && csBlob->GetBufferSize() > 0)
            {
                D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
                psoDesc.pRootSignature = temporalRootSignature_.Get();
                psoDesc.CS.pShaderBytecode = csBlob->GetBufferPointer();
                psoDesc.CS.BytecodeLength = csBlob->GetBufferSize();
                HRESULT psoHr = dxCommon_->GetDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&temporalPipelineState_));
                if (SUCCEEDED(psoHr)) {
                    temporalInitialized_ = true;
                    log.Log("RayTracingShadowManager: Temporal accumulation pipeline created",
                        LogLevel::Info, LogCategory::Graphics);
                } else {
                    log.Log("RayTracingShadowManager: Temporal PSO creation failed",
                        LogLevel::Error, LogCategory::Graphics);
                }
            }
            else
            {
                log.Log("RayTracingShadowManager: Temporal shader compile failed",
                    LogLevel::Warn, LogCategory::Graphics);
            }
        }

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
        // サイズ変更時は履歴を無効化（古いサイズの履歴を引き継がない）
        view.isHistoryValid = false;

        // R32_FLOAT UAV テクスチャ（シャドウ出力）
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

        // -------------------------------------------------------
        // テンポラル蓄積用 履歴テクスチャ（UAV なし、COPY_DEST + SRV のみ）
        // -------------------------------------------------------
        D3D12_RESOURCE_DESC histDesc = texDesc;
        histDesc.Flags = D3D12_RESOURCE_FLAG_NONE; // UAV 不要

        hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &histDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&view.historyTexture));
        if (FAILED(hr)) return false;

        view.historyCurrentState = D3D12_RESOURCE_STATE_COMMON;

        // 履歴テクスチャの SRV（RTShadow.hlsl の gHistoryShadow に対応）
        std::string histSrvName = "RTShadow_Hist_v" + std::to_string(viewIndex) + "_l" + std::to_string(lightIndex);
        descriptorManager_->CreateSRV(view.historyTexture.Get(), srvDesc,
            view.historySrvCpuHandle, view.historySrvHandle, histSrvName);

        // -------------------------------------------------------
        // A-Trous デノイズ用中間バッファ（ping-pong のもう片面）
        // UAV + SRV 両方必要
        // -------------------------------------------------------
        D3D12_RESOURCE_DESC denoiseDesc = texDesc; // R32_FLOAT UAV フラグ付き
        hr = dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &denoiseDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, IID_PPV_ARGS(&view.denoiseTemp));
        if (FAILED(hr)) return false;

        view.denoiseTempState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        std::string denoiseTempUavName = "RTShadow_DenoiseTemp_UAV_v" + std::to_string(viewIndex) + "_l" + std::to_string(lightIndex);
        descriptorManager_->CreateUAV(view.denoiseTemp.Get(), uavDesc,
            view.denoiseTempUavCpuHandle, view.denoiseTempUavHandle, denoiseTempUavName);

        std::string denoiseTempSrvName = "RTShadow_DenoiseTemp_SRV_v" + std::to_string(viewIndex) + "_l" + std::to_string(lightIndex);
        descriptorManager_->CreateSRV(view.denoiseTemp.Get(), srvDesc,
            view.denoiseTempSrvCpuHandle, view.denoiseTempSrvHandle, denoiseTempSrvName);

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
        D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
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

        // 定数データを構築（テンポラル蓄積は ApplyTemporal 側で行うので
        // historyAlpha / screenWidth / screenHeight はシェーダー側では未使用）
        ShadowRayConstants constants{};
        constants.lightDir[0] = lightDirection.x;
        constants.lightDir[1] = lightDirection.y;
        constants.lightDir[2] = lightDirection.z;
        constants.shadowBias = settings_.shadowBias;
        constants.maxRayDistance = settings_.maxRayDistance;
        constants.lightRadius = settings_.lightRadius;
        constants.softShadowSamples = settings_.softShadowSamples;
        constants.frameIndex = frameIndex_++;
        constants.historyAlpha = 1.0f; // RayGen側では使わない
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);

        // CommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) return;

        // パイプライン設定
        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        // RayGen は生バイナリを denoiseTemp に書き込む（ApplyTemporal が読む）
        ResourceBarrierHelper::Transition(
            cmdList, view.denoiseTemp.Get(),
            view.denoiseTempState,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // ルートパラメータのバインド（history / motionVector は未使用だがスロット維持のためダミーで埋める）
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gShadowOutput")),
            view.denoiseTempUavHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene")),
            asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gWorldPosition")),
            worldPositionSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gNormalRoughness")),
            normalRoughnessSRV);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gHistoryShadow")),
            view.historySrvHandle);
        cmdList->SetComputeRootDescriptorTable(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gMotionVector")),
            motionVectorSRV);

        cmdList->SetComputeRoot32BitConstants(
            static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("ShadowRayConstants")),
            sizeof(ShadowRayConstants) / sizeof(uint32_t),
            &constants,
            0);

        // DispatchRays: 生バイナリを denoiseTemp に書き出す
        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        cmdList4->DispatchRays(&dispatchDesc);

        // ApplyTemporal の読み取り前に完了を保証
        ResourceBarrierHelper::UAV(cmdList, view.denoiseTemp.Get());

        // denoiseTemp を SRV 状態へ
        ResourceBarrierHelper::Transition(
            cmdList, view.denoiseTemp.Get(),
            view.denoiseTempState,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        view.dispatchedThisFrame = true;

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch complete viewId={} lightIndex={} srvHandle=0x{:X}",
                vi, lightIndex, view.srvHandle.ptr);
            ++dispatchLogCount_;
        }
    }

    // =========================================================================
    // A-Trous デノイズ（Dispatch の直後に呼ぶ）
    // =========================================================================
    void RayTracingShadowManager::Denoise(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
        UINT width, UINT height,
        ViewID viewId,
        uint32_t lightIndex)
    {
        if (!isInitialized_ || !denoiseInitialized_) return;

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];

        if (!view.texture || !view.denoiseTemp) return;

        // DenoiseConstants（8 x 32bit = 32byte）
        struct DenoiseConstants {
            int   stepSize;
            float phiShadow;
            float phiNormal;
            float phiDepth;
            int   screenWidth;
            int   screenHeight;
            float padding[2];
        };

        // À-Trous 3パス: ステップ幅 1 → 2 → 4
        // ping=view.texture / pong=view.denoiseTemp で交互に入出力
        // 最終出力を常に view.texture にするためパス数は奇数にする
        // パス数は「偶数」にすること:
        //   ping-pong は pass%2==0 で texture→denoiseTemp, pass%2==1 で denoiseTemp→texture
        //   最後のパスが奇数インデックス（pass = kNumPasses-1 が奇数）のとき texture に書き込まれる
        //   → kNumPasses が偶数のとき、最終パスインデックス = kNumPasses-1 は奇数 → texture に書き込み ✓
        static const int   kSteps[] = { 1,    2,    4,    8 };
        static const float kPhiNormal[] = { 4.0f, 8.0f, 16.0f, 32.0f };
        static const float kPhiShadow[] = { 1.0f, 0.7f,  0.5f,  0.35f };
        static_assert((sizeof(kSteps) / sizeof(kSteps[0])) % 2 == 0,
            "kNumPasses must be even so the final result lands in view.texture");
        static const int kNumPasses = sizeof(kSteps) / sizeof(kSteps[0]);

        // view.texture は Dispatch 直後に PIXEL_SHADER_RESOURCE 状態になっているので
        // 最初のパスで SRV として読む前にそのまま使える（NON_PIXEL_SHADER_RESOURCE に遷移）
        ResourceBarrierHelper::Transition(cmdList, view.texture.Get(),
            view.currentState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetComputeRootSignature(denoiseRootSignature_.Get());
        cmdList->SetPipelineState(denoisePipelineState_.Get());

        const UINT groupX = (width + 7) / 8;
        const UINT groupY = (height + 7) / 8;

        for (int pass = 0; pass < kNumPasses; ++pass)
        {
            // pass が偶数: texture → denoiseTemp / 奇数: denoiseTemp → texture
            bool pingToTemp = (pass % 2 == 0);

            ID3D12Resource* inputRes = pingToTemp ? view.texture.Get() : view.denoiseTemp.Get();
            ID3D12Resource* outputRes = pingToTemp ? view.denoiseTemp.Get() : view.texture.Get();
            D3D12_RESOURCE_STATES& inputState = pingToTemp ? view.currentState : view.denoiseTempState;
            D3D12_RESOURCE_STATES& outputState = pingToTemp ? view.denoiseTempState : view.currentState;
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrv = pingToTemp ? view.srvHandle : view.denoiseTempSrvHandle;
            D3D12_GPU_DESCRIPTOR_HANDLE outputUav = pingToTemp ? view.denoiseTempUavHandle : view.uavHandle;

            // 入力: SRV へ
            ResourceBarrierHelper::Transition(cmdList, inputRes, inputState,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            // 出力: UAV へ
            ResourceBarrierHelper::Transition(cmdList, outputRes, outputState,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            // バインド
            cmdList->SetComputeRootDescriptorTable(0, inputSrv);          // t0: InputShadow
            cmdList->SetComputeRootDescriptorTable(1, normalRoughnessSRV); // t1: Normal
            cmdList->SetComputeRootDescriptorTable(2, worldPositionSRV);   // t2: WorldPos
            cmdList->SetComputeRootDescriptorTable(3, outputUav);          // u0: Output

            DenoiseConstants dc{};
            dc.stepSize = kSteps[pass];
            dc.phiShadow = kPhiShadow[pass];
            dc.phiNormal = kPhiNormal[pass];
            dc.phiDepth = 1.0f;
            dc.screenWidth = static_cast<int>(width);
            dc.screenHeight = static_cast<int>(height);
            cmdList->SetComputeRoot32BitConstants(4, 8, &dc, 0);

            cmdList->Dispatch(groupX, groupY, 1);

            // UAV バリア（次パスの読み取りを保証）
            ResourceBarrierHelper::UAV(cmdList, outputRes);
        }

        // 最終結果は view.texture（kNumPasses が偶数のため）
        ResourceBarrierHelper::Transition(cmdList, view.texture.Get(),
            view.currentState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ResourceBarrierHelper::Transition(cmdList, view.denoiseTemp.Get(),
            view.denoiseTempState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // 履歴へのコピーは ApplyTemporal で行う（A-Trous 出力は履歴に書かない）
    }

    // =========================================================================
    // テンポラル蓄積パス（RayGenの直後、Denoiseの前に呼び出す）
    // =========================================================================
    void RayTracingShadowManager::ApplyTemporal(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE worldPositionSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
        UINT width, UINT height,
        ViewID viewId,
        uint32_t lightIndex)
    {
        if (!isInitialized_ || !temporalInitialized_) return;

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];

        if (!view.texture || !view.denoiseTemp || !view.historyTexture) return;

        // 入出力の状態遷移
        ResourceBarrierHelper::Transition(cmdList, view.denoiseTemp.Get(),
            view.denoiseTempState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, view.texture.Get(),
            view.currentState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, view.historyTexture.Get(),
            view.historyCurrentState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetComputeRootSignature(temporalRootSignature_.Get());
        cmdList->SetPipelineState(temporalPipelineState_.Get());

        // t0=RawShadow, t1=Normal, t2=WorldPos, t3=History, t4=MotionVector, u0=Output
        cmdList->SetComputeRootDescriptorTable(0, view.denoiseTempSrvHandle);
        cmdList->SetComputeRootDescriptorTable(1, normalRoughnessSRV);
        cmdList->SetComputeRootDescriptorTable(2, worldPositionSRV);
        cmdList->SetComputeRootDescriptorTable(3, view.historySrvHandle);
        cmdList->SetComputeRootDescriptorTable(4, motionVectorSRV);
        cmdList->SetComputeRootDescriptorTable(5, view.uavHandle);

        // ── 定数 ──
        struct TemporalConstants {
            int   screenWidth;
            int   screenHeight;
            float historyAlpha;
            float disableHistory; // 1.0 = 履歴無効（初回フレーム）
            float padding[4];
        };
        TemporalConstants tc{};
        tc.screenWidth = static_cast<int>(width);
        tc.screenHeight = static_cast<int>(height);
        tc.historyAlpha = settings_.historyAlpha;
        tc.disableHistory = view.isHistoryValid ? 0.0f : 1.0f;
        cmdList->SetComputeRoot32BitConstants(6, 8, &tc, 0);

        const UINT groupX = (width + 7) / 8;
        const UINT groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);

        ResourceBarrierHelper::UAV(cmdList, view.texture.Get());

        // テンポラル結果を履歴にコピー（次フレームの参照用）
        ResourceBarrierHelper::Transition(cmdList, view.texture.Get(),
            view.currentState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, view.historyTexture.Get(),
            view.historyCurrentState, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(view.historyTexture.Get(), view.texture.Get());

        // 状態を後段（A-Trous）向けに整える
        ResourceBarrierHelper::Transition(cmdList, view.historyTexture.Get(),
            view.historyCurrentState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, view.texture.Get(),
            view.currentState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        view.isHistoryValid = true;
    }
}
