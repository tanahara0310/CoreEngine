#pragma once

#ifdef USE_IMGUI

#include "ICameraEditorModule.h"

namespace CoreEngine
{
    /// @brief アクティブカメラのコントローラ設定を表示・編集するモジュール
    /// @details 入力処理そのものはコントローラ（FreeLookController / OrbitFlyController）が持つ。
    ///          このモジュールは「どのコントローラが付いているか」と、その設定値の UI だけを担当する。
    class CameraGameViewControlModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "カメラ操作"; }

        /// @brief 毎フレーム更新（このモジュールは状態を持たない）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;
    };
}

#endif // USE_IMGUI
