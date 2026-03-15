#pragma once

#ifdef _DEBUG

#include "ICameraEditorModule.h"

namespace CoreEngine
{
    /// @brief アクティブカメラの投影パラメータ（FOV/Clip/Aspect）を編集するモジュール
    class CameraParametersEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "パラメータ"; }

        /// @brief 毎フレーム更新（現時点では状態更新なし）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;
    };
}

#endif // _DEBUG
