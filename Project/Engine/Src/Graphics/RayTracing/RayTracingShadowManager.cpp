#include "pch.h"
#include "RayTracingShadowManager.h"
#include "Graphics/RayTracing/RTShadowBindings.h"
#include "Graphics/RayTracing/Debug/RTDebugRegistry.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "AccelerationStructureManager.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Utility/Logger/Logger.h"
#include "Utility/CVar/CVar.h"
#include "Camera/View/ViewInfo.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/Resource/UploadRing.h"

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
            "基本のシャドウレイ本数（本影・日向で使う）。コストはほぼ本数に比例する。"
            "影の縁は PenumbraSamples が別に適用されるため 1 で十分",
            CVarRange{ 1.0f, 16.0f } };

        CVar<int> cvPenumbraSamples{
            "r.RTShadow.PenumbraSamples", 8,
            "影の縁（前フレームの蓄積値が中間 or 蓄積が浅いピクセル）で使うレイ本数。"
            "1spp のノイズは縁でしか出ないので、ここだけ増やすとコスト増を縁の面積に限定できる。"
            "縁のノイズ σ は 1/sqrt(本数)",
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
            "r.RTShadow.MaxHistoryFrames", 64,
            "テンポラル蓄積フレーム数の上限。ピクセルごとの蓄積カウント N で α=1/N の適応ブレンドを行い、"
            "静止時は 1/この値 まで収束する（旧 HistoryAlpha の固定 α は定常ノイズが残るため廃止）。"
            "大きいほど滑らかだが、影の変化への追従はクランプ棄却頼みになる",
            CVarRange{ 1.0f, 255.0f } };

        CVar<int> cvAtrousPassCount{
            "r.RTShadow.AtrousPassCount", 3,
            "A-Trous デノイズのパス数。0 = デノイズ無効。カーネルは 5x5 の B3 スプラインで、"
            "1 パスごとにステップ幅が 1→2→4→8 と倍になる（3 パスで実効 17x17）",
            CVarRange{ 0.0f, 4.0f } };

        CVar<float> cvDenoisePhiShadow{
            "r.RTShadow.DenoisePhiShadow", 6.0f,
            "A-Trous のエッジ停止関数の σ 倍率。影値の差を「その値がもつ推定標準偏差」で割って"
            "評価するため、収束したピクセルは自動的にぼけにくくなる。大きいほど広くぼける",
            CVarRange{ 0.5f, 24.0f } };

        CVar<float> cvStillnessSigma{
            "r.RTShadow.StillnessSigma", 1.25f,
            "静止化しきい値（σ の倍数、0 で無効）。現フレームと履歴の差がこの倍数×σ 未満の更新は"
            "ノイズと区別できないので捨て、履歴をそのまま出す（静止シーンで影の縁が完全に止まる）。"
            "上げすぎると影の微小変化への追従が鈍る",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvHistoryClampSigma{
            "r.RTShadow.HistoryClampSigma", 3.0f,
            "履歴クランプ帯の広さ（推定標準偏差の何倍まで許容するか）。小さすぎるとモンテカルロの"
            "揺らぎで履歴が毎フレーム棄却され蓄積が収束しない。大きすぎると影の変化でゴーストが出る",
            CVarRange{ 0.5f, 8.0f } };

        CVar<float> cvHistoryDepthTolerance{
            "r.RTShadow.HistoryDepthTolerance", 0.05f,
            "再投影した履歴を採用する相対深度しきい値。これを超える深度差はディスオクルージョンと"
            "みなして履歴を捨てる（斜面の勾配ぶんはシェーダー側で自動加算）",
            CVarRange{ 0.005f, 0.5f } };

        CVar<float> cvDenoisePhiDepth{
            "r.RTShadow.DenoisePhiDepth", 1.0f,
            "A-Trous / テンポラルの深度エッジ重み。大きいほどエッジ検出が厳しく影の輪郭が保たれる"
            "（ぼけにくい）。小さいほど深度差を跨いで広くぼける",
            CVarRange{ 0.05f, 8.0f } };

        CVar<bool> cvCycleTraceOffset{
            "r.RTShadow.CycleTraceOffset", false,
            "ハーフ解像度時に 2x2 のサンプル位置をフレームごとに巡回させる。"
            "細い遮蔽物を拾えるようになる代わりに、影の縁では毎フレーム別のサブピクセルを"
            "トレースすることになり、テンポラル蓄積から見ると入力が 4 フレーム周期で振動する"
            "（＝縁が這うように動く）。既定 false = 常に 2x2 の中央代表点を撃つ",
            CVarRange{} };

        CVar<bool> cvHalfResolutionTrace{
            "r.RTShadow.HalfResolutionTrace", true,
            "トレース〜デノイズを 1/2 解像度で行い、最後にバイラテラルアップサンプルする。"
            "レイ本数もデノイズ帯域も 1/4 になる（最終マスクはフル解像度のまま）" };

        CVar<float> cvUpsamplePhiDepth{
            "r.RTShadow.UpsamplePhiDepth", 8.0f,
            "アップサンプル時の深度エッジ重み（ハーフ解像度時のみ有効）。"
            "大きいほど深度が近い結果しか採用しない（輪郭はシャープだがエイリアスが出る）",
            CVarRange{ 0.5f, 40.0f } };

        CVar<int> cvMaxLocalLights{
            "r.RTShadow.MaxLocalLights", 4,
            "同時に影を落とす点光源・スポット・エリアの数。影を落とす設定で画面に届くライトから、"
            "カメラに近い順に選ぶ。0 で局所ライトの影を撃たない",
            CVarRange{ 0.0f, 4.0f } };
        // 残像・ゴーストの切り分け用の切り替え（保存しない）
        CVar<bool> cvHistoryEnabled{
            "r.RTShadow.HistoryEnabled", true,
            "テンポラル蓄積の履歴を参照する（デバッグ用）。"
            "OFF にすると毎フレーム現フレームの空間前処理結果のみを使う",
            CVarRange{}, CVarFlags::NoSave };
    }

    void RayTracingShadowManager::SyncSettingsFromCVars()
    {
        settings_.softShadowSamples = cvSoftShadowSamples.Get();
        settings_.penumbraSamples = cvPenumbraSamples.Get();
        settings_.stillnessSigma = cvStillnessSigma.Get();
        settings_.lightRadius = cvLightRadius.Get();
        settings_.shadowBias = cvShadowBias.Get();
        settings_.maxRayDistance = cvMaxRayDistance.Get();
        settings_.scaleRayDistanceBySunElevation = cvScaleRayDistanceBySunElevation.Get();
        settings_.maxHistoryFrames = cvMaxHistoryFrames.Get();
        settings_.atrousPassCount = cvAtrousPassCount.Get();
        settings_.denoisePhiShadow = cvDenoisePhiShadow.Get();
        settings_.historyClampSigma = cvHistoryClampSigma.Get();
        settings_.historyDepthTolerance = cvHistoryDepthTolerance.Get();
        settings_.denoisePhiDepth = cvDenoisePhiDepth.Get();
        settings_.halfResolutionTrace = cvHalfResolutionTrace.Get();
        settings_.cycleTraceOffset = cvCycleTraceOffset.Get();
        settings_.upsamplePhiDepth = cvUpsamplePhiDepth.Get();
        settings_.disableHistory = !cvHistoryEnabled.Get();
    }

    void RayTracingShadowManager::SetSettings(const RayTracingShadowSettings& settings)
    {
        // CVar が唯一の保持者。書き戻すことで UI・自動保存にも反映される
        cvSoftShadowSamples.Set(settings.softShadowSamples);
        cvPenumbraSamples.Set(settings.penumbraSamples);
        cvStillnessSigma.Set(settings.stillnessSigma);
        cvLightRadius.Set(settings.lightRadius);
        cvShadowBias.Set(settings.shadowBias);
        cvMaxRayDistance.Set(settings.maxRayDistance);
        cvScaleRayDistanceBySunElevation.Set(settings.scaleRayDistanceBySunElevation);
        cvMaxHistoryFrames.Set(settings.maxHistoryFrames);
        cvAtrousPassCount.Set(settings.atrousPassCount);
        cvDenoisePhiShadow.Set(settings.denoisePhiShadow);
        cvHistoryClampSigma.Set(settings.historyClampSigma);
        cvHistoryDepthTolerance.Set(settings.historyDepthTolerance);
        cvDenoisePhiDepth.Set(settings.denoisePhiDepth);
        cvHalfResolutionTrace.Set(settings.halfResolutionTrace);
        cvCycleTraceOffset.Set(settings.cycleTraceOffset);
        cvUpsamplePhiDepth.Set(settings.upsamplePhiDepth);
        cvHistoryEnabled.Set(!settings.disableHistory);
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
        int      penumbraSamples;    // offset 52  影の縁で使うレイ本数（適応サンプリング）
        int      historyValid;       // offset 56  履歴テクスチャが有効なら 1
        int      pad2;               // offset 60  → row4 終了(64)
        Matrix4x4 invViewProj;       // offset 64  深度復元用 → 128
        float    waterHeight;        // offset 128 平らな水面の高さ
        float    waterRefractiveIndex; // offset 132
        float    waterRegionCenterX; // offset 136
        float    waterRegionCenterZ; // offset 140 → row9 終了(144)
        float    waterRegionHalfExtentX; // offset 144
        float    waterRegionHalfExtentZ; // offset 148
        int      waterEnabled;       // offset 152 1 なら水中の受光点を屈折した経路で調べる
        int      pad3;               // offset 156 → row10 終了(160)
        uint32_t hitInstanceTableIndex; // offset 160 材質のインスタンス表（kRTHitNoTexture = アルファを調べない）
        uint32_t hitSubMeshTableIndex;  // offset 164 材質のサブメッシュ表
        uint32_t pad4;               // offset 168
        uint32_t pad5;               // offset 172 → row11 終了(176)
        int      lightType;          // offset 176 0 = 平行光源 / 1 = 点光源 / 2 = スポット / 3 = エリア（LightType の並び）
        float    lightPositionX;     // offset 180 点光源・スポット・エリアの位置
        float    lightPositionY;     // offset 184
        float    lightPositionZ;     // offset 188 → row12 終了(192)
        float    lightRange;         // offset 192 届く距離 [m]
        float    spotCosOuterCone;   // offset 196 スポットの外角の余弦
        float    areaHalfWidth;      // offset 200 エリアの発光面の半分の幅
        float    areaHalfHeight;     // offset 204 → row13 終了(208)
        float    areaRightX;         // offset 208 エリアの発光面の右向き
        float    areaRightY;         // offset 212
        float    areaRightZ;         // offset 216
        float    pad6;               // offset 220 → row14 終了(224)
        float    areaUpX;            // offset 224 エリアの発光面の上向き
        float    areaUpY;            // offset 228
        float    areaUpZ;            // offset 232
        float    pad7;               // offset 236 → row15 終了(240)
    };
    static_assert(sizeof(ShadowRayConstants) == 240, "ShadowRayConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kShadowRayConstantsFields[] = {
        CB_FIELD(ShadowRayConstants, lightDir), CB_FIELD(ShadowRayConstants, shadowBias),
        CB_FIELD(ShadowRayConstants, maxRayDistance), CB_FIELD(ShadowRayConstants, lightRadius),
        CB_FIELD(ShadowRayConstants, softShadowSamples), CB_FIELD(ShadowRayConstants, frameIndex),
        CB_FIELD(ShadowRayConstants, screenWidth), CB_FIELD(ShadowRayConstants, screenHeight),
        CB_FIELD(ShadowRayConstants, traceOffsetX), CB_FIELD(ShadowRayConstants, traceOffsetY),
        CB_FIELD(ShadowRayConstants, traceScale), CB_FIELD(ShadowRayConstants, penumbraSamples),
        CB_FIELD(ShadowRayConstants, historyValid), CB_FIELD(ShadowRayConstants, pad2),
        CB_FIELD(ShadowRayConstants, invViewProj),
        CB_FIELD(ShadowRayConstants, waterHeight), CB_FIELD(ShadowRayConstants, waterRefractiveIndex),
        CB_FIELD(ShadowRayConstants, waterRegionCenterX), CB_FIELD(ShadowRayConstants, waterRegionCenterZ),
        CB_FIELD(ShadowRayConstants, waterRegionHalfExtentX), CB_FIELD(ShadowRayConstants, waterRegionHalfExtentZ),
        CB_FIELD(ShadowRayConstants, waterEnabled), CB_FIELD(ShadowRayConstants, pad3),
        CB_FIELD(ShadowRayConstants, hitInstanceTableIndex), CB_FIELD(ShadowRayConstants, hitSubMeshTableIndex),
        CB_FIELD(ShadowRayConstants, pad4), CB_FIELD(ShadowRayConstants, pad5),
        CB_FIELD(ShadowRayConstants, lightType), CB_FIELD(ShadowRayConstants, lightPositionX),
        CB_FIELD(ShadowRayConstants, lightPositionY), CB_FIELD(ShadowRayConstants, lightPositionZ),
        CB_FIELD(ShadowRayConstants, lightRange), CB_FIELD(ShadowRayConstants, spotCosOuterCone),
        CB_FIELD(ShadowRayConstants, areaHalfWidth), CB_FIELD(ShadowRayConstants, areaHalfHeight),
        CB_FIELD(ShadowRayConstants, areaRightX), CB_FIELD(ShadowRayConstants, areaRightY),
        CB_FIELD(ShadowRayConstants, areaRightZ), CB_FIELD(ShadowRayConstants, pad6),
        CB_FIELD(ShadowRayConstants, areaUpX), CB_FIELD(ShadowRayConstants, areaUpY),
        CB_FIELD(ShadowRayConstants, areaUpZ), CB_FIELD(ShadowRayConstants, pad7),
    };
    CB_VERIFY_LAYOUT(ShadowRayConstants, kShadowRayConstantsFields);
    CB_BIND_HLSL(ShadowRayConstants, kShadowRayConstantsFields, "ShadowRayConstants");

    namespace {
        /// @brief ハーフ解像度時に巡回させる 2x2 サンプル位置
        /// @details 固定オフセットだと常に同じサブピクセルしか見ないため、
        ///          細い遮蔽物が原理的に落ちる。4 フレームで全サブピクセルを踏み、
        ///          テンポラル蓄積で平均させる。
        constexpr UINT kTraceOffsetTable[4][2] = { {0, 0}, {1, 1}, {1, 0}, {0, 1} };
    }

    // =========================================================================
    // Initialize
    // =========================================================================
    bool RayTracingShadowManager::Initialize(const RTEffectInitContext& init)
    {
        Logger& log = Logger::GetInstance();

        // 共通基盤へ委譲（ポインタ保持と DXR サポート判定・警告ログまで面倒を見る）
        if (!InitializeBase(init.dxCommon, init.descriptorAllocator, init.asMgr, init.shaderProgramCache,
            "RayTracingShadowManager", "RTShadow")) {
            return false;
        }

        // lib_6_6 のコンパイルとリフレクションはキャッシュが担当する
        const ShaderProgram* program =
            shaderProgramCache_->GetOrCreateLibrary(L"RTShadow.hlsl", "RTShadow");
        if (!program) {
            log.Log("RayTracingShadowManager: Shader compile failed",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }
        shaderBlob_ = program->GetCS();

        // グローバルルートシグネチャをリフレクションから構築する
        // （ShadowRayConstants は記録中のフレームの UploadRing に置き、ルート CBV で差す）
        RootSignatureConfig config;
        // DXR に入力アセンブラは無い。アルファで抜く判定は材質の表とテクスチャをヒープから直接引く
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
        // 物のテクスチャを繰り返して引く（Include/RayTracing/RTSceneTables.hlsli）
        config.ConfigureSampler("gLinearWrap", SamplerConfig::Linear());
        if (!BuildGlobalRootSignature(*program, RTShadowBind::kDecls,
            RTShadowBind::Slot::Count, config)) {
            return false;
        }
        log.Log("RayTracingShadowManager: Global root signature created",
            LogLevel::Info, LogCategory::Graphics);

        // State Object とシェーダーテーブルは別スレッドで作り、最初のディスパッチの前に待つ
        // （レイは RayGen の中から RayQuery で撃つ。ミスとヒットグループは持たない）
        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob_)
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        shaderTableBuilder_
            .SetRayGenShader(L"RTShadowRayGen");
        BuildPipelineAsync(std::move(pipelineBuilder));

        isInitialized_ = true;
        shaderBlob_ = nullptr;  // State Object構築後は不要（実体はキャッシュが保持）

        // 光の届いた割合は影の枠ごとに 1 チャンネルでデノイズする
        denoiser_.Initialize(init, "RTShadow", kViewCount * kMaxShadowSlots);

        log.Log("RayTracingShadowManager: Initialized successfully",
            LogLevel::Info, LogCategory::Graphics);
        return true;
    }

    // =========================================================================
    // アクセサ
    // =========================================================================
    UINT RayTracingShadowManager::GetTraceScale() const
    {
        return settings_.halfResolutionTrace ? 2u : 1u;
    }

    void RayTracingShadowManager::OnResize(uint32_t width, uint32_t height)
    {
        if (!isInitialized_) {
            return;
        }
        denoiser_.OnResize(width, height, GetTraceScale());
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetShadowSRVHandle(
        ViewID viewId, uint32_t shadowSlot) const
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return denoiser_.GetOutputSRV(GetDenoiseChannel(viewId, shadowSlot));
    }

    const RayTracingDispatchInfo& RayTracingShadowManager::GetDispatchInfo(
        ViewID viewId, uint32_t shadowSlot) const
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return views_[static_cast<uint32_t>(viewId)][shadowSlot].dispatchInfo;
    }

    int RayTracingShadowManager::GetEffectiveAtrousPassCount() const
    {
        return RTDenoiser::ClampAtrousPassCount(settings_.atrousPassCount);
    }

    RTDenoiseSettings RayTracingShadowManager::MakeDenoiseSettings() const
    {
        RTDenoiseSettings denoise{};
        denoise.maxHistoryFrames = settings_.maxHistoryFrames;
        denoise.historyClampSigma = settings_.historyClampSigma;
        denoise.historyDepthTolerance = settings_.historyDepthTolerance;
        denoise.stillnessSigma = settings_.stillnessSigma;
        denoise.atrousPassCount = settings_.atrousPassCount;
        denoise.atrousPhiValue = settings_.denoisePhiShadow;
        denoise.atrousPhiDepth = settings_.denoisePhiDepth;
        denoise.upsamplePhiDepth = settings_.upsamplePhiDepth;
        denoise.useHistory = !settings_.disableHistory;
        return denoise;
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

        // 光源が地平線より下（sin<=0）なら基準値のまま返す。
        // 倍率は kMaxRayDistanceScale で頭打ちにする。
        if (sinElevation <= 0.0f) {
            return settings_.maxRayDistance;
        }
        const float minSin = 1.0f / kMaxRayDistanceScale;
        const float scale = 1.0f / (std::max)(sinElevation, minSin);
        return settings_.maxRayDistance * (std::min)(scale, kMaxRayDistanceScale);
    }

    GpuResource& RayTracingShadowManager::GetShadowResource(
        ViewID viewId,
        uint32_t shadowSlot)
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return denoiser_.GetOutput(GetDenoiseChannel(viewId, shadowSlot));
    }

    bool RayTracingShadowManager::IsDispatchedThisFrame(
        ViewID viewId, uint32_t shadowSlot) const
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return views_[static_cast<uint32_t>(viewId)][shadowSlot].dispatchedThisFrame;
    }

    void RayTracingShadowManager::BeginFrame()
    {
        for (auto& viewRow : views_) {
            for (auto& v : viewRow) {
                v.dispatchedThisFrame = false;
            }
        }
    }

    namespace
    {
        /// @brief 球が視錐台の完全に外側にあるか
        bool IsSphereOutsideFrustum(const Frustum& frustum, const Vector3& center, float radius)
        {
            for (const auto& plane : frustum.planes) {
                if (plane.DistanceTo(center) < -radius) {
                    return true;
                }
            }
            return false;
        }

        /// @brief 局所ライトの影の形を作る（向きと発光面の基底は LightManager::UpdateAll と同じ求め方）
        RTShadowLightShape MakeLocalShadowShape(LightType type, LightHandle handle, const Light& light)
        {
            RTShadowLightShape shape{};
            shape.type = type;
            shape.position = light.position;
            const float lengthSq = Dot(light.direction, light.direction);
            shape.direction = (lengthSq > 1.0e-12f) ? Normalize(light.direction) : Vector3{ 0.0f, -1.0f, 0.0f };
            shape.range = light.range;
            shape.sourceRadius = (std::max)(light.sourceRadius, 0.0f);
            if (type == LightType::Spot) {
                const float outerDeg = (std::max)(light.outerConeAngleDeg, 0.1f);
                shape.cosOuterCone = std::cos(outerDeg * MathCore::Constants::kDegToRad);
            }
            if (type == LightType::Area) {
                LightManager::ComputeAreaLightBasis(light.direction, shape.areaRight, shape.areaUp);
                shape.areaHalfWidth = light.areaWidth * 0.5f;
                shape.areaHalfHeight = light.areaHeight * 0.5f;
            }
            // 種類・世代・スロットの番号で見分ける（同じスロットを別のライトが使い直しても履歴を引き継がない）
            shape.key = (1ull << 63)
                | (static_cast<uint64_t>(type) << 40)
                | (static_cast<uint64_t>(handle.generation) << 16)
                | static_cast<uint64_t>(handle.index);
            return shape;
        }
    }

    void RayTracingShadowManager::SelectLocalShadowLights(const LightManager& lights, const ViewInfo& view)
    {
        struct Candidate {
            LightType type;
            uint32_t gpuIndex;
            LightHandle handle;
            const Light* light;
            float score;
        };
        std::vector<Candidate> candidates;
        const uint32_t maxLocal = view.isValid
            ? static_cast<uint32_t>(std::clamp(cvMaxLocalLights.Get(), 0, static_cast<int>(kMaxLocalLights)))
            : 0u;
        if (maxLocal > 0) {
            lights.ForEachLocalLight([&](LightType type, uint32_t gpuIndex, LightHandle handle, const Light& light) {
                if (!light.enabled || !light.castShadows || light.intensity <= 0.0f || light.range <= 0.0f) {
                    return;
                }
                // 光が届く範囲を囲む球（エリアは発光面の端から届く距離ぶん広げる）
                float reach = light.range;
                if (type == LightType::Area) {
                    reach += 0.5f * std::sqrt(light.areaWidth * light.areaWidth + light.areaHeight * light.areaHeight);
                }
                if (IsSphereOutsideFrustum(view.frustum, light.position, reach)) {
                    return;
                }
                // カメラから球の表面までの距離を届く距離で割った値が小さいほど先に選ぶ（中にいれば 0）
                const float distance = Length(light.position - view.position);
                candidates.push_back({ type, gpuIndex, handle, &light, (std::max)(distance - reach, 0.0f) / reach });
            });
            std::stable_sort(candidates.begin(), candidates.end(),
                [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
            if (candidates.size() > maxLocal) {
                candidates.resize(maxLocal);
            }
        }

        // 前のフレームと同じライトは同じ枠に残し、残りのライトを空いた枠へ順に入れる
        LocalShadowSlot next[kMaxLocalLights]{};
        bool taken[kMaxLocalLights] = {};
        std::vector<bool> placed(candidates.size(), false);
        const auto assign = [&](uint32_t local, const Candidate& candidate) {
            LocalShadowSlot& slot = next[local];
            slot.active = true;
            slot.type = candidate.type;
            slot.gpuIndex = candidate.gpuIndex;
            slot.handle = candidate.handle;
            slot.shaderKey = (static_cast<uint32_t>(candidate.type) << 16) | candidate.gpuIndex;
            slot.shape = MakeLocalShadowShape(candidate.type, candidate.handle, *candidate.light);
            taken[local] = true;
        };
        for (size_t c = 0; c < candidates.size(); ++c) {
            for (uint32_t local = 0; local < kMaxLocalLights; ++local) {
                const LocalShadowSlot& previous = localSlots_[local];
                if (!taken[local] && previous.active
                    && previous.handle == candidates[c].handle && previous.type == candidates[c].type) {
                    assign(local, candidates[c]);
                    placed[c] = true;
                    break;
                }
            }
        }
        for (size_t c = 0; c < candidates.size(); ++c) {
            if (placed[c]) {
                continue;
            }
            for (uint32_t local = 0; local < kMaxLocalLights; ++local) {
                if (!taken[local]) {
                    assign(local, candidates[c]);
                    break;
                }
            }
        }
        std::copy(std::begin(next), std::end(next), std::begin(localSlots_));
    }

    const RayTracingShadowManager::LocalShadowSlot& RayTracingShadowManager::GetLocalShadowSlot(uint32_t localIndex) const
    {
        return localSlots_[(std::min)(localIndex, kMaxLocalLights - 1)];
    }

    // =========================================================================
    // Dispatch（毎フレーム呼び出し）
    //   RayGen はトレース解像度で Raw スロットへ書き出す。
    // =========================================================================
    void RayTracingShadowManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE normalRoughnessSRV,
        const RTShadowLightShape& light,
        const Matrix4x4& invViewProj,
        UINT width, UINT height,
        ViewID viewId,
        uint32_t shadowSlot,
        const RayTracingShadowWaterSurface& waterSurface,
        const RTHitTableIndices& hitTables)
    {
        // 設定は CVar が保持する。UI・設定復元・コンソールのどの経路で変わってもここで取り込む
        SyncSettingsFromCVars();

        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][shadowSlot];

        const UINT traceScale = GetTraceScale();
        const UINT traceWidth = (width + traceScale - 1) / traceScale;
        const UINT traceHeight = (height + traceScale - 1) / traceScale;

        // 診断情報を毎回リセットしてから埋める（失敗経路でも状態が UI に出るようにする）
        const int numSamples = std::clamp(settings_.softShadowSamples, 1, 16);
        // 平行光源は太陽の高さで射程を伸ばし、局所ライトはライトまでの距離で止める
        const bool isDirectional = (light.type == LightType::Directional);
        const float effectiveRayDistance = isDirectional ? ResolveEffectiveRayDistance(light.direction) : light.range;
        view.dispatchInfo = {};
        view.dispatchInfo.passName = "RTShadow";
        view.dispatchInfo.viewIndex = vi;
        view.dispatchInfo.slotIndex = shadowSlot;
        view.dispatchInfo.width = traceWidth;
        view.dispatchInfo.height = traceHeight;
        view.dispatchInfo.blasCount = asMgr_ ? asMgr_->GetBLASCount() : 0;
        view.dispatchInfo.AddExtra("traceScale", static_cast<float>(traceScale));
        view.dispatchInfo.AddExtra("samples/px", static_cast<float>(numSamples));
        view.dispatchInfo.AddExtra("penumbraSpp", static_cast<float>(std::clamp(settings_.penumbraSamples, 1, 16)));
        view.dispatchInfo.AddExtra("atrousPasses", static_cast<float>(GetEffectiveAtrousPassCount()));
        view.dispatchInfo.AddExtra("lightType", static_cast<float>(light.type));
        if (isDirectional) {
            view.dispatchInfo.AddExtra("lightRadius", settings_.lightRadius);
        } else {
            view.dispatchInfo.AddExtra("sourceRadius[m]", light.sourceRadius);
        }
        view.dispatchInfo.AddExtra("rayDist(実効)", effectiveRayDistance);

        if (!WaitForPipeline()) {
            view.dispatchInfo.status = RayTracingDispatchStatus::NotInitialized;
            return;
        }
        if (!asMgr_ || !asMgr_->IsSupported()) {
            view.dispatchInfo.status = RayTracingDispatchStatus::RayTracingUnsupported;
            return;
        }
        if (!asMgr_->IsTLASReady()) {
            view.dispatchInfo.status = RayTracingDispatchStatus::NoTLAS;
            return;
        }

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch START viewId={} shadowSlot={} size={}x{} trace={}x{} BLAS={} "
                "samples={} rayDist={:.1f} lightDir=({:.3f},{:.3f},{:.3f})",
                vi, shadowSlot, width, height, traceWidth, traceHeight, asMgr_->GetBLASCount(),
                numSamples, effectiveRayDistance,
                light.direction.x, light.direction.y, light.direction.z);
        }

        // 入力・履歴・マスクをそろえる（別のライトがこの枠へ来たとき・前のフレームに撃っていないときは履歴を捨てる）
        const uint32_t channel = GetDenoiseChannel(viewId, shadowSlot);
        if (!denoiser_.Prepare(channel, width, height, traceScale, light.key)) {
            view.dispatchInfo.status = RayTracingDispatchStatus::OutputAllocationFailed;
            return;
        }
        view.dispatchInfo.outputSrvPtr = denoiser_.GetOutputSRV(channel).ptr;

        // サンプル位置を巡回させる（カウンタは view × light ごと）
        if (traceScale > 1 && settings_.cycleTraceOffset) {
            const UINT phase = RTDebug::NoiseFrame(view.frameCount) & 3u;
            view.traceOffsetX = kTraceOffsetTable[phase][0];
            view.traceOffsetY = kTraceOffsetTable[phase][1];
        } else if (traceScale > 1) {
            // 巡回しない：デノイザが G-Buffer を読む 2x2 の中央の代表点を撃つ
            view.traceOffsetX = traceScale >> 1;
            view.traceOffsetY = traceScale >> 1;
        } else {
            view.traceOffsetX = 0;
            view.traceOffsetY = 0;
        }

        // 定数データを構築
        ShadowRayConstants constants{};
        constants.lightDir[0] = light.direction.x;
        constants.lightDir[1] = light.direction.y;
        constants.lightDir[2] = light.direction.z;
        constants.shadowBias = settings_.shadowBias;
        constants.maxRayDistance = effectiveRayDistance;
        // 平行光源は角半径 [rad]、点光源・スポットは光る球の半径 [m]
        constants.lightRadius = isDirectional ? settings_.lightRadius : (std::max)(light.sourceRadius, 0.0f);
        constants.softShadowSamples = numSamples;
        constants.frameIndex = RTDebug::NoiseFrame(view.frameCount++);
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.traceOffsetX = static_cast<int>(view.traceOffsetX);
        constants.traceOffsetY = static_cast<int>(view.traceOffsetY);
        constants.traceScale = static_cast<int>(traceScale);
        constants.penumbraSamples = std::clamp(settings_.penumbraSamples, 1, 16);
        // 初回フレームや履歴無効化中は適応サンプリングの判定材料が無いので基本本数で撃つ
        constants.historyValid = (denoiser_.IsHistoryValid(channel) && !settings_.disableHistory) ? 1 : 0;
        constants.invViewProj = invViewProj;
        constants.waterHeight = waterSurface.height;
        constants.waterRefractiveIndex = waterSurface.refractiveIndex;
        constants.waterRegionCenterX = waterSurface.regionCenterXZ[0];
        constants.waterRegionCenterZ = waterSurface.regionCenterXZ[1];
        constants.waterRegionHalfExtentX = waterSurface.regionHalfExtentXZ[0];
        constants.waterRegionHalfExtentZ = waterSurface.regionHalfExtentXZ[1];
        constants.waterEnabled = waterSurface.enabled ? 1 : 0;
        // 表がそろわないフレームはアルファを調べず、どの物も不透明として当てる
        constants.hitInstanceTableIndex = hitTables.IsValid() ? hitTables.instanceTable : kRTHitNoTexture;
        constants.hitSubMeshTableIndex = hitTables.IsValid() ? hitTables.subMeshTable : kRTHitNoTexture;
        constants.lightType = static_cast<int>(light.type);
        constants.lightPositionX = light.position.x;
        constants.lightPositionY = light.position.y;
        constants.lightPositionZ = light.position.z;
        constants.lightRange = light.range;
        constants.spotCosOuterCone = light.cosOuterCone;
        constants.areaHalfWidth = light.areaHalfWidth;
        constants.areaHalfHeight = light.areaHalfHeight;
        constants.areaRightX = light.areaRight.x;
        constants.areaRightY = light.areaRight.y;
        constants.areaRightZ = light.areaRight.z;
        constants.areaUpX = light.areaUp.x;
        constants.areaUpY = light.areaUp.y;
        constants.areaUpZ = light.areaUp.z;

        // CommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
            view.dispatchInfo.status = RayTracingDispatchStatus::CommandList4Unavailable;
            return;
        }

        // パイプライン設定
        cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        cmdList4->SetPipelineState1(stateObject_.Get());

        // 入力へ書き、前フレームの履歴（ペナンブラ適応サンプリングの判定）を読む
        denoiser_.BeginInputWrite(cmdList, channel);

        // 差し方は RootSlot の種別から ShaderBinder が決める（番号を直接触らない）
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[RTShadowBind::gShadowOutput], denoiser_.GetInputUAV(channel));
        binder.Set(bindings_[RTShadowBind::gScene], asMgr_->GetTLASSRVHandle());
        binder.Set(bindings_[RTShadowBind::gSceneDepth], sceneDepthSRV);
        binder.Set(bindings_[RTShadowBind::gNormalRoughness], normalRoughnessSRV);
        binder.Set(bindings_[RTShadowBind::gShadowHistory], denoiser_.GetPreviousHistorySRV(channel));
        binder.Set(bindings_[RTShadowBind::ShadowRayConstants], dxCommon_->GetUploadRing().AllocateConstants(constants));
        binder.ValidateBeforeDraw(bindings_);

        // DispatchRays はトレース解像度で行う（ハーフ解像度ならレイ本数は 1/4）
        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(traceWidth, traceHeight);
        cmdList4->DispatchRays(&dispatchDesc);

        // 時間の蓄積が読む前に書き込みの完了を待つ
        denoiser_.EndInputWrite(cmdList, channel);

        view.dispatchedThisFrame = true;
        view.dispatchInfo.status = RayTracingDispatchStatus::Dispatched;
        view.dispatchInfo.rayCount =
            static_cast<uint64_t>(traceWidth) * static_cast<uint64_t>(traceHeight)
            * static_cast<uint64_t>(numSamples);

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch complete viewId={} shadowSlot={} srvHandle=0x{:X}",
                vi, shadowSlot, denoiser_.GetOutputSRV(channel).ptr);
            ++dispatchLogCount_;
        }
    }

    // =========================================================================
    // 時間の蓄積と A-Trous・仕上げ（どちらも今フレームに撃った枠だけ）
    // =========================================================================
    void RayTracingShadowManager::ApplyTemporal(
        ID3D12GraphicsCommandList* cmdList,
        const RTDenoiseGuides& guides,
        ViewID viewId,
        uint32_t shadowSlot)
    {
        if (!isInitialized_) return;

        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        if (!views_[static_cast<uint32_t>(viewId)][shadowSlot].dispatchedThisFrame) return;

        denoiser_.Accumulate(cmdList, GetDenoiseChannel(viewId, shadowSlot), guides, MakeDenoiseSettings());
    }

    void RayTracingShadowManager::Denoise(
        ID3D12GraphicsCommandList* cmdList,
        const RTDenoiseGuides& guides,
        ViewID viewId,
        uint32_t shadowSlot)
    {
        if (!isInitialized_) return;

        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        if (!views_[static_cast<uint32_t>(viewId)][shadowSlot].dispatchedThisFrame) return;

        denoiser_.Filter(cmdList, GetDenoiseChannel(viewId, shadowSlot), guides, MakeDenoiseSettings());
    }
}
