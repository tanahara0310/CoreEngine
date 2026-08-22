#include "pch.h"
#include "RayTracingShadowManager.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "AccelerationStructureManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorManager.h"
#include "Graphics/RHI/Barrier/ResourceBarrierHelper.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Utility/Logger/Logger.h"
#include "Utility/CVar/CVar.h"

#include <d3d12.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        // RT シャドウのチューニング値。既定値の根拠は RayTracingShadowSettings のコメントを参照
        CVar<int> cvSoftShadowSamples{
            "r.RTShadow.SoftShadowSamples", 1,
            "1 ピクセルあたりのシャドウレイ本数。コストはほぼ本数に比例する（1spp = 約1.14ms @1920x1080）。"
            "A-Trous デノイザーで補完するため 1 で十分",
            CVarRange{ 1.0f, 16.0f } };

        CVar<float> cvLightRadius{
            "r.RTShadow.LightRadius", 0.02f,
            "光源の角半径 [rad]。ペナンブラ幅を決める。実際の太陽は約 0.0046。"
            "0.15 以上はペナンブラが広すぎてゴーストが出るため非推奨",
            CVarRange{ 0.0f, 0.2f } };

        CVar<float> cvShadowBias{
            "r.RTShadow.ShadowBias", 0.05f,
            "セルフシャドウ防止バイアス",
            CVarRange{ 0.0f, 0.5f } };

        CVar<float> cvMaxRayDistance{
            "r.RTShadow.MaxRayDistance", 1000.0f,
            "シャドウレイの基準射程距離。これより遠い遮蔽物は影を落とさない",
            CVarRange{ 10.0f, 20000.0f } };

        CVar<bool> cvScaleRayDistanceBySunElevation{
            "r.RTShadow.ScaleRayDistanceBySunElevation", true,
            "射程を太陽高度でスケールする。光源が低いほどレイは水平に走るため、"
            "固定距離だと遠くの遮蔽物へ届かない（有効時は 基準距離/sin(高度)、最大10倍）" };

        CVar<int> cvMaxHistoryFrames{
            "r.RTShadow.MaxHistoryFrames", 32,
            "テンポラル蓄積フレーム数の上限。ピクセルごとの蓄積カウント N で α=1/N の適応ブレンドを行い、"
            "静止時は 1/この値 まで収束する（旧 HistoryAlpha の固定 α は定常ノイズが残るため廃止）。"
            "大きいほど滑らかだが、影の変化への追従はクランプ棄却頼みになる",
            CVarRange{ 1.0f, 255.0f } };

        CVar<int> cvAtrousPassCount{
            "r.RTShadow.AtrousPassCount", 2,
            "A-Trous デノイズのパス数。0 = デノイズ無効。1 パスごとにステップ幅が 1→2→4→8 と倍になる。"
            "1spp のシャドウに step=8（実効31x31）まで広げる必要は無い",
            CVarRange{ 0.0f, 4.0f } };

        CVar<float> cvDenoisePhiDepth{
            "r.RTShadow.DenoisePhiDepth", 1.0f,
            "A-Trous / テンポラルの深度エッジ重み。大きいほどエッジ検出が厳しく影の輪郭が保たれる"
            "（ぼけにくい）。小さいほど深度差を跨いで広くぼける",
            CVarRange{ 0.05f, 8.0f } };

        CVar<bool> cvHalfResolutionTrace{
            "r.RTShadow.HalfResolutionTrace", true,
            "トレース〜デノイズを 1/2 解像度で行い、最後にバイラテラルアップサンプルする。"
            "レイ本数もデノイズ帯域も 1/4 になる（最終マスクはフル解像度のまま）" };

        CVar<float> cvUpsamplePhiDepth{
            "r.RTShadow.UpsamplePhiDepth", 8.0f,
            "アップサンプル時の深度エッジ重み（ハーフ解像度時のみ有効）。"
            "大きいほど深度が近い結果しか採用しない（輪郭はシャープだがエイリアスが出る）",
            CVarRange{ 0.5f, 40.0f } };

        // 残像・ゴーストの切り分け用トグル。ON のまま保存されると品質低下に気づけないため保存しない
        CVar<bool> cvDisableHistory{
            "r.RTShadow.DisableHistory", false,
            "テンポラル蓄積の履歴参照を強制的に無効化する（デバッグ用）。"
            "ON にすると毎フレーム現フレームの空間前処理結果のみを使う",
            CVarRange{}, CVarFlags::NoSave };
    }

    void RayTracingShadowManager::SyncSettingsFromCVars()
    {
        settings_.softShadowSamples = cvSoftShadowSamples.Get();
        settings_.lightRadius = cvLightRadius.Get();
        settings_.shadowBias = cvShadowBias.Get();
        settings_.maxRayDistance = cvMaxRayDistance.Get();
        settings_.scaleRayDistanceBySunElevation = cvScaleRayDistanceBySunElevation.Get();
        settings_.maxHistoryFrames = cvMaxHistoryFrames.Get();
        settings_.atrousPassCount = cvAtrousPassCount.Get();
        settings_.denoisePhiDepth = cvDenoisePhiDepth.Get();
        settings_.halfResolutionTrace = cvHalfResolutionTrace.Get();
        settings_.upsamplePhiDepth = cvUpsamplePhiDepth.Get();
        settings_.disableHistory = cvDisableHistory.Get();
    }

    void RayTracingShadowManager::SetSettings(const RayTracingShadowSettings& settings)
    {
        // CVar が唯一の保持者。書き戻すことで UI・自動保存にも反映される
        cvSoftShadowSamples.Set(settings.softShadowSamples);
        cvLightRadius.Set(settings.lightRadius);
        cvShadowBias.Set(settings.shadowBias);
        cvMaxRayDistance.Set(settings.maxRayDistance);
        cvScaleRayDistanceBySunElevation.Set(settings.scaleRayDistanceBySunElevation);
        cvMaxHistoryFrames.Set(settings.maxHistoryFrames);
        cvAtrousPassCount.Set(settings.atrousPassCount);
        cvDenoisePhiDepth.Set(settings.denoisePhiDepth);
        cvHalfResolutionTrace.Set(settings.halfResolutionTrace);
        cvUpsamplePhiDepth.Set(settings.upsamplePhiDepth);
        cvDisableHistory.Set(settings.disableHistory);
        SyncSettingsFromCVars();
    }
    // =========================================================================
    // HLSL 側の cbuffer とレイアウトを共有する構造体
    // どれもスカラーのみで構成する。float2/float3 や配列を使うと 16 バイト境界への
    // 切り上げが入り、C++ 側とオフセットが食い違う（影が全消失した原因）。
    // =========================================================================

    /// @brief RayGen（RTShadow.hlsl）の ShadowRayConstants
    struct alignas(16) ShadowRayConstants {
        float    lightDir[3];        // offset  0
        float    shadowBias;         // offset 12  → row1 終了(16)
        float    maxRayDistance;     // offset 16
        float    lightRadius;        // offset 20
        int      softShadowSamples;  // offset 24
        uint32_t frameIndex;         // offset 28  → row2 終了(32)
        float    screenWidth;        // offset 32  フル解像度（UV 計算に使う）
        float    screenHeight;       // offset 36
        int      traceOffsetX;       // offset 40  ハーフ解像度時の 2x2 サンプル位置
        int      traceOffsetY;       // offset 44  → row3 終了(48)
        int      traceScale;         // offset 48  1 = フル / 2 = ハーフ
        int      pad0;               // offset 52
        int      pad1;               // offset 56
        int      pad2;               // offset 60  → row4 終了(64)
        Matrix4x4 invViewProj;       // offset 64  深度復元用 → 128
    };
    static_assert(sizeof(ShadowRayConstants) == 128, "ShadowRayConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kShadowRayConstantsFields[] = {
        CB_FIELD(ShadowRayConstants, lightDir), CB_FIELD(ShadowRayConstants, shadowBias),
        CB_FIELD(ShadowRayConstants, maxRayDistance), CB_FIELD(ShadowRayConstants, lightRadius),
        CB_FIELD(ShadowRayConstants, softShadowSamples), CB_FIELD(ShadowRayConstants, frameIndex),
        CB_FIELD(ShadowRayConstants, screenWidth), CB_FIELD(ShadowRayConstants, screenHeight),
        CB_FIELD(ShadowRayConstants, traceOffsetX), CB_FIELD(ShadowRayConstants, traceOffsetY),
        CB_FIELD(ShadowRayConstants, traceScale), CB_FIELD(ShadowRayConstants, pad0),
        CB_FIELD(ShadowRayConstants, pad1), CB_FIELD(ShadowRayConstants, pad2),
        CB_FIELD(ShadowRayConstants, invViewProj),
    };
    CB_VERIFY_LAYOUT(ShadowRayConstants, kShadowRayConstantsFields);
    CB_BIND_HLSL(ShadowRayConstants, kShadowRayConstantsFields, "ShadowRayConstants");
    static constexpr UINT kShadowRayConstantCount = sizeof(ShadowRayConstants) / sizeof(uint32_t);

    /// @brief A-Trous デノイズ（RTShadowDenoise.hlsl）の DenoiseConstants
    struct DenoiseConstants {
        int   stepSize;      // offset  0
        float phiShadow;     // offset  4
        float phiNormal;     // offset  8
        float phiDepth;      // offset 12 → row1 終了(16)
        int   traceWidth;    // offset 16
        int   traceHeight;   // offset 20
        float projM33;       // offset 24  proj._33 = far/(far-near)
        float projM43;       // offset 28  proj._43 = -near*far/(far-near) → row2 終了(32)
        int   traceScale;    // offset 32
        int   traceOffsetX;  // offset 36
        int   traceOffsetY;  // offset 40
        int   fullWidth;     // offset 44 → row3 終了(48)
        int   fullHeight;    // offset 48
        int   pad0;          // offset 52
        int   pad1;          // offset 56
        int   pad2;          // offset 60 → row4 終了(64)
    };
    static_assert(sizeof(DenoiseConstants) == 64, "DenoiseConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kDenoiseConstantsFields[] = {
        CB_FIELD(DenoiseConstants, stepSize), CB_FIELD(DenoiseConstants, phiShadow),
        CB_FIELD(DenoiseConstants, phiNormal), CB_FIELD(DenoiseConstants, phiDepth),
        CB_FIELD(DenoiseConstants, traceWidth), CB_FIELD(DenoiseConstants, traceHeight),
        CB_FIELD(DenoiseConstants, projM33), CB_FIELD(DenoiseConstants, projM43),
        CB_FIELD(DenoiseConstants, traceScale), CB_FIELD(DenoiseConstants, traceOffsetX),
        CB_FIELD(DenoiseConstants, traceOffsetY), CB_FIELD(DenoiseConstants, fullWidth),
        CB_FIELD(DenoiseConstants, fullHeight), CB_FIELD(DenoiseConstants, pad0), CB_FIELD(DenoiseConstants, pad1),
        CB_FIELD(DenoiseConstants, pad2),
    };
    CB_VERIFY_LAYOUT(DenoiseConstants, kDenoiseConstantsFields);
    CB_BIND_HLSL(DenoiseConstants, kDenoiseConstantsFields, "DenoiseConstants");
    static constexpr UINT kDenoiseConstantCount = sizeof(DenoiseConstants) / sizeof(uint32_t);

    /// @brief テンポラル蓄積（RTShadowTemporal.CS.hlsl）の TemporalConstants
    struct TemporalConstants {
        int   traceWidth;     // offset  0
        int   traceHeight;    // offset  4
        float maxHistoryFrames; // offset 8  適応ブレンドの蓄積上限（α の下限 = 1/この値）
        float disableHistory; // offset 12 → row1 終了(16)
        float projM33;        // offset 16
        float projM43;        // offset 20
        int   traceScale;     // offset 24
        int   traceOffsetX;   // offset 28 → row2 終了(32)
        int   traceOffsetY;   // offset 32
        int   fullWidth;      // offset 36
        int   fullHeight;     // offset 40
        int   pad0;           // offset 44 → row3 終了(48)
    };
    static_assert(sizeof(TemporalConstants) == 48, "TemporalConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kTemporalConstantsFields[] = {
        CB_FIELD(TemporalConstants, traceWidth), CB_FIELD(TemporalConstants, traceHeight),
        CB_FIELD(TemporalConstants, maxHistoryFrames), CB_FIELD(TemporalConstants, disableHistory),
        CB_FIELD(TemporalConstants, projM33), CB_FIELD(TemporalConstants, projM43),
        CB_FIELD(TemporalConstants, traceScale), CB_FIELD(TemporalConstants, traceOffsetX),
        CB_FIELD(TemporalConstants, traceOffsetY), CB_FIELD(TemporalConstants, fullWidth),
        CB_FIELD(TemporalConstants, fullHeight), CB_FIELD(TemporalConstants, pad0),
    };
    CB_VERIFY_LAYOUT(TemporalConstants, kTemporalConstantsFields);
    CB_BIND_HLSL(TemporalConstants, kTemporalConstantsFields, "TemporalConstants");
    static constexpr UINT kTemporalConstantCount = sizeof(TemporalConstants) / sizeof(uint32_t);

    /// @brief 解決＝バイラテラルアップサンプル（RTShadowResolve.CS.hlsl）の ResolveConstants
    struct ResolveConstants {
        int   fullWidth;      // offset  0
        int   fullHeight;     // offset  4
        int   traceWidth;     // offset  8
        int   traceHeight;    // offset 12 → row1 終了(16)
        float projM33;        // offset 16
        float projM43;        // offset 20
        int   traceScale;     // offset 24
        int   traceOffsetX;   // offset 28 → row2 終了(32)
        int   traceOffsetY;   // offset 32
        float phiDepth;       // offset 36
        int   pad0;           // offset 40
        int   pad1;           // offset 44 → row3 終了(48)
    };
    static_assert(sizeof(ResolveConstants) == 48, "ResolveConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kResolveConstantsFields[] = {
        CB_FIELD(ResolveConstants, fullWidth), CB_FIELD(ResolveConstants, fullHeight),
        CB_FIELD(ResolveConstants, traceWidth), CB_FIELD(ResolveConstants, traceHeight),
        CB_FIELD(ResolveConstants, projM33), CB_FIELD(ResolveConstants, projM43),
        CB_FIELD(ResolveConstants, traceScale), CB_FIELD(ResolveConstants, traceOffsetX),
        CB_FIELD(ResolveConstants, traceOffsetY), CB_FIELD(ResolveConstants, phiDepth),
        CB_FIELD(ResolveConstants, pad0), CB_FIELD(ResolveConstants, pad1),
    };
    CB_VERIFY_LAYOUT(ResolveConstants, kResolveConstantsFields);
    CB_BIND_HLSL(ResolveConstants, kResolveConstantsFields, "ResolveConstants");
    static constexpr UINT kResolveConstantCount = sizeof(ResolveConstants) / sizeof(uint32_t);

    namespace {
        /// @brief 投影行列から線形深度復元用の 2 要素を取り出す
        /// @details 行ベクトル規約の透視投影では ndcZ = _33 + _43/viewZ。
        ///          MathCore::Matrix::PerspectiveFov の構成に対応する。
        inline void ExtractProjectionZW(const Matrix4x4& projection, float& outM33, float& outM43)
        {
            outM33 = projection.m[2][2];
            outM43 = projection.m[3][2];
        }

        /// @brief 8x8 スレッドグループでの必要グループ数
        inline UINT DispatchGroupCount(UINT pixels) { return (pixels + 7) / 8; }

        /// @brief ハーフ解像度時に巡回させる 2x2 サンプル位置
        /// @details 固定オフセットだと常に同じサブピクセルしか見ないため、
        ///          細い遮蔽物が原理的に落ちる。4 フレームで全サブピクセルを踏み、
        ///          テンポラル蓄積で平均させる。
        constexpr UINT kTraceOffsetTable[4][2] = { {0, 0}, {1, 1}, {1, 0}, {0, 1} };
    }

    // =========================================================================
    // コンピュートパス（RS + PSO）の共通構築
    // デノイズ / テンポラル / 解決の 3 本が全く同じ形なのでまとめてある。
    // ルートパラメータ番号: t0..t(n-1) = 0..n-1, u0 = n, b0 = n+1
    // =========================================================================
    bool RayTracingShadowManager::CreateComputePass(
        const wchar_t* shaderPath,
        UINT srvCount,
        UINT constantDwordCount,
        const char* debugLabel,
        Microsoft::WRL::ComPtr<ID3D12RootSignature>& outRootSignature,
        Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPipelineState)
    {
        Logger& log = Logger::GetInstance();

        // ディスクリプタレンジは D3D12SerializeRootSignature が読むまで生存させる必要がある
        std::vector<D3D12_DESCRIPTOR_RANGE> ranges(srvCount + 1);
        for (UINT i = 0; i < srvCount; ++i) {
            ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[i].NumDescriptors = 1;
            ranges[i].BaseShaderRegister = i;
        }
        ranges[srvCount].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[srvCount].NumDescriptors = 1;
        ranges[srvCount].BaseShaderRegister = 0;

        std::vector<D3D12_ROOT_PARAMETER> rootParams(srvCount + 2);
        for (UINT i = 0; i <= srvCount; ++i) {
            rootParams[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParams[i].DescriptorTable.NumDescriptorRanges = 1;
            rootParams[i].DescriptorTable.pDescriptorRanges = &ranges[i];
            rootParams[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        rootParams[srvCount + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[srvCount + 1].Constants.ShaderRegister = 0;
        rootParams[srvCount + 1].Constants.RegisterSpace = 0;
        rootParams[srvCount + 1].Constants.Num32BitValues = constantDwordCount;
        rootParams[srvCount + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = static_cast<UINT>(rootParams.size());
        rsDesc.pParameters = rootParams.data();
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3DBlob> rsBlob, rsError;
        if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsError))) {
            log.Logf(LogLevel::Error, LogCategory::Graphics,
                "RayTracingShadowManager: {} root signature serialize failed", debugLabel);
            return false;
        }
        if (FAILED(dxCommon_->GetDevice()->CreateRootSignature(0, rsBlob->GetBufferPointer(),
            rsBlob->GetBufferSize(), IID_PPV_ARGS(&outRootSignature)))) {
            log.Logf(LogLevel::Error, LogCategory::Graphics,
                "RayTracingShadowManager: {} root signature creation failed", debugLabel);
            return false;
        }

        ShaderCompiler compiler;
        compiler.Initialize();
        Microsoft::WRL::ComPtr<IDxcBlob> csBlob;
        csBlob.Attach(compiler.CompileShader(shaderPath, L"cs_6_6"));
        if (!csBlob || csBlob->GetBufferSize() == 0) {
            log.Logf(LogLevel::Warn, LogCategory::Graphics,
                "RayTracingShadowManager: {} shader compile failed", debugLabel);
            return false;
        }

        outPipelineState = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), outRootSignature.Get(), csBlob.Get(),
            std::string("RTShadow_") + debugLabel);
        if (!outPipelineState) {
            return false;
        }

        log.Logf(LogLevel::Info, LogCategory::Graphics,
            "RayTracingShadowManager: {} pipeline created", debugLabel);
        return true;
    }

    // =========================================================================
    // Initialize
    // =========================================================================
    bool RayTracingShadowManager::Initialize(
        GraphicsCore* dxCommon,
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
        // 履歴・モーションベクターは RayGen では読まない（テンポラルパスの仕事）ので張らない。
        globalRootSigMgr_
            .AddUAVTable("gShadowOutput", 0)      // u0: シャドウ出力（トレース解像度）
            .AddSRVTable("gScene", 0)              // t0: TLAS
            .AddSRVTable("gSceneDepth", 1)         // t1: 深度（ワールド座標復元用）
            .AddSRVTable("gNormalRoughness", 2)    // t2: G-Buffer 法線（セルフシャドウバイアス用）
            .AddRootConstants("ShadowRayConstants", 0, kShadowRayConstantCount); // b0
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

        // 後段の 3 本はどれも「SRV テーブル n 個 + UAV 1 個 + ルート定数」なので共通構築
        denoiseInitialized_ = CreateComputePass(
            L"RTShadowDenoise.hlsl", 3, kDenoiseConstantCount, "A-Trous denoise",
            denoiseRootSignature_, denoisePipelineState_);
        temporalInitialized_ = CreateComputePass(
            L"RTShadowTemporal.CS.hlsl", 5, kTemporalConstantCount, "Temporal accumulation",
            temporalRootSignature_, temporalPipelineState_);
        resolveInitialized_ = CreateComputePass(
            L"RTShadowResolve.CS.hlsl", 3, kResolveConstantCount, "Bilateral resolve",
            resolveRootSignature_, resolvePipelineState_);

        log.Log("RayTracingShadowManager: Initialized successfully",
            LogLevel::Info, LogCategory::Graphics);
        return true;
    }

    // =========================================================================
    // 出力テクスチャの確保（リサイズ対応、ビュー × ライトごと）
    //   Mask だけがフル解像度、それ以外はトレース解像度。
    // =========================================================================
    bool RayTracingShadowManager::EnsureOutputTexture(
        UINT width, UINT height, UINT traceWidth, UINT traceHeight, UINT traceScale,
        uint32_t viewIndex, uint32_t lightIndex)
    {
        auto& view = views_[viewIndex][lightIndex];
        const uint32_t maskSlot = MakeSlotIndex(viewIndex, lightIndex, TextureSlot::Mask);
        if (width == view.width && height == view.height
            && traceWidth == view.traceWidth && traceHeight == view.traceHeight
            && outputViews_.HasTexture(maskSlot)) {
            return true;  // サイズ変更なし
        }

        view.width = width;
        view.height = height;
        view.traceWidth = traceWidth;
        view.traceHeight = traceHeight;
        view.traceScale = traceScale;
        // サイズ変更時は履歴を無効化（古いサイズの履歴を引き継がない）
        view.isHistoryValid = false;

        const std::string suffix = "_v" + std::to_string(viewIndex) + "_l" + std::to_string(lightIndex);

        RayTracingOutputViewSet::TextureOptions options{};
        options.format = kShadowTextureFormat;
        options.allowUAV = true;
        options.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        // 最終シャドウマスク（フル解像度。DeferredLighting がフル解像度座標で Load する）
        if (!outputViews_.EnsureTexture(dxCommon_, descriptorManager_, width, height,
            maskSlot, "RayTracingShadowManager", "RTShadow_Mask" + suffix, options)) {
            return false;
        }

        // 残りはすべてトレース解像度
        // 履歴だけは R8G8（R=シャドウ値, G=蓄積フレーム数 N/255）。
        // 適応ブレンド α=1/N のカウントを持ち歩くため（詳細は RTShadowTemporal.CS.hlsl）。
        struct TraceSlot { TextureSlot slot; const char* name; DXGI_FORMAT format; };
        static constexpr TraceSlot kTraceSlots[] = {
            { TextureSlot::Raw,      "RTShadow_Raw",      kShadowTextureFormat },
            { TextureSlot::DenoiseA, "RTShadow_DenoiseA", kShadowTextureFormat },
            { TextureSlot::DenoiseB, "RTShadow_DenoiseB", kShadowTextureFormat },
            { TextureSlot::HistoryA, "RTShadow_HistoryA", kShadowHistoryFormat },
            { TextureSlot::HistoryB, "RTShadow_HistoryB", kShadowHistoryFormat },
        };
        for (const TraceSlot& traceSlot : kTraceSlots) {
            options.format = traceSlot.format;
            if (!outputViews_.EnsureTexture(dxCommon_, descriptorManager_, traceWidth, traceHeight,
                MakeSlotIndex(viewIndex, lightIndex, traceSlot.slot),
                "RayTracingShadowManager", traceSlot.name + suffix, options)) {
                return false;
            }
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "RayTracingShadowManager: Output texture[view={} light={}] created (mask {}x{} / trace {}x{})",
            viewIndex, lightIndex, width, height, traceWidth, traceHeight);

        return true;
    }

    // =========================================================================
    // アクセサ
    // =========================================================================
    UINT RayTracingShadowManager::GetTraceScale() const
    {
        return settings_.halfResolutionTrace ? 2u : 1u;
    }

    void RayTracingShadowManager::Resize(UINT width, UINT height, ViewID viewId, uint32_t lightIndex)
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const UINT scale = GetTraceScale();
        EnsureOutputTexture(width, height,
            (width + scale - 1) / scale, (height + scale - 1) / scale, scale,
            static_cast<uint32_t>(viewId), lightIndex);
    }

    void RayTracingShadowManager::ResizeAllExisting(UINT width, UINT height)
    {
        if (!isInitialized_ || width == 0 || height == 0) {
            return;
        }

        const UINT scale = GetTraceScale();
        const UINT traceWidth = (width + scale - 1) / scale;
        const UINT traceHeight = (height + scale - 1) / scale;

        for (uint32_t viewIndex = 0; viewIndex < kViewCount; ++viewIndex) {
            for (uint32_t lightIndex = 0; lightIndex < kMaxDirectionalLights; ++lightIndex) {
                if (outputViews_.HasTexture(MakeSlotIndex(viewIndex, lightIndex, TextureSlot::Mask))) {
                    EnsureOutputTexture(width, height, traceWidth, traceHeight, scale, viewIndex, lightIndex);
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
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return SlotSRV(static_cast<uint32_t>(viewId), lightIndex, TextureSlot::Raw);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetHistorySRVHandle(
        ViewID viewId, uint32_t lightIndex) const
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        return SlotSRV(vi, lightIndex, views_[vi][lightIndex].CurrentHistorySlot());
    }

    const RayTracingDispatchInfo& RayTracingShadowManager::GetDispatchInfo(
        ViewID viewId, uint32_t lightIndex) const
    {
        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        return views_[static_cast<uint32_t>(viewId)][lightIndex].dispatchInfo;
    }

    int RayTracingShadowManager::GetEffectiveAtrousPassCount() const
    {
        // Stage 3 で ping/pong 専用スクラッチを持たせたため偶数制約は無い
        return std::clamp(settings_.atrousPassCount, 0, kMaxAtrousPassCount);
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
                const ShadowView& view = views_[viewIndex][lightIndex];
                if (!view.dispatchedThisFrame) {
                    continue;
                }

                const TextureSlot debugSlots[] = { TextureSlot::Raw, view.CurrentHistorySlot() };
                ResourceBarrierBatch batch(cmdList);
                for (TextureSlot slot : debugSlots) {
                    if (auto* resource = SlotResource(viewIndex, lightIndex, slot)) {
                        batch.Add(resource, SlotState(viewIndex, lightIndex, slot),
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                    }
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
    //   RayGen はトレース解像度で Raw スロットへ書き出す。
    // =========================================================================
    void RayTracingShadowManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        const Vector3& lightDirection,
        const Matrix4x4& invViewProj,
        UINT width, UINT height,
        ViewID viewId,
        uint32_t lightIndex)
    {
        // 設定は CVar が保持する。UI・設定復元・コンソールのどの経路で変わってもここで取り込む
        SyncSettingsFromCVars();

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];

        const UINT traceScale = GetTraceScale();
        const UINT traceWidth = (width + traceScale - 1) / traceScale;
        const UINT traceHeight = (height + traceScale - 1) / traceScale;

        // 診断情報を毎回リセットしてから埋める（失敗経路でも状態が UI に出るようにする）
        const int numSamples = std::clamp(settings_.softShadowSamples, 1, 16);
        const float effectiveRayDistance = ResolveEffectiveRayDistance(lightDirection);
        view.dispatchInfo = {};
        view.dispatchInfo.passName = "RTShadow";
        view.dispatchInfo.viewIndex = vi;
        view.dispatchInfo.slotIndex = lightIndex;
        view.dispatchInfo.width = traceWidth;
        view.dispatchInfo.height = traceHeight;
        view.dispatchInfo.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;
        view.dispatchInfo.AddExtra("traceScale", static_cast<float>(traceScale));
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
                "RTShadow::Dispatch START viewId={} lightIndex={} size={}x{} trace={}x{} BLAS={} "
                "samples={} rayDist={:.1f} lightDir=({:.3f},{:.3f},{:.3f})",
                vi, lightIndex, width, height, traceWidth, traceHeight, asMgr_->GetBLASCount(),
                numSamples, effectiveRayDistance,
                lightDirection.x, lightDirection.y, lightDirection.z);
        }

        // 出力テクスチャの確保（リサイズ・解像度モード変更に対応）
        if (!EnsureOutputTexture(width, height, traceWidth, traceHeight, traceScale, vi, lightIndex)) {
            view.dispatchInfo.status = RayTracingDispatchStatus::OutputAllocationFailed;
            return;
        }
        view.dispatchInfo.outputSrvPtr = SlotSRV(vi, lightIndex, TextureSlot::Mask).ptr;

        // フレームを 1 つ進める（履歴の書き込み先を入れ替え、サンプル位置を巡回させる）
        // カウンタは view × light ごと（共有だと 2x2 巡回が欠ける。ShadowView 側コメント参照）
        view.historyParity ^= 1u;
        if (traceScale > 1) {
            const UINT phase = view.frameCount & 3u;
            view.traceOffsetX = kTraceOffsetTable[phase][0];
            view.traceOffsetY = kTraceOffsetTable[phase][1];
        } else {
            view.traceOffsetX = 0;
            view.traceOffsetY = 0;
        }

        // 定数データを構築
        ShadowRayConstants constants{};
        constants.lightDir[0] = lightDirection.x;
        constants.lightDir[1] = lightDirection.y;
        constants.lightDir[2] = lightDirection.z;
        constants.shadowBias = settings_.shadowBias;
        constants.maxRayDistance = effectiveRayDistance;
        constants.lightRadius = settings_.lightRadius;
        constants.softShadowSamples = numSamples;
        constants.frameIndex = view.frameCount++;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.traceOffsetX = static_cast<int>(view.traceOffsetX);
        constants.traceOffsetY = static_cast<int>(view.traceOffsetY);
        constants.traceScale = static_cast<int>(traceScale);
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

        ResourceBarrierHelper::Transition(
            cmdList, SlotResource(vi, lightIndex, TextureSlot::Raw),
            SlotState(vi, lightIndex, TextureSlot::Raw),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetComputeRootDescriptorTable(
            rootParams_.shadowOutput, SlotUAV(vi, lightIndex, TextureSlot::Raw));
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.scene, asMgr_->GetTLASSRVHandle());
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.sceneDepth, sceneDepthSRV);
        cmdList->SetComputeRootDescriptorTable(
            rootParams_.normalRoughness, normalRoughnessSRV);

        cmdList->SetComputeRoot32BitConstants(
            rootParams_.constants, kShadowRayConstantCount, &constants, 0);

        // DispatchRays はトレース解像度で行う（ハーフ解像度ならレイ本数は 1/4）
        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(traceWidth, traceHeight);
        cmdList4->DispatchRays(&dispatchDesc);

        // ApplyTemporal の読み取り前に完了を保証してから SRV 状態へ
        ResourceBarrierHelper::UAV(cmdList, SlotResource(vi, lightIndex, TextureSlot::Raw));
        ResourceBarrierHelper::Transition(
            cmdList, SlotResource(vi, lightIndex, TextureSlot::Raw),
            SlotState(vi, lightIndex, TextureSlot::Raw),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        view.dispatchedThisFrame = true;
        view.dispatchInfo.status = RayTracingDispatchStatus::Dispatched;
        view.dispatchInfo.rayCount =
            static_cast<uint64_t>(traceWidth) * static_cast<uint64_t>(traceHeight)
            * static_cast<uint64_t>(numSamples);

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch complete viewId={} lightIndex={} srvHandle=0x{:X}",
                vi, lightIndex, SlotSRV(vi, lightIndex, TextureSlot::Mask).ptr);
            ++dispatchLogCount_;
        }
    }

    // =========================================================================
    // テンポラル蓄積パス（RayGen の直後、Denoise の前に呼び出す）
    //   Raw + 前フレーム履歴 → 今フレーム履歴（履歴は 2 枚の ping-pong）。
    // =========================================================================
    void RayTracingShadowManager::ApplyTemporal(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE motionVectorSRV,
        const Matrix4x4& projection,
        ViewID viewId,
        uint32_t lightIndex)
    {
        if (!isInitialized_ || !temporalInitialized_) return;

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];
        if (!view.dispatchedThisFrame) return;

        const TextureSlot currentHistory = view.CurrentHistorySlot();
        const TextureSlot previousHistory = view.PreviousHistorySlot();

        ID3D12Resource* rawRes = SlotResource(vi, lightIndex, TextureSlot::Raw);
        ID3D12Resource* prevRes = SlotResource(vi, lightIndex, previousHistory);
        ID3D12Resource* curRes = SlotResource(vi, lightIndex, currentHistory);
        if (!rawRes || !prevRes || !curRes) return;

        // 入出力の状態遷移（3リソースを一括バリア）
        {
            ResourceBarrierBatch batch(cmdList);
            batch.Add(rawRes, SlotState(vi, lightIndex, TextureSlot::Raw),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Add(prevRes, SlotState(vi, lightIndex, previousHistory),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Add(curRes, SlotState(vi, lightIndex, currentHistory),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetComputeRootSignature(temporalRootSignature_.Get());
        cmdList->SetPipelineState(temporalPipelineState_.Get());

        // t0=RawShadow, t1=Normal, t2=SceneDepth, t3=History(前フレーム), t4=MotionVector, u0=History(今フレーム)
        cmdList->SetComputeRootDescriptorTable(0, SlotSRV(vi, lightIndex, TextureSlot::Raw));
        cmdList->SetComputeRootDescriptorTable(1, normalRoughnessSRV);
        cmdList->SetComputeRootDescriptorTable(2, sceneDepthSRV);
        cmdList->SetComputeRootDescriptorTable(3, SlotSRV(vi, lightIndex, previousHistory));
        cmdList->SetComputeRootDescriptorTable(4, motionVectorSRV);
        cmdList->SetComputeRootDescriptorTable(5, SlotUAV(vi, lightIndex, currentHistory));

        TemporalConstants tc{};
        tc.traceWidth = static_cast<int>(view.traceWidth);
        tc.traceHeight = static_cast<int>(view.traceHeight);
        tc.maxHistoryFrames = static_cast<float>(settings_.maxHistoryFrames);
        // 初回フレーム（履歴未生成）か、デバッグトグルで明示的に無効化された場合は履歴を使わない
        tc.disableHistory = (view.isHistoryValid && !settings_.disableHistory) ? 0.0f : 1.0f;
        ExtractProjectionZW(projection, tc.projM33, tc.projM43);
        tc.traceScale = static_cast<int>(view.traceScale);
        tc.traceOffsetX = static_cast<int>(view.traceOffsetX);
        tc.traceOffsetY = static_cast<int>(view.traceOffsetY);
        tc.fullWidth = static_cast<int>(view.width);
        tc.fullHeight = static_cast<int>(view.height);
        cmdList->SetComputeRoot32BitConstants(6, kTemporalConstantCount, &tc, 0);

        cmdList->Dispatch(DispatchGroupCount(view.traceWidth), DispatchGroupCount(view.traceHeight), 1);

        // Denoise が SRV として読むので完了を保証してから遷移
        ResourceBarrierHelper::UAV(cmdList, curRes);
        ResourceBarrierHelper::Transition(cmdList, curRes,
            SlotState(vi, lightIndex, currentHistory),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        view.isHistoryValid = true;
    }

    // =========================================================================
    // A-Trous デノイズ ＋ フル解像度への解決（ApplyTemporal の直後に呼ぶ）
    //   入力はテンポラル出力（＝今フレームの履歴）。ping/pong は専用スロットで行う。
    // =========================================================================
    void RayTracingShadowManager::Denoise(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        const Matrix4x4& projection,
        ViewID viewId,
        uint32_t lightIndex)
    {
        if (!isInitialized_) return;

        lightIndex = (std::min)(lightIndex, kMaxDirectionalLights - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][lightIndex];
        if (!view.dispatchedThisFrame) return;

        // A-Trous: ステップ幅 1 → 2 → 4 → 8
        static constexpr int   kSteps[kMaxAtrousPassCount] = { 1,    2,    4,    8 };
        static constexpr float kPhiNormal[kMaxAtrousPassCount] = { 4.0f, 8.0f, 16.0f, 32.0f };
        static constexpr float kPhiShadow[kMaxAtrousPassCount] = { 1.0f, 0.7f,  0.5f,  0.35f };

        // デノイズ PSO が作れていない場合はパス数 0 として扱う
        // （その場合でも解決パスがテンポラル結果を Mask へ運ぶので影は消えない）
        const int numPasses = denoiseInitialized_ ? GetEffectiveAtrousPassCount() : 0;

        // フル解像度のときだけ最終パスが Mask へ直接書ける（ハーフだと寸法が違う）
        const bool directToMask = (view.traceScale == 1) && (numPasses > 0);

        TextureSlot lastSlot = view.CurrentHistorySlot();

        if (numPasses > 0) {
            cmdList->SetComputeRootSignature(denoiseRootSignature_.Get());
            cmdList->SetPipelineState(denoisePipelineState_.Get());

            const UINT groupX = DispatchGroupCount(view.traceWidth);
            const UINT groupY = DispatchGroupCount(view.traceHeight);

            for (int pass = 0; pass < numPasses; ++pass)
            {
                const bool isLast = (pass == numPasses - 1);
                const TextureSlot inSlot = lastSlot;
                const TextureSlot outSlot = (isLast && directToMask)
                    ? TextureSlot::Mask
                    : ((pass % 2 == 0) ? TextureSlot::DenoiseA : TextureSlot::DenoiseB);

                ID3D12Resource* inputRes = SlotResource(vi, lightIndex, inSlot);
                ID3D12Resource* outputRes = SlotResource(vi, lightIndex, outSlot);
                if (!inputRes || !outputRes) return;

                // 入力: SRV へ、出力: UAV へ（2リソースを一括バリア）
                {
                    ResourceBarrierBatch batch(cmdList);
                    batch.Add(inputRes, SlotState(vi, lightIndex, inSlot),
                        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    batch.Add(outputRes, SlotState(vi, lightIndex, outSlot),
                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }

                cmdList->SetComputeRootDescriptorTable(0, SlotSRV(vi, lightIndex, inSlot));   // t0: InputShadow
                cmdList->SetComputeRootDescriptorTable(1, normalRoughnessSRV);                // t1: Normal
                cmdList->SetComputeRootDescriptorTable(2, sceneDepthSRV);                     // t2: SceneDepth
                cmdList->SetComputeRootDescriptorTable(3, SlotUAV(vi, lightIndex, outSlot));  // u0: Output

                DenoiseConstants dc{};
                dc.stepSize = kSteps[pass];
                dc.phiShadow = kPhiShadow[pass];
                dc.phiNormal = kPhiNormal[pass];
                dc.phiDepth = settings_.denoisePhiDepth;
                dc.traceWidth = static_cast<int>(view.traceWidth);
                dc.traceHeight = static_cast<int>(view.traceHeight);
                ExtractProjectionZW(projection, dc.projM33, dc.projM43);
                dc.traceScale = static_cast<int>(view.traceScale);
                dc.traceOffsetX = static_cast<int>(view.traceOffsetX);
                dc.traceOffsetY = static_cast<int>(view.traceOffsetY);
                dc.fullWidth = static_cast<int>(view.width);
                dc.fullHeight = static_cast<int>(view.height);
                cmdList->SetComputeRoot32BitConstants(4, kDenoiseConstantCount, &dc, 0);

                // directToMask は traceScale==1 のときだけ成立するので、
                // Mask へ直接書く場合もトレース解像度＝フル解像度で寸法は一致する
                cmdList->Dispatch(groupX, groupY, 1);

                ResourceBarrierHelper::UAV(cmdList, outputRes);
                lastSlot = outSlot;
            }
        }

        if (!directToMask) {
            // ハーフ解像度、またはデノイズ 0 パス。
            // トレース解像度の最終結果をフル解像度の Mask へ解決する。
            ResolveToFullResolution(cmdList, normalRoughnessSRV, sceneDepthSRV, projection,
                vi, lightIndex, lastSlot);
        }

        // 後段（DeferredLighting）はピクセルシェーダから読む
        ResourceBarrierHelper::Transition(cmdList, SlotResource(vi, lightIndex, TextureSlot::Mask),
            SlotState(vi, lightIndex, TextureSlot::Mask), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // =========================================================================
    // 解決パス（トレース解像度 → フル解像度のバイラテラルアップサンプル）
    // =========================================================================
    void RayTracingShadowManager::ResolveToFullResolution(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        const Matrix4x4& projection,
        uint32_t viewIndex,
        uint32_t lightIndex,
        TextureSlot sourceSlot)
    {
        // ハーフ解像度で解いた影を、法線と深度をガイドにフル解像度へバイラテラルアップサンプルする
        if (!resolveInitialized_) return;

        auto& view = views_[viewIndex][lightIndex];
        ID3D12Resource* sourceRes = SlotResource(viewIndex, lightIndex, sourceSlot);
        ID3D12Resource* maskRes = SlotResource(viewIndex, lightIndex, TextureSlot::Mask);
        if (!sourceRes || !maskRes) return;

        {
            ResourceBarrierBatch batch(cmdList);
            batch.Add(sourceRes, SlotState(viewIndex, lightIndex, sourceSlot),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Add(maskRes, SlotState(viewIndex, lightIndex, TextureSlot::Mask),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetComputeRootSignature(resolveRootSignature_.Get());
        cmdList->SetPipelineState(resolvePipelineState_.Get());

        cmdList->SetComputeRootDescriptorTable(0, SlotSRV(viewIndex, lightIndex, sourceSlot)); // t0: トレース解像度シャドウ
        cmdList->SetComputeRootDescriptorTable(1, sceneDepthSRV);                              // t1: フル解像度 深度
        cmdList->SetComputeRootDescriptorTable(2, normalRoughnessSRV);                         // t2: フル解像度 法線
        cmdList->SetComputeRootDescriptorTable(3, SlotUAV(viewIndex, lightIndex, TextureSlot::Mask)); // u0

        ResolveConstants rc{};
        rc.fullWidth = static_cast<int>(view.width);
        rc.fullHeight = static_cast<int>(view.height);
        rc.traceWidth = static_cast<int>(view.traceWidth);
        rc.traceHeight = static_cast<int>(view.traceHeight);
        ExtractProjectionZW(projection, rc.projM33, rc.projM43);
        rc.traceScale = static_cast<int>(view.traceScale);
        rc.traceOffsetX = static_cast<int>(view.traceOffsetX);
        rc.traceOffsetY = static_cast<int>(view.traceOffsetY);
        rc.phiDepth = settings_.upsamplePhiDepth;
        cmdList->SetComputeRoot32BitConstants(4, kResolveConstantCount, &rc, 0);

        cmdList->Dispatch(DispatchGroupCount(view.width), DispatchGroupCount(view.height), 1);

        ResourceBarrierHelper::UAV(cmdList, maskRes);
    }
}
