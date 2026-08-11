#pragma once
#include <cstdint>

namespace CoreEngine {

    /// @brief ポストエフェクトが属するパイプライン段
    /// @details チェーンは SceneHDR → Tonemap → PostTonemap の順に単調でなければならない。
    ///          「光学現象・露出・グレーディングはトーンマップ前、記録と演出はトーンマップ後」
    ///          という原則を型で表現し、並び順の事故を起動時に検出できるようにする。
    ///          この規約は PostEffectManager::ValidateChain() が検証する。
    ///          設計: Docs/Engine/Graphics/PostProcess/PostEffect_Refactoring_Plan.md
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
