#pragma once

#ifdef _DEBUG

#include "ICameraEditorModule.h"

namespace CoreEngine
{
    /// @brief カメラ一覧の表示とアクティブ切り替えを担当する最小モジュール
    class CameraListEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "カメラ一覧"; }

        /// @brief 毎フレーム更新（現時点では状態更新なし）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;
    };
}

#endif // _DEBUG
