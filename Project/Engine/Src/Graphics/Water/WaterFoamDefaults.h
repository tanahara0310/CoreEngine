#pragma once

// ============================================================
// 泡（whitecap）パラメータの既定値 —— 唯一の情報源
// ------------------------------------------------------------
// 泡の値は CVar → WaterFrameConstants → FFTOceanManager::FoamSettings →
// FoamConstants と 4 段のバケツリレーで運ばれる。各段の構造体が「自前の
// 初期値」を持っていたため、リテラルが 4 か所へ散らばっていた。
//
// 実際に事故が起きた: r.Water.Foam.Bias の既定値だけが旧 Water.json から
// 引き継いだ調整値 0.806 のまま残り、他の 3 か所（設計値 0.85）と食い違った。
// 実行時は CVar が毎フレーム上書きするので描画には出ず、コードを読んだ人が
// 「較正値は 0.806 なのか 0.85 なのか」を判断できないという形でだけ現れる。
// r.Water.Foam.DecaySeconds も同様に 3.07 と 3.0 で割れていた。
//
// そこでリテラルをここへ集約し、全段がこの定数を参照する形にした。
// 値を変えるときはここだけを直せばよく、片側だけ直して割れることが構造上ない。
//
// 依存ゼロのリーフヘッダーにしてあるのは、シミュレーション層
// （FFTOceanManager）から描画層（Surface/WaterSurfaceTypes.h）へ include が
// 逆流するのを避けるため。
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
