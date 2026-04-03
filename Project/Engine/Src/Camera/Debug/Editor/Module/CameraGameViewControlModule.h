#pragma once

#ifdef USE_IMGUI

#include "ICameraEditorModule.h"

namespace CoreEngine
{
    /// @brief デバッグカメラのGameビュー操作設定を管理するモジュール
    class CameraGameViewControlModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "Gameビュー操作"; }

        /// @brief 毎フレーム更新
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;
    };
}

#endif // USE_IMGUI
