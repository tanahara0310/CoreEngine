#include "pch.h"
#include "RayTracingShadowManager.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"
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
        float clampSigmaScale;// offset 44 → row3 終了(48)
        float clampMargin;    // offset 48  クランプ帯の固定下駄
        float depthTolerance; // offset 52  再投影を採用する相対深度しきい値
        float stillnessSigma; // offset 56  静止化しきい値（σ の倍数、0 で無効）
        int   pad1;           // offset 60 → row4 終了(64)
    };
    static_assert(sizeof(TemporalConstants) == 64, "TemporalConstants size mismatch with HLSL cbuffer");

    static constexpr Cb::Field kTemporalConstantsFields[] = {
        CB_FIELD(TemporalConstants, traceWidth), CB_FIELD(TemporalConstants, traceHeight),
        CB_FIELD(TemporalConstants, maxHistoryFrames), CB_FIELD(TemporalConstants, disableHistory),
        CB_FIELD(TemporalConstants, projM33), CB_FIELD(TemporalConstants, projM43),
        CB_FIELD(TemporalConstants, traceScale), CB_FIELD(TemporalConstants, traceOffsetX),
        CB_FIELD(TemporalConstants, traceOffsetY), CB_FIELD(TemporalConstants, fullWidth),
        CB_FIELD(TemporalConstants, fullHeight), CB_FIELD(TemporalConstants, clampSigmaScale),
        CB_FIELD(TemporalConstants, clampMargin), CB_FIELD(TemporalConstants, depthTolerance),
        CB_FIELD(TemporalConstants, stillnessSigma), CB_FIELD(TemporalConstants, pad1),
    };
    CB_VERIFY_LAYOUT(TemporalConstants, kTemporalConstantsFields);
    CB_BIND_HLSL(TemporalConstants, kTemporalConstantsFields, "TemporalConstants");

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
    // コンピュートパス（RS + PSO + バインド表）の共通構築
    // デノイズ / テンポラル / 解決の 3 本が全く同じ形なのでまとめてある
    // =========================================================================
    bool RayTracingShadowManager::CreateComputePass(
        const wchar_t* shaderPath,
        const ShaderBindingDecl* decls,
        size_t declCount,
        const char* rootConstantName,
        const char* debugLabel,
        ComputePass& outPass)
    {
        Logger& log = Logger::GetInstance();

        // プロファイルはキャッシュのキーに含まれるので cs_6_0 の他パスとは衝突しない
        const ShaderProgram* program =
            shaderProgramCache_->GetOrCreateCompute(shaderPath, debugLabel, L"cs_6_6");
        if (!program) {
            log.Logf(LogLevel::Warn, LogCategory::Graphics,
                "RayTracingShadowManager: {} shader compile failed", debugLabel);
            return false;
        }

        // 定数だけルート定数にする。dword 数はリフレクションが cbuffer サイズから決める
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);  // コンピュートに入力アセンブラは無い
        config.ConfigureResource(
            ResourceBindingConfig(rootConstantName, BindingStrategy::RootConstants));

        const auto buildResult = outPass.rootSigMgr.Build(
            dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RayTracingShadowManager: {} のルートシグネチャ構築に失敗: {}",
                debugLabel, buildResult.errorMessage);
            return false;
        }

        // 宣言表とシェーダー実体を突き合わせる（改名・削除・種別違いはここで検出される）
        try {
            outPass.bindings = BindingTable::Resolve(
                program->GetReflection(), decls, declCount, debugLabel);
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "RayTracingShadowManager: {} のバインド契約違反: {}", debugLabel, e.what());
            return false;
        }

        outPass.pipelineState = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), outPass.GetRootSignature(), program->GetCS(),
            std::string("RTShadow_") + debugLabel);
        if (!outPass.pipelineState) {
            return false;
        }

        outPass.initialized = true;
        log.Logf(LogLevel::Info, LogCategory::Graphics,
            "RayTracingShadowManager: {} pipeline created", debugLabel);
        return true;
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

        // 後段の 3 本はどれも「SRV n 枚 + UAV 1 枚 + ルート定数」なので共通構築
        CreateComputePass(
            L"RTShadowDenoise.hlsl",
            RTShadowDenoiseBind::kDecls, RTShadowDenoiseBind::Slot::Count,
            "DenoiseConstants", "A-Trous denoise", denoisePass_);
        CreateComputePass(
            L"RTShadowTemporal.CS.hlsl",
            RTShadowTemporalBind::kDecls, RTShadowTemporalBind::Slot::Count,
            "TemporalConstants", "Temporal accumulation", temporalPass_);
        CreateComputePass(
            L"RTShadowResolve.CS.hlsl",
            RTShadowResolveBind::kDecls, RTShadowResolveBind::Slot::Count,
            "ResolveConstants", "Bilateral resolve", resolvePass_);

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
        uint32_t viewIndex, uint32_t shadowSlot)
    {
        auto& view = views_[viewIndex][shadowSlot];
        const uint32_t maskSlot = MakeSlotIndex(viewIndex, shadowSlot, TextureSlot::Mask);
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

        const std::string suffix = "_v" + std::to_string(viewIndex) + "_l" + std::to_string(shadowSlot);

        RayTracingOutputViewSet::TextureOptions options{};
        options.format = kShadowTextureFormat;
        options.allowUAV = true;
        options.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        // 最終シャドウマスク（フル解像度。DeferredLighting がフル解像度座標で Load する）
        if (!outputViews_.EnsureTexture(dxCommon_, descriptorAllocator_, width, height,
            maskSlot, "RayTracingShadowManager", "RTShadow_Mask" + suffix, options)) {
            return false;
        }

        // 残りはすべてトレース解像度。
        // Raw は RayGen の生バイナリなので 1 チャンネル。
        // 履歴と A-Trous の ping-pong は「シャドウ値 / 推定分散 / 蓄積数 N / 線形深度」の
        // 4 チャンネルを同じレイアウトで運ぶ（詳細は RTShadowTemporal.CS.hlsl の先頭）。
        struct TraceSlot { TextureSlot slot; const char* name; DXGI_FORMAT format; };
        static constexpr TraceSlot kTraceSlots[] = {
            { TextureSlot::Raw,      "RTShadow_Raw",      kShadowRawFormat },
            { TextureSlot::DenoiseA, "RTShadow_DenoiseA", kShadowSignalFormat },
            { TextureSlot::DenoiseB, "RTShadow_DenoiseB", kShadowSignalFormat },
            { TextureSlot::HistoryA, "RTShadow_HistoryA", kShadowSignalFormat },
            { TextureSlot::HistoryB, "RTShadow_HistoryB", kShadowSignalFormat },
        };
        for (const TraceSlot& traceSlot : kTraceSlots) {
            options.format = traceSlot.format;
            if (!outputViews_.EnsureTexture(dxCommon_, descriptorAllocator_, traceWidth, traceHeight,
                MakeSlotIndex(viewIndex, shadowSlot, traceSlot.slot),
                "RayTracingShadowManager", traceSlot.name + suffix, options)) {
                return false;
            }
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "RayTracingShadowManager: Output texture[view={} light={}] created (mask {}x{} / trace {}x{})",
            viewIndex, shadowSlot, width, height, traceWidth, traceHeight);

        return true;
    }

    // =========================================================================
    // アクセサ
    // =========================================================================
    UINT RayTracingShadowManager::GetTraceScale() const
    {
        return settings_.halfResolutionTrace ? 2u : 1u;
    }

    void RayTracingShadowManager::Resize(UINT width, UINT height, ViewID viewId, uint32_t shadowSlot)
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        const UINT scale = GetTraceScale();
        EnsureOutputTexture(width, height,
            (width + scale - 1) / scale, (height + scale - 1) / scale, scale,
            static_cast<uint32_t>(viewId), shadowSlot);
    }

    void RayTracingShadowManager::OnResize(uint32_t width, uint32_t height)
    {
        if (!isInitialized_ || width == 0 || height == 0) {
            return;
        }

        const UINT scale = GetTraceScale();
        const UINT traceWidth = (width + scale - 1) / scale;
        const UINT traceHeight = (height + scale - 1) / scale;

        for (uint32_t viewIndex = 0; viewIndex < kViewCount; ++viewIndex) {
            for (uint32_t shadowSlot = 0; shadowSlot < kMaxShadowSlots; ++shadowSlot) {
                if (outputViews_.HasTexture(MakeSlotIndex(viewIndex, shadowSlot, TextureSlot::Mask))) {
                    EnsureOutputTexture(width, height, traceWidth, traceHeight, scale, viewIndex, shadowSlot);
                }
            }
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetShadowSRVHandle(
        ViewID viewId, uint32_t shadowSlot) const
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return SlotSRV(static_cast<uint32_t>(viewId), shadowSlot, TextureSlot::Mask);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetRawShadowSRVHandle(
        ViewID viewId, uint32_t shadowSlot) const
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return SlotSRV(static_cast<uint32_t>(viewId), shadowSlot, TextureSlot::Raw);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE RayTracingShadowManager::GetHistorySRVHandle(
        ViewID viewId, uint32_t shadowSlot) const
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        return SlotSRV(vi, shadowSlot, views_[vi][shadowSlot].CurrentHistorySlot());
    }

    GpuResource& RayTracingShadowManager::GetRawShadowResource(ViewID viewId, uint32_t shadowSlot)
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return Slot(static_cast<uint32_t>(viewId), shadowSlot, TextureSlot::Raw);
    }

    GpuResource& RayTracingShadowManager::GetHistoryResource(ViewID viewId, uint32_t shadowSlot)
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        return Slot(vi, shadowSlot, views_[vi][shadowSlot].CurrentHistorySlot());
    }

    const RayTracingDispatchInfo& RayTracingShadowManager::GetDispatchInfo(
        ViewID viewId, uint32_t shadowSlot) const
    {
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return views_[static_cast<uint32_t>(viewId)][shadowSlot].dispatchInfo;
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
        // 指定ビュー・ライトの現在のシャドウ出力テクスチャを返す。
        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        return Slot(static_cast<uint32_t>(viewId), shadowSlot, TextureSlot::Mask);
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

        // 出力テクスチャの確保（リサイズ・解像度モード変更に対応）
        if (!EnsureOutputTexture(width, height, traceWidth, traceHeight, traceScale, vi, shadowSlot)) {
            view.dispatchInfo.status = RayTracingDispatchStatus::OutputAllocationFailed;
            return;
        }
        view.dispatchInfo.outputSrvPtr = SlotSRV(vi, shadowSlot, TextureSlot::Mask).ptr;

        // 別のライトがこの番号へ来たとき・前のフレームに撃っていないときは履歴を捨てる
        const uint64_t frameNumber = dxCommon_->Frame().FrameNumber();
        const bool continuous =
            (frameNumber == view.lastDispatchFrame) || (frameNumber == view.lastDispatchFrame + 1);
        if (view.lightKey != light.key || !continuous) {
            view.isHistoryValid = false;
        }
        view.lightKey = light.key;
        view.lastDispatchFrame = frameNumber;

        // フレームを 1 つ進める（履歴の書き込み先を入れ替え、サンプル位置を巡回させる）
        // カウンタは view × light ごと（共有だと 2x2 巡回が欠ける。ShadowView 側コメント参照）
        view.historyParity ^= 1u;
        if (traceScale > 1 && settings_.cycleTraceOffset) {
            const UINT phase = RTDebug::NoiseFrame(view.frameCount) & 3u;
            view.traceOffsetX = kTraceOffsetTable[phase][0];
            view.traceOffsetY = kTraceOffsetTable[phase][1];
        } else if (traceScale > 1) {
            // 巡回しない: 常に 2x2 の中央代表点を撃つ。
            // 後段（Temporal / A-Trous / Resolve）の TraceToFull と完全に一致するので、
            // 「トレースした点」と「幾何ガイドを読んだ点」がずれない。
            // 巡回させると、影の縁ではフレームごとに別のサブピクセル（＝別の遮蔽率）を
            // 撃つことになり、テンポラル蓄積の入力が 4 フレーム周期で 0↔1 近く振動する。
            // クランプ棄却で蓄積カウントが落ち、縁が這うように動く主因だった。
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
        constants.historyValid = (view.isHistoryValid && !settings_.disableHistory) ? 1 : 0;
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

        {
            BarrierBatch batch(cmdList);
            batch.Transition(Slot(vi, shadowSlot, TextureSlot::Raw),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            // 前フレームの履歴（ペナンブラ適応サンプリングの判定に読む）
            batch.Transition(Slot(vi, shadowSlot, view.PreviousHistorySlot()),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // 差し方は RootSlot の種別から ShaderBinder が決める（番号を直接触らない）
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[RTShadowBind::gShadowOutput], SlotUAV(vi, shadowSlot, TextureSlot::Raw));
        binder.Set(bindings_[RTShadowBind::gScene], asMgr_->GetTLASSRVHandle());
        binder.Set(bindings_[RTShadowBind::gSceneDepth], sceneDepthSRV);
        binder.Set(bindings_[RTShadowBind::gNormalRoughness], normalRoughnessSRV);
        binder.Set(bindings_[RTShadowBind::gShadowHistory], SlotSRV(vi, shadowSlot, view.PreviousHistorySlot()));
        binder.Set(bindings_[RTShadowBind::ShadowRayConstants], dxCommon_->GetUploadRing().AllocateConstants(constants));
        binder.ValidateBeforeDraw(bindings_);

        // DispatchRays はトレース解像度で行う（ハーフ解像度ならレイ本数は 1/4）
        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(traceWidth, traceHeight);
        cmdList4->DispatchRays(&dispatchDesc);

        // ApplyTemporal の読み取り前に完了を保証してから SRV 状態へ
        BarrierBatch rawToSrv(cmdList);
        rawToSrv.UAV(Slot(vi, shadowSlot, TextureSlot::Raw));
        rawToSrv.Transition(Slot(vi, shadowSlot, TextureSlot::Raw),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        rawToSrv.Flush();

        view.dispatchedThisFrame = true;
        view.dispatchInfo.status = RayTracingDispatchStatus::Dispatched;
        view.dispatchInfo.rayCount =
            static_cast<uint64_t>(traceWidth) * static_cast<uint64_t>(traceHeight)
            * static_cast<uint64_t>(numSamples);

        if (dispatchLogCount_ < 10) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "RTShadow::Dispatch complete viewId={} shadowSlot={} srvHandle=0x{:X}",
                vi, shadowSlot, SlotSRV(vi, shadowSlot, TextureSlot::Mask).ptr);
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
        uint32_t shadowSlot)
    {
        if (!isInitialized_ || !temporalPass_.initialized) return;

        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][shadowSlot];
        if (!view.dispatchedThisFrame) return;

        const TextureSlot currentHistory = view.CurrentHistorySlot();
        const TextureSlot previousHistory = view.PreviousHistorySlot();

        GpuResource& rawRes = Slot(vi, shadowSlot, TextureSlot::Raw);
        GpuResource& prevRes = Slot(vi, shadowSlot, previousHistory);
        GpuResource& curRes = Slot(vi, shadowSlot, currentHistory);
        if (!rawRes.IsValid() || !prevRes.IsValid() || !curRes.IsValid()) return;

        // 入出力の状態遷移（3リソースを一括バリア）
        {
            BarrierBatch batch(cmdList);
            batch.Transition(rawRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(prevRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(curRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetComputeRootSignature(temporalPass_.GetRootSignature());
        cmdList->SetPipelineState(temporalPass_.pipelineState.Get());

        namespace Bind = RTShadowTemporalBind;
        const BindingTable& table = temporalPass_.bindings;
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);

        binder.Set(table[Bind::gRawShadow], SlotSRV(vi, shadowSlot, TextureSlot::Raw));
        binder.Set(table[Bind::gGBufferNormal], normalRoughnessSRV);
        binder.Set(table[Bind::gGBufferDepth], sceneDepthSRV);
        binder.Set(table[Bind::gHistoryShadow], SlotSRV(vi, shadowSlot, previousHistory));
        binder.Set(table[Bind::gMotionVector], motionVectorSRV);
        binder.Set(table[Bind::gOutputShadow], SlotUAV(vi, shadowSlot, currentHistory));

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
        tc.clampSigmaScale = settings_.historyClampSigma;
        // 帯の固定下駄。σ が本当に 0 になる領域（完全な本影・完全な日向）でも
        // 量子化とガイドの揺らぎぶんは許容しておく。
        tc.clampMargin = 0.02f;
        tc.depthTolerance = settings_.historyDepthTolerance;
        tc.stillnessSigma = settings_.stillnessSigma;
        binder.SetConstants(table[Bind::TemporalConstants], tc);

        binder.ValidateBeforeDraw(table);
        cmdList->Dispatch(DispatchGroupCount(view.traceWidth), DispatchGroupCount(view.traceHeight), 1);

        // Denoise が SRV として読むので完了を保証してから遷移
        BarrierBatch curToSrv(cmdList);
        curToSrv.UAV(curRes);
        curToSrv.Transition(curRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        curToSrv.Flush();

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
        uint32_t shadowSlot)
    {
        if (!isInitialized_) return;

        shadowSlot = (std::min)(shadowSlot, kMaxShadowSlots - 1);
        const uint32_t vi = static_cast<uint32_t>(viewId);
        auto& view = views_[vi][shadowSlot];
        if (!view.dispatchedThisFrame) return;

        // A-Trous: ステップ幅 1 → 2 → 4 → 8
        static constexpr int kSteps[kMaxAtrousPassCount] = { 1, 2, 4, 8 };
        // 法線重みの指数はパスに依らず一定にする。旧実装は 4→8→16→32 と厳しくしていたが、
        // ステップ幅が広い後段ほど離れたタップを見るため、曲面ではわずかな向きの差でも
        // 重みが消え「低周波ノイズを均すはずの広いパスだけが働かない」状態になっていた。
        static constexpr float kPhiNormal = 16.0f;

        // デノイズ PSO が作れていない場合はパス数 0 として扱う
        // （その場合でも解決パスがテンポラル結果を Mask へ運ぶので影は消えない）
        const int numPasses = denoisePass_.initialized ? GetEffectiveAtrousPassCount() : 0;

        // A-Trous は 4 チャンネル信号（値/分散/N/深度）を読み書きするので、
        // 1 チャンネルの Mask へ直接書くことはできない。解決パスが必ず最後に走る。
        TextureSlot lastSlot = view.CurrentHistorySlot();

        if (numPasses > 0) {
            cmdList->SetComputeRootSignature(denoisePass_.GetRootSignature());
            cmdList->SetPipelineState(denoisePass_.pipelineState.Get());

            namespace Bind = RTShadowDenoiseBind;
            const BindingTable& table = denoisePass_.bindings;

            const UINT groupX = DispatchGroupCount(view.traceWidth);
            const UINT groupY = DispatchGroupCount(view.traceHeight);

            for (int pass = 0; pass < numPasses; ++pass)
            {
                const TextureSlot inSlot = lastSlot;
                const TextureSlot outSlot = (pass % 2 == 0) ? TextureSlot::DenoiseA : TextureSlot::DenoiseB;

                GpuResource& inputRes = Slot(vi, shadowSlot, inSlot);
                GpuResource& outputRes = Slot(vi, shadowSlot, outSlot);
                if (!inputRes.IsValid() || !outputRes.IsValid()) return;

                // 入力: SRV へ、出力: UAV へ（2リソースを一括バリア）
                {
                    BarrierBatch batch(cmdList);
                    batch.Transition(inputRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    batch.Transition(outputRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }

                // 差し忘れ検出のビットをパス単位で見るため、バインダーはパスごとに作り直す
                ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
                binder.Set(table[Bind::gInputShadow], SlotSRV(vi, shadowSlot, inSlot));
                binder.Set(table[Bind::gNormalRoughness], normalRoughnessSRV);
                binder.Set(table[Bind::gSceneDepth], sceneDepthSRV);
                binder.Set(table[Bind::gOutputShadow], SlotUAV(vi, shadowSlot, outSlot));

                DenoiseConstants dc{};
                dc.stepSize = kSteps[pass];
                dc.phiShadow = settings_.denoisePhiShadow;
                dc.phiNormal = kPhiNormal;
                dc.phiDepth = settings_.denoisePhiDepth;
                dc.traceWidth = static_cast<int>(view.traceWidth);
                dc.traceHeight = static_cast<int>(view.traceHeight);
                ExtractProjectionZW(projection, dc.projM33, dc.projM43);
                dc.traceScale = static_cast<int>(view.traceScale);
                dc.traceOffsetX = static_cast<int>(view.traceOffsetX);
                dc.traceOffsetY = static_cast<int>(view.traceOffsetY);
                dc.fullWidth = static_cast<int>(view.width);
                dc.fullHeight = static_cast<int>(view.height);
                binder.SetConstants(table[Bind::DenoiseConstants], dc);

                binder.ValidateBeforeDraw(table);
                cmdList->Dispatch(groupX, groupY, 1);

                Barrier::UAV(cmdList, outputRes);
                lastSlot = outSlot;
            }
        }

        // トレース解像度の最終結果をフル解像度の Mask へ解決する
        // （等倍のときは解決パスが 1:1 の写しになる）。
        ResolveToFullResolution(cmdList, normalRoughnessSRV, sceneDepthSRV, projection,
            vi, shadowSlot, lastSlot);

        // 後段（DeferredLighting）はピクセルシェーダから読む
        Barrier::Transition(cmdList, Slot(vi, shadowSlot, TextureSlot::Mask),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
        uint32_t shadowSlot,
        TextureSlot sourceSlot)
    {
        // ハーフ解像度で解いた影を、法線と深度をガイドにフル解像度へバイラテラルアップサンプルする
        if (!resolvePass_.initialized) return;

        auto& view = views_[viewIndex][shadowSlot];
        GpuResource& sourceRes = Slot(viewIndex, shadowSlot, sourceSlot);
        GpuResource& maskRes = Slot(viewIndex, shadowSlot, TextureSlot::Mask);
        if (!sourceRes.IsValid() || !maskRes.IsValid()) return;

        {
            BarrierBatch batch(cmdList);
            batch.Transition(sourceRes, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(maskRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        cmdList->SetComputeRootSignature(resolvePass_.GetRootSignature());
        cmdList->SetPipelineState(resolvePass_.pipelineState.Get());

        namespace Bind = RTShadowResolveBind;
        const BindingTable& table = resolvePass_.bindings;
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);

        binder.Set(table[Bind::gTraceShadow], SlotSRV(viewIndex, shadowSlot, sourceSlot));
        binder.Set(table[Bind::gSceneDepth], sceneDepthSRV);
        binder.Set(table[Bind::gNormalRoughness], normalRoughnessSRV);
        binder.Set(table[Bind::gOutputShadow], SlotUAV(viewIndex, shadowSlot, TextureSlot::Mask));

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
        binder.SetConstants(table[Bind::ResolveConstants], rc);

        binder.ValidateBeforeDraw(table);
        cmdList->Dispatch(DispatchGroupCount(view.width), DispatchGroupCount(view.height), 1);

        Barrier::UAV(cmdList, maskRes);
    }
}
