#pragma once
#include <cstdint>

/// @brief 描画パスタイプ

namespace CoreEngine
{
    /// @brief 描画パスタイプ
    /// @details
    ///  - エンジン組み込みパスは固定値（0..kBuiltInCount）として安定
    ///  - 値 100..999 はユーザー定義パス用に予約
    ///  - 新しい組み込みパスを追加する場合は kBuiltInCount を超えないこと
    /// @note ユーザー拡張パスを追加する際に、本ヘッダの編集を不要にする設計。
    enum class RenderPassType : int32_t {
        Invalid = -1,        ///< 無効

        // ──────────────────────────────────────────────
        // エンジン組み込みパス（0..99）
        // ──────────────────────────────────────────────
        ShadowMap = 0,       ///< [廃止・欠番] 従来型シャドウマップ（2026-07-25削除。既存パス値の安定性のため欠番として保持）
        Model,               ///< 通常モデル
        SkinnedModel,        ///< スキニングモデル
        WaterSurface,        ///< 水面専用 forward パス
        SkyBox,              ///< SkyBox
        ModelParticle,       ///< モデルパーティクル（3D）
        Line,                ///< ライン描画（デバッグ用）
        Particle,            ///< パーティクル（ビルボード）
        Sprite,              ///< スプライト（ゲームワールド）
        UI,                  ///< UI（最前面・スクリーン固定座標）
        GpuParticle,         ///< GPUパーティクル（Compute駆動ビルボード）※既存パスの値を変えないため末尾追加

        kBuiltInCount,       ///< 組み込みパス数

        // ──────────────────────────────────────────────
        // ユーザー定義パス用予約領域（100..999）
        // ──────────────────────────────────────────────
        UserDefinedBegin = 100, ///< ユーザー拡張の開始値
        UserDefinedEnd = 999, ///< ユーザー拡張の終了値
    };

    /// @brief RenderPassType がユーザー定義範囲かどうかを判定
    constexpr bool IsUserDefinedPass(RenderPassType type) {
        const auto v = static_cast<int32_t>(type);
        return v >= static_cast<int32_t>(RenderPassType::UserDefinedBegin)
            && v <= static_cast<int32_t>(RenderPassType::UserDefinedEnd);
    }

    /// @brief RenderPassType が組み込みパスかどうかを判定
    constexpr bool IsBuiltInPass(RenderPassType type) {
        const auto v = static_cast<int32_t>(type);
        return v >= 0 && v < static_cast<int32_t>(RenderPassType::kBuiltInCount);
    }

    /// @brief RenderPassType を文字列に変換（ログ・デバッグ用）
    constexpr const char* RenderPassTypeToString(RenderPassType type) {
        switch (type) {
        case RenderPassType::Invalid:       return "Invalid";
        case RenderPassType::ShadowMap:     return "ShadowMap";
        case RenderPassType::Model:         return "Model";
        case RenderPassType::SkinnedModel:  return "SkinnedModel";
        case RenderPassType::WaterSurface:  return "WaterSurface";
        case RenderPassType::SkyBox:        return "SkyBox";
        case RenderPassType::ModelParticle: return "ModelParticle";
        case RenderPassType::Line:          return "Line";
        case RenderPassType::Particle:      return "Particle";
        case RenderPassType::Sprite:        return "Sprite";
        case RenderPassType::UI:            return "UI";
        case RenderPassType::GpuParticle:   return "GpuParticle";
        default:
            return IsUserDefinedPass(type) ? "UserDefined" : "Unknown";
        }
    }
}
