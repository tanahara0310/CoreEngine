#pragma once

#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"
#include <cstdint>

/// @brief Gerstner Wave 1 本分のパラメータ
/// @note HLSL 側の WaveParams 構造体とメモリレイアウトを一致させること
struct WaveParams {
    CoreEngine::Vector2 direction = { 1.0f, 0.0f }; ///< 進行方向（XZ, 正規化済み）
    float amplitude = 0.5f;  ///< 振幅（波の高さ）
    float wavelength = 10.0f; ///< 波長（山から山の距離）
    float speed = 2.0f;  ///< 位相速度（波が進む速さ）
    float steepness = 0.5f;  ///< 横揺れ係数 Q（0=正弦波, 1=完全Gerstner）
    float padding[2] = {};    ///< 16 バイトアライメント用
};

/// @brief 水面 Gerstner Wave の定数バッファ全体
/// @note HLSL 側の WaterConstants cbuffer とメモリレイアウトを一致させること
struct WaterConstants {
    WaveParams waves[4]; ///< 重ね合わせる波（最大 4 本）
    float      time;     ///< 経過時間（秒）
    float      padding[3] = {}; ///< 16 バイトアライメント用

    static_assert(sizeof(WaveParams) == 32, "WaveParams must be 32 bytes");
};

/// @brief 毎フレーム更新する水面用フレーム定数バッファ
/// クリップ平面（反射パス用）を格納する。HLSL 側の WaterFrameConstants と一致させること
struct WaterFrameConstants {
    /// @brief クリップ平面 (A, B, C, D)。dot(worldPos, clipPlane) > 0 なら描画する
    float clipPlane[4] = { 0.0f, 1.0f, 0.0f, 0.0f };

    /// @brief 1 = クリップ平面を有効にする、0 = 無効
    int   clipEnabled = 0;

    /// @brief 1 = 反射テクスチャ（gReflectionTexture）が有効、0 = IBL フォールバック
    int   reflectionEnabled = 0;

    /// @brief Fresnel が 0（真上から見た時）のときの alpha 値（浅い角度 = 透明）
    float fresnelMinAlpha = 0.05f;

    /// @brief Fresnel が 1（斜めから見た時）のときの alpha 値（浅い角度 = 不透明）
    float fresnelMaxAlpha = 1.0f;

    // ---- Depth Fade（Beer-Lambert 則）----

    /// @brief 光吸収係数（大きいほど短距離で不透明になる）
    float absorptionCoeff = 0.3f;

    /// @brief 1 = 深度テクスチャによる Depth Fade を有効にする、0 = 無効
    int   depthFadeEnabled = 1;

    /// @brief アライメント用パディング
    float padding[2] = {};

    // ---- 浅瀬 / 深場の水色 ----

    /// @brief 浅瀬（d ≈ 0）のときの水色（RGB）
    float shallowColor[3] = { 0.1f, 0.6f, 0.6f };

    /// @brief shallowColor アライメント用
    float shallowColorPad = 0.0f;

    /// @brief 深場（d 大）のときの水色（RGB）
    float deepColor[3] = { 0.02f, 0.1f, 0.2f };

    /// @brief deepColor アライメント用
    float deepColorPad = 0.0f;
};
