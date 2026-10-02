#pragma once

#include "Utility/Lifetime/ScopedRegistration.h"

namespace CoreEngine {
    class EngineSystem;
    class FogManager;

    /// @brief 高さフォグのエンジン常駐エディタ
    /// @details DebugSubsystem がエンジン寿命で 1 個所有し、シーンに置かれたコンポーネントの
    ///          インスペクタとして中身を描く。
    ///          UI は「① プリセット → ② 詳細設定（CVar 自動生成）」の 2 層。
    class FogEditor {
    public:
        /// @brief 参照先を初期化し、環境エディタとして登録する
        void Initialize(EngineSystem& engine);

    private:
        /// @brief フォグの編集パネル内容を描画する（Inspector 内に埋め込み）
        void DrawContent();

        /// @brief プリセットボタン群を描画する（押した時点で即適用）
        void DrawPresetButtons();

        FogManager* GetFogManager() const;

        EngineSystem* engine_ = nullptr;

        /// @brief インスペクタの出し方の登録（破棄すると外れる）
        ScopedRegistration inspector_;
    };
}
