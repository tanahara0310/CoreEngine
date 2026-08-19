#pragma once

// ============================================================
// 泡（whitecap）パラメータの既定値 —— 唯一の情報源
// ------------------------------------------------------------
// 泡の値は CVar → WaterFrameConstants → FoamSettings → FoamConstants と
// 4 段で運ばれる。各段が自前の初期値を持つと片側だけ直して割れるため、
// リテラルはここへ集約し、全段がこの定数を参照する。
// 依存ゼロのリーフヘッダーにしてあるのは、シミュレーション層から
// 描画層へ include が逆流するのを避けるため。
// ============================================================

namespace CoreEngine
{
    namespace WaterFoamDefaults
    {
        /// @brief 泡（whitecap / 岸際泡）を有効にするか
        inline constexpr bool kEnabled = true;

        /// @brief 発生しきい値。合成ヤコビアン detJ がこれを下回ると泡が立つ
        /// @details Tessendorf 系実装の常用域 0.7〜1.0。値域ログの実測から 0.85 に較正した。
        ///          風速依存は Bias ではなく foamWindCoverageScale（Monahan W ∝ U^3.41）が担う。
        inline constexpr float kBias = 0.85f;

        /// @brief しきい値からの立ち上がり勾配
        inline constexpr float kGain = 4.0f;

        /// @brief 泡レイヤの不透明度（1.0 の白ベタは禁止・水面下の情報を残す）
        inline constexpr float kOpacity = 0.9f;

        /// @brief カスケード別の勾配寄与の重み (大 / 中 / 小)
        /// @details 飽和対策の較正値。勾配は波数 k に比例するため、無重みだと最小カスケード
        ///          （31m）が支配して detJ が広範囲で飽和する（Phase 0 実測）。
        inline constexpr float kCascadeWeights[3] = { 1.0f, 0.5f, 0.2f };

        /// @brief 泡の寿命 τ [s]（e^-1 減衰時間）
        inline constexpr float kDecaySeconds = 3.0f;
    }
}
