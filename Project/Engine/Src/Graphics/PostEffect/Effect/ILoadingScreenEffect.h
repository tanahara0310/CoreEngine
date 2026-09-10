#pragma once

namespace CoreEngine
{
    /// @brief シーン遷移がローディング画面を駆動するための口
    /// @details SceneTransition は「今どれだけ表示すべきか」と「読み込みの進捗」だけを
    ///          伝える。何を描くかは実装側の自由なので、見た目を差し替えても
    ///          SceneTransition のフェーズ機械には手を入れずに済む。
    /// @note 実装は必ず PostEffectBase の派生でもあること。差し替えは
    ///       SceneTransition::SetLoadingScreen(名前) が dynamic_cast で解決する。
    class ILoadingScreenEffect {
    public:
        virtual ~ILoadingScreenEffect() = default;

        /// @brief 表示強度を設定する（0.0 = 非表示, 1.0 = 完全表示）
        virtual void SetScreenAlpha(float alpha) = 0;

        /// @brief シーン読み込みの進捗を設定する（0.0〜1.0）
        virtual void SetProgress(float progress) = 0;

        /// @brief 進捗ゲージの表示強度を設定する（0.0〜1.0）
        /// @note 進捗の見せ方は実装に任せるので、ゲージを持たない実装では無視してよい
        virtual void SetGaugeAlpha(float alpha) = 0;

        /// @brief エフェクトそのものの有効・無効を切り替える
        /// @details PostEffectBase::SetEnabled への転送。この口だけ見て
        ///          チェーンから外せるようにしておく（PostEffectBase へ
        ///          ダウンキャストし直さずに済む）
        virtual void SetLoadingEnabled(bool enabled) = 0;
    };
}
