#pragma once

#include <cstdint>

/// @brief 水面デバッグ可視化モード
enum class WaterDebugViewMode : uint32_t {
    None = 0,
    RawDepth = 1,
    LinearDepth = 2,
    DepthDelta = 3,
    ScreenUV = 4,
    SceneColor = 5,
    Reflection = 6,
    Fresnel = 7,
    RTRefraction = 8,
    RTRefractionReason = 9,
    RTRefractionVsScene = 10,
    Transmission = 11,
    Transmittance = 12,
    Reflectance = 13,
    WaterComposite = 14,
    RTRefractionSuccessMask = 15,
    FFTOceanJacobian = 16,
    PlanarReflectionRaw = 17,
    SkyEnvCloudColor = 18,
    CloudOverlayWeight = 19,
    // まだら診断: フレネル混合を強制して各端点を単独表示する
    CompositeTransmissionOnly = 20, // reflectanceWeight=0 の最終合成（透過のみ）
    CompositeReflectionOnly = 21,   // reflectanceWeight=1 の最終合成（反射のみ）
    ReflectionMinusTransmission = 22, // |反射 - 透過| ×3（斑を生む輝度差の分布）
    FFTOceanFoam = 23, // 泡マスク（グレースケール。foamBias/foamGain の較正用）

    Count, // 末尾に追加すること（名前テーブルの static_assert が個数一致を検証する）
};

/// @brief デバッグ表示モードの UI 表示名（enum 値と添字が一致）
/// @details Water.Debug.hlsli の分岐・この enum・この名前テーブルの 3 つは
///          モード追加時に必ず同時更新する。以前は名前テーブルが
///          Application 側（WaterSurfaceDebugPanel.cpp）にあり、追記漏れが
///          静かに起きる構造だった（TAA の kTechniqueNames 追記漏れと同型）。
inline const char* const kWaterDebugViewModeNames[] = {
    "なし",
    "生の深度",
    "線形深度",
    "深度差",
    "スクリーンUV",
    "シーンカラー",
    "反射",
    "フレネル",
    "RT屈折",
    "RT屈折理由",
    "RT屈折とシーン比較",
    "透過光",
    "波長別透過率",
    "反射率",
    "最終合成",
    "RT屈折成功マスク",
    "FFT Jacobian",
    "平面反射(生・雲なし)",
    "雲キューブマップ色",
    "雲上書き強度",
    "合成:透過のみ(Fresnel=0)",
    "合成:反射のみ(Fresnel=1)",
    "反射-透過の差分",
    "FFT 泡マスク",
};

static_assert(
    sizeof(kWaterDebugViewModeNames) / sizeof(kWaterDebugViewModeNames[0])
        == static_cast<uint32_t>(WaterDebugViewMode::Count),
    "kWaterDebugViewModeNames must have one entry per WaterDebugViewMode value");
