#include "pch.h"
#include "RayTracingShadowManager.h"
#include "AccelerationStructureManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"

#include <d3d12.h>
#include <algorithm>
#include <cassert>
#include <cmath>
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
        Matrix4x4 invViewProj;       // offset 48  → WorldPosition ターゲット廃止に伴う深度復元用(+64=112)
    };
    static_assert(sizeof(ShadowRayConstants) == 112, "ShadowRayConstants size mismatch with HLSL cbuffer");

    // =========================================================================
    // A-Trous デノイズ定数（HLSL: RTShadowDenoise.hlsl の DenoiseConstants と一致させる）
    // Stage 1 で invViewProj(16 float) を廃し、線形深度パラメータ float2 に置き換えた。
    // =========================================================================
    struct DenoiseConstants {
        int   stepSize;      // offset  0
        float phiShadow;     // offset  4
        float phiNormal;     // offset  8
        float phiDepth;      // offset 12 → row1 終了(16)
        int   screenWidth;   // offset 16
        int   screenHeight;  // offset 20
        float projM33;       // offset 24  proj._33 = far/(far-near)
        float projM43;       // offset 28  proj._43 = -near*far/(far-near) → row2 終了(32)
    };
    static_assert(sizeof(DenoiseConstants) == 32, "DenoiseConstants size mismatch with HLSL cbuffer");
    static constexpr UINT kDenoiseConstantCount = sizeof(DenoiseConstants) / sizeof(uint32_t);

    // =========================================================================
    // テンポラル蓄積定数（HLSL: RTShadowTemporal.CS.hlsl の TemporalConstants と一致させる）
    // 注意: HLSL の cbuffer は 16 バイト境界へ切り上げられるため、
    //       float2 gProjZW の後ろに 8 バイトの詰めが入る。C++ 側も同じサイズにする。
    // =========================================================================
    struct TemporalConstants {
        int   screenWidth;    // offset  0
        int   screenHeight;   // offset  4
        float historyAlpha;   // offset  8
        float disableHistory; // offset 12 → row1 終了(16)
        float projM33;        // offset 16
        float projM43;        // offset 20
        float padding[2];     // offset 24 → row2 終了(32)
    };
    static_assert(sizeof(TemporalConstants) == 32, "TemporalConstants size mismatch with HLSL cbuffer");
    static constexpr UINT kTemporalConstantCount = sizeof(TemporalConstants) / sizeof(uint32_t);

    namespace {
        /// @brief 投影行列から線形深度復元用の 2 要素を取り出す
        /// @details 行ベクトル規約の透視投影では ndcZ = _33 + _43/viewZ。
        ///          MathCore::Matrix::PerspectiveFov の構成に対応する。
        inline void ExtractProjectionZW(const Matrix4x4& projection, float& outM33, float& outM43)
        {
            outM33 = projection.m[2][2];
            outM43 = projection.m[3][2];
        }
    }
    // =========================================================================
    // Initialize
    // =========================================================================
    bool RayTracingShadowManager::Initialize(
        DirectXCommon* dxCommon,
        DescriptorManager* descriptorManager,
        AccelerationStructureManager* asMgr)
    {
        Logger& log = Logger::GetInstance();

        // 共通基盤へ委譲（ポインタ保持と DXR サポート判定・警告ログまで面倒を見る）
        if (!InitializeBase(dxCommon, descriptorManager, asMgr,
            "RayTracingShadowManager", "RTShadow")) {
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
            .AddSRVTable("gSceneDepth", 1)         // t1: 深度（WorldPosition ターゲット廃止に伴い深度から復元する）
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

        // ルートパラメータ番号はここで 1 回だけ解決する（毎ディスパッチの map 検索を避ける）
        rootParams_.shadowOutput = static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gShadowOutput"));
        rootParams_.scene = static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gScene"));
        rootParams_.sceneDepth = static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gSceneDepth"));
        rootParams_.normalRoughness = static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gNormalRoughness"));
        rootParams_.historyShadow = static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gHistoryShadow"));
        rootParams_.motionVector = static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("gMotionVector"));
        rootParams_.constants = static_cast<UINT>(globalRootSigMgr_.GetRootParameterIndex("ShadowRayConstants"));

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
            // ルートシグネチャ: t0=InputShadow, t1=Normal, t2=SceneDepth, u0=Output, b0=Constants
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
            // DenoiseConstants(8)。以前は invViewProj(16) も積んで 24 だったが、
            // 深度重みを線形ビュー深度へ置き換えたので float2 で足りる（Stage 1）
            rootParams[4].Constants.Num32BitValues = kDenoiseConstantCount;
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
            // t0=RawShadow, t1=Normal, t2=SceneDepth, t3=History, t4=MotionVector, u0=Output, b0=Constants
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
            // TemporalConstants(6→8にパディング)。invViewProj(16) は Stage 1 で不要になった
            rootParams[6].Constants.Num32BitValues = kTemporalConstantCount;
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
        const uint32_t maskSlot = MakeSlotIndex(viewIndex, lightIndex, TextureSlot::Mask);
        if (width == view.width && height == view.height && outputViews_.HasTexture(maskSlot)) {
            return true;  // サイズ変更なし
        }

        view.width = width;
        view.height = height;
        // サイズ変更時は履歴を無効化（古いサイズの履歴を引き継がない）
        view.isHistoryValid = false;

        const std::string suffix = "_v" + std::to_string(viewIndex) + "_l" + std::to_string(lightIndex);

        // 最終シャドウマスク（R32_FLOAT / UAV + SRV）
        RayTracingOutputViewSet::TextureOptions maskOptions{};
        maskOptions.format = DXGI_FORMAT_R32_FLOAT;
        maskOptions.allowUAV = true;
        maskOptions.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (!outputViews_.EnsureTexture(dxCommon_, descriptorManager_, width, height,
            maskSlot, "RayTracingShadowManager", "RTShadow_Mask" + suffix, maskOptions)) {
            return false;
        }

        // テンポラル蓄積用 履歴テクスチャ（UAV なし、COPY_DEST + SRV のみ）
        RayTracingOutputViewSet::TextureOptions historyOptions{};
        historyOptions.format = DXGI_FORMAT_R32_FLOAT;
        historyOptions.allowUAV = false;
        historyOptions.initialState = D3D12_RESOURCE_STATE_COMMON;
        if (!outputViews_.EnsureTexture(dxCommon_, descriptorManager_, width, height,
            MakeSlotIndex(viewIndex, lightIndex, TextureSlot::History),
            "RayTracingShadowManager", "RTShadow_History" + suffix, historyOptions)) {
            return false;
        }

        // RayGen の生出力兼 A-Trous の ping-pong 相手（UAV + SRV）
        if (!outputViews_.EnsureTexture(dxCommon_, descriptorManager_, width, height,
            MakeSlotIndex(viewIndex, lightIndex, TextureSlot::DenoiseTemp),
            "RayTracingShadowManager", "RTShadow_DenoiseTemp" + suffix, maskOptions)) {
            return false;
        }

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

    void RayTracingShadowManager::ResizeAllExisting(UINT width, UINT height)
    {
        if (!isInitialized_ || width == 0 || height == 0) {
            return;
        }

        for (uint32_t viewIndex = 0; viewIndex < kViewCount; ++viewIndex) {
            for (uint32_t lightIndex = 0; lightIndex < kMaxDirectionalLights; ++lightIndex) {
                if (outputViews_.HasTexture(MakeSlotIndex(viewIndex, lightIndex, TextureSlot::Mask))) {
                    EnsureOutputTexture(width, height, viewIndex, lightIndex);
                }
            }
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetShadowSRVHandle(
        ViewID viewId, uint32_t lightIndex) const
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return SlotSRV(static_cast<uint32_t>(viewId), lightIndex, TextureSlot::Mask);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetRawShadowSRVHandle(
        ViewID viewId, uint32_t lightIndex) const
    {
        // RayGen の出力先は DenoiseTemp（デノイズ前の生マスク）
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return SlotSRV(static_cast<uint32_t>(viewId), lightIndex, TextureSlot::DenoiseTemp);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetHistorySRVHandle(
        ViewID viewId, uint32_t lightIndex) const
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return SlotSRV(static_cast<uint32_t>(viewId), lightIndex, TextureSlot::History);
    }

    const RayTracingDispatchInfo& RayTracingShadowManager::GetDispatchInfo(
        ViewID viewId, uint32_t lightIndex) const
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return views_[static_cast<uint32_t>(viewId)][lightIndex].dispatchInfo;
    }

    int RayTracingShadowManager::GetEffectiveAtrousPassCount() const
    {
        // ping-pong の都合で偶数のみ有効（Stage 3 で解消予定）
        const int clamped = std::clamp(settings_.atrousPassCount, 0, kMaxAtrousPassCount);
        return clamped - (clamped % 2);
    }

    float RayTracingShadowManager::ResolveEffectiveRayDistance(const Vector3& lightDirection) const
    {
        if (!settings_.scaleRayDistanceBySunElevation) {
            return settings_.maxRayDistance;
        }

        // lightDirection は「光源 → シーン」向き。真上の太陽なら y = -1。
        // sin(高度) = -normalize(dir).y
        const float lengthSq =
            lightDirection.x * lightDirection.x +
            lightDirection.y * lightDirection.y +
            lightDirection.z * lightDirection.z;
        if (lengthSq <= 1.0e-12f) {
            return settings_.maxRayDistance;
        }

        const float sinElevation = -lightDirection.y / std::sqrt(lengthSq);

        // 光源が地平線より下（sin<=0）なら影を落とす意味が無いので基準値のまま返す。
        // 倍率は kMaxRayDistanceScale で頭打ちにする（地平線近傍での爆発を防ぐ）。
        const float minSin = 1.0f / kMaxRayDistanceScale;
        const float scale = 1.0f / (std::max)(sinElevation, minSin);
        return settings_.maxRayDistance * (std::min)(scale, kMaxRayDistanceScale);
    }

    void RayTracingShadowManager::PrepareDebugViews(ID3D12GraphicsCommandList* cmdList)
    {
        // 要求は 1 フレーム限り。ここで消費してクリアするので、パネルを閉じれば
        // 次フレームからバリアは発行されなくなる。
        if (!debugViewRequested_) {
            return;
        }
        debugViewRequested_ = false;

        if (!isInitialized_ || !cmdList) {
            return;
        }

        // ImGui はピクセルシェーダでサンプルするため PIXEL_SHADER_RESOURCE へ揃える。
        // Mask は各ステージ終了時点で既に PIXEL_SHADER_RESOURCE なので対象外。
        for (uint32_t viewIndex = 0; viewIndex < kViewCount; ++viewIndex) {
            for (uint32_t lightIndex = 0; lightIndex < kMaxDirectionalLights; ++lightIndex) {
                if (!views_[viewIndex][lightIndex].dispatchedThisFrame) {
                    continue;
                }

                ResourceBarrierBatch batch(cmdList);
                if (auto* raw = SlotResource(viewIndex, lightIndex, TextureSlot::DenoiseTemp)) {
                    batch.Add(raw, SlotState(viewIndex, lightIndex, TextureSlot::DenoiseTemp),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
                if (auto* history = SlotResource(viewIndex, lightIndex, TextureSlot::History)) {
                    batch.Add(history, SlotState(viewIndex, lightIndex, TextureSlot::History),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
            }
        }
    }

    ID3D12Resource* RayTracingShadowManager::GetShadowResource(
        ViewID viewId,
        uint32_t lightIndex) const
    {
        // 指定ビュー・ライトの現在のシャドウ出力テクスチャを返す。
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return SlotResource(static_cast<uint32_t>(viewId), lightIndex, TextureSlot::Mask);
    }

    D3D12_RESOURCE_STATES& RayTracingShadowManager::GetShadowCurrentState(
        ViewID viewId,
        uint32_t lightIndex)
    {
        // 自動遷移処理が共有するシャドウ出力の現在状態参照を返す。
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return SlotState(static_cast<uint32_t>(viewId), lightIndex, TextureSlot::Mask);
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
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
        const Vector3& lightDirection,
        const Matrix4x4& invViewProj,
        UINT width, UINT height,
        ViewID viewId,
        uint32_t lightIndex)
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];

        // 診断情報を毎回リセットしてから埋める（失敗経路でも状態が UI に出るようにする）
        const int numSamples = std::clamp(settings_.softShadowSamples, 1, 16);
        const float effectiveRayDistance = ResolveEffectiveRayDistance(lightDirection);
        view.dispatchInfo = {};
        view.dispatchInfo.passName = "RTShadow";
        view.dispatchInfo.viewIndex = vi;
        view.dispatchInfo.slotIndex = lightIndex;
        view.dispatchInfo.width = width;
        view.dispatchInfo.height = height;
        view.dispatchInfo.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;
        view.dispatchInfo.AddExtra("samples/px", static_cast<float>(numSamples));
        view.dispatchInfo.AddExtra("atrousPasses", static_cast<float>(GetEffectiveAtrousPassCount()));
        view.dispatchInfo.AddExtra("lightRadius", settings_.lightRadius);
        view.dispatchInfo.AddExtra("rayDist(実効)", effectiveRayDistance);

        if (!isInitialized_) {
            view.dispatchInfo.status = RayTracingDispatchStatus::NotInitialized;
            return;
        }
        if (!asMgr_ || !asMgr_->IsSupported()) {
            view.dispatchInfo.status = RayTracingDispatchStatus::RayTracingUnsupported;
            return;
        }
        if (asMgr_->GetBLASCount() == 0) {
            view.dispatchInfo.status = RayTracingDispatchStatus::NoBLAS;
            return;
        }

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch START viewId={} lightIndex={} size={}x{} BLAS={}",
                vi, lightIndex, width, height, asMgr_->GetBLASCount());
        }

        // 出力テクスチャの確保（リサイズ対応）
        if (!EnsureOutputTexture(width, height, vi, lightIndex)) {
            view.dispatchInfo.status = RayTracingDispatchStatus::OutputAllocationFailed;
            return;
        }
        view.dispatchInfo.outputSrvPtr = SlotSRV(vi, lightIndex, TextureSlot::Mask).ptr;

        // 定数データを構築（テンポラル蓄積は ApplyTemporal 側で行うので
        // historyAlpha / screenWidth / screenHeight はシェーダー側では未使用）
        ShadowRayConstants constants{};
        constants.lightDir[0] = lightDirection.x;
        constants.lightDir[1] = lightDirection.y;
        constants.lightDir[2] = lightDirection.z;
        constants.shadowBias = settings_.shadowBias;
        constants.maxRayDistance = effectiveRayDistance;
        constants.lightRadius = settings_.lightRadius;
        constants.softShadowSamples = settings_.softShadowSamples;
        constants.frameIndex = frameIndex_++;
        constants.historyAlpha = 1.0f; // RayGen側では使わない
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.invViewProj = invViewProj;

        // CommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
            view.dispatchInfo.status = RayTracingDispatchStatus::CommandList4Unavailable;
            return;
        }

        // パイプライン設定
        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        // RayGen は生バイナリを DenoiseTemp に書き込む（ApplyTemporal が読む）
        ResourceBarrierHelper::Transition(
            cmdList, SlotResource(vi, lightIndex, TextureSlot::DenoiseTemp),
            SlotState(vi, lightIndex, TextureSlot::DenoiseTemp),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // ルートパラメータのバインド（history / motionVector は未使用だがスロット維持のためダミーで埋める）
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.shadowOutput, SlotUAV(vi, lightIndex, TextureSlot::DenoiseTemp));
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.scene, asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.sceneDepth, sceneDepthSRV);
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.normalRoughness, normalRoughnessSRV);
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.historyShadow, SlotSRV(vi, lightIndex, TextureSlot::History));
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.motionVector, motionVectorSRV);

        cmdList->SetComputeRoot32BitConstants(
            rootParams_.constants,
            sizeof(ShadowRayConstants) / sizeof(uint32_t),
            &constants,
            0);

        // DispatchRays: 生バイナリを DenoiseTemp に書き出す
        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        cmdList4->DispatchRays(&dispatchDesc);

        // ApplyTemporal の読み取り前に完了を保証
        ResourceBarrierHelper::UAV(cmdList, SlotResource(vi, lightIndex, TextureSlot::DenoiseTemp));

        // DenoiseTemp を SRV 状態へ
        ResourceBarrierHelper::Transition(
            cmdList, SlotResource(vi, lightIndex, TextureSlot::DenoiseTemp),
            SlotState(vi, lightIndex, TextureSlot::DenoiseTemp),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        view.dispatchedThisFrame = true;
        view.dispatchInfo.status = RayTracingDispatchStatus::Dispatched;
        view.dispatchInfo.rayCount =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height)
            * static_cast<uint64_t>(numSamples);

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch complete viewId={} lightIndex={} srvHandle=0x{:X}",
                vi, lightIndex, SlotSRV(vi, lightIndex, TextureSlot::Mask).ptr);
            ++dispatchLogCount_;
        }
    }

    // =========================================================================
    // A-Trous デノイズ（Dispatch の直後に呼ぶ）
    // =========================================================================
    void RayTracingShadowManager::Denoise(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        const Matrix4x4& projection,
        UINT width, UINT height,
        ViewID viewId,
        uint32_t lightIndex)
    {
        if (!isInitialized_ || !denoiseInitialized_) return;

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);

        // Stage 2c 以降、テクスチャは共通基盤のスロット参照で取るため
        // views_[vi][lightIndex] はこの関数では使わない。

        if (!SlotResource(vi, lightIndex, TextureSlot::Mask)
            || !SlotResource(vi, lightIndex, TextureSlot::DenoiseTemp)) {
            return;
        }

        // À-Trous: ステップ幅 1 → 2 → 4 → 8
        // ping=Mask / pong=DenoiseTemp で交互に入出力
        // パス数は「偶数」にすること:
        //   ping-pong は pass%2==0 で Mask→DenoiseTemp, pass%2==1 で DenoiseTemp→Mask
        //   最後のパスが奇数インデックス（pass = kNumPasses-1 が奇数）のとき Mask に書き込まれる
        //   → kNumPasses が偶数のとき、最終パスインデックス = kNumPasses-1 は奇数 → Mask に書き込み ✓
        static const int   kSteps[kMaxAtrousPassCount] = { 1,    2,    4,    8 };
        static const float kPhiNormal[kMaxAtrousPassCount] = { 4.0f, 8.0f, 16.0f, 32.0f };
        static const float kPhiShadow[kMaxAtrousPassCount] = { 1.0f, 0.7f,  0.5f,  0.35f };
        static_assert(kMaxAtrousPassCount % 2 == 0,
            "kMaxAtrousPassCount must be even so the final result lands in the Mask slot");

        // 設定から実効パス数を決める（0 = デノイズ無効）。
        // 偶数へ切り捨てるのは上記 ping-pong の都合。
        const int kNumPasses = GetEffectiveAtrousPassCount();
        if (kNumPasses == 0) {
            // デノイズを丸ごとスキップする。ApplyTemporal は Mask を
            // PIXEL_SHADER_RESOURCE で残すため、後段（DeferredLighting）はそのまま読める。
            return;
        }

        // Mask は Dispatch 直後に PIXEL_SHADER_RESOURCE 状態になっているので
        // 最初のパスで SRV として読む前にそのまま使える（NON_PIXEL_SHADER_RESOURCE に遷移）
        ResourceBarrierHelper::Transition(cmdList, SlotResource(vi, lightIndex, TextureSlot::Mask),
            SlotState(vi, lightIndex, TextureSlot::Mask), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->SetComputeRootSignature(denoiseRootSignature_.Get());
        cmdList->SetPipelineState(denoisePipelineState_.Get());

        const UINT groupX = (width + 7) / 8;
        const UINT groupY = (height + 7) / 8;

        for (int pass = 0; pass < kNumPasses; ++pass)
        {
            // pass が偶数: texture → denoiseTemp / 奇数: denoiseTemp → texture
            bool pingToTemp = (pass % 2 == 0);

            const TextureSlot inSlot = pingToTemp ? TextureSlot::Mask : TextureSlot::DenoiseTemp;
            const TextureSlot outSlot = pingToTemp ? TextureSlot::DenoiseTemp : TextureSlot::Mask;

            ID3D12Resource* inputRes = SlotResource(vi, lightIndex, inSlot);
            ID3D12Resource* outputRes = SlotResource(vi, lightIndex, outSlot);
            D3D12_RESOURCE_STATES& inputState = SlotState(vi, lightIndex, inSlot);
            D3D12_RESOURCE_STATES& outputState = SlotState(vi, lightIndex, outSlot);
            D3D12_GPU_DESCRIPTOR_HANDLE inputSrv = SlotSRV(vi, lightIndex, inSlot);
            D3D12_GPU_DESCRIPTOR_HANDLE outputUav = SlotUAV(vi, lightIndex, outSlot);

            // 入力: SRV へ、出力: UAV へ（2リソースを一括バリア）
            {
                ResourceBarrierBatch batch(cmdList);
                batch.Add(inputRes, inputState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                batch.Add(outputRes, outputState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }

            // バインド
            cmdList->SetComputeRootDescriptorTable(0, inputSrv);          // t0: InputShadow
            cmdList->SetComputeRootDescriptorTable(1, normalRoughnessSRV); // t1: Normal
            cmdList->SetComputeRootDescriptorTable(2, sceneDepthSRV);      // t2: SceneDepth
            cmdList->SetComputeRootDescriptorTable(3, outputUav);          // u0: Output

            DenoiseConstants dc{};
            dc.stepSize = kSteps[pass];
            dc.phiShadow = kPhiShadow[pass];
            dc.phiNormal = kPhiNormal[pass];
            dc.phiDepth = settings_.denoisePhiDepth;
            dc.screenWidth = static_cast<int>(width);
            dc.screenHeight = static_cast<int>(height);
            ExtractProjectionZW(projection, dc.projM33, dc.projM43);
            cmdList->SetComputeRoot32BitConstants(4, kDenoiseConstantCount, &dc, 0);

            cmdList->Dispatch(groupX, groupY, 1);

            // UAV バリア（次パスの読み取りを保証）
            ResourceBarrierHelper::UAV(cmdList, outputRes);
        }

        // 最終結果は Mask スロット（kNumPasses を偶数へ切り捨てているため）
        {
            ResourceBarrierBatch batch(cmdList);
            batch.Add(SlotResource(vi, lightIndex, TextureSlot::Mask),
                SlotState(vi, lightIndex, TextureSlot::Mask), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Add(SlotResource(vi, lightIndex, TextureSlot::DenoiseTemp),
                SlotState(vi, lightIndex, TextureSlot::DenoiseTemp), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // 履歴へのコピーは ApplyTemporal で行う（A-Trous 出力は履歴に書かない）
    }

    // =========================================================================
    // テンポラル蓄積パス（RayGenの直後、Denoiseの前に呼び出す）
    // =========================================================================
    void RayTracingShadowManager::ApplyTemporal(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
        const Matrix4x4& projection,
        UINT width, UINT height,
        ViewID viewId,
        uint32_t lightIndex)
    {
        if (!isInitialized_ || !temporalInitialized_) return;

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];

        ID3D12Resource* maskRes = SlotResource(vi, lightIndex, TextureSlot::Mask);
        ID3D12Resource* rawRes = SlotResource(vi, lightIndex, TextureSlot::DenoiseTemp);
        ID3D12Resource* historyRes = SlotResource(vi, lightIndex, TextureSlot::History);
        if (!maskRes || !rawRes || !historyRes) return;

        // 入出力の状態遷移（3リソースを一括バリア）
        {
            ResourceBarrierBatch batch(cmdList);
            batch.Add(rawRes, SlotState(vi, lightIndex, TextureSlot::DenoiseTemp),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Add(maskRes, SlotState(vi, lightIndex, TextureSlot::Mask),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            batch.Add(historyRes, SlotState(vi, lightIndex, TextureSlot::History),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        cmdList->SetComputeRootSignature(temporalRootSignature_.Get());
        cmdList->SetPipelineState(temporalPipelineState_.Get());

        // t0=RawShadow, t1=Normal, t2=SceneDepth, t3=History, t4=MotionVector, u0=Output
        cmdList->SetComputeRootDescriptorTable(0, SlotSRV(vi, lightIndex, TextureSlot::DenoiseTemp));
        cmdList->SetComputeRootDescriptorTable(1, normalRoughnessSRV);
        cmdList->SetComputeRootDescriptorTable(2, sceneDepthSRV);
        cmdList->SetComputeRootDescriptorTable(3, SlotSRV(vi, lightIndex, TextureSlot::History));
        cmdList->SetComputeRootDescriptorTable(4, motionVectorSRV);
        cmdList->SetComputeRootDescriptorTable(5, SlotUAV(vi, lightIndex, TextureSlot::Mask));

        // ── 定数 ──
        TemporalConstants tc{};
        tc.screenWidth = static_cast<int>(width);
        tc.screenHeight = static_cast<int>(height);
        tc.historyAlpha = settings_.historyAlpha;
        // 初回フレーム（履歴未生成）か、デバッグトグルで明示的に無効化された場合は履歴を使わない
        tc.disableHistory = (view.isHistoryValid && !settings_.disableHistory) ? 0.0f : 1.0f;
        ExtractProjectionZW(projection, tc.projM33, tc.projM43);
        cmdList->SetComputeRoot32BitConstants(6, kTemporalConstantCount, &tc, 0);

        const UINT groupX = (width + 7) / 8;
        const UINT groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);

        ResourceBarrierHelper::UAV(cmdList, maskRes);

        // テンポラル結果を履歴にコピー（次フレームの参照用）
        {
            ResourceBarrierBatch batch(cmdList);
            batch.Add(maskRes, SlotState(vi, lightIndex, TextureSlot::Mask),
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            batch.Add(historyRes, SlotState(vi, lightIndex, TextureSlot::History),
                D3D12_RESOURCE_STATE_COPY_DEST);
        }

        cmdList->CopyResource(historyRes, maskRes);

        // 状態を後段（A-Trous）向けに整える
        {
            ResourceBarrierBatch batch(cmdList);
            batch.Add(historyRes, SlotState(vi, lightIndex, TextureSlot::History),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Add(maskRes, SlotState(vi, lightIndex, TextureSlot::Mask),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }

        view.isHistoryValid = true;
    }
}
