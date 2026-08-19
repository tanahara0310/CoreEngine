#pragma once
#include <cstdint>

namespace CoreEngine {

    /// @brief ポストエフェクトが属するパイプライン段
    /// @details 「光学現象・露出・グレーディングはトーンマップ前、記録と演出は後」という原則を型で表す。
    ///          単調性は PostEffectManager::ValidateChain() が検証する。
    enum class PostEffectStage : uint8_t {
        SceneHDR = 0,   ///< トーンマップ前。ブルーム・レンズフレア・色収差・口径食・グレーディング
        Tonemap,        ///< トーンマッパ本体。チェーン中ちょうど 1 つだけ存在する
        PostTonemap,    ///< トーンマップ後。フィルムグレイン・ディザ・UI 演出
    };

    /// @brief 段の表示名を返す（ログ・ImGui 用）
    /// @param stage 対象の段
    /// @return 段の名前。未知の値は "?"
    constexpr const char* ToString(PostEffectStage stage) noexcept
    {
        switch (stage) {
        case PostEffectStage::SceneHDR:    return "SceneHDR";
        case PostEffectStage::Tonemap:     return "Tonemap";
        case PostEffectStage::PostTonemap: return "PostTonemap";
        default:                           return "?";
        }
    }

    /// @brief 段の総数（集計配列のサイズに使う）
    inline constexpr size_t kPostEffectStageCount = 3;
}
