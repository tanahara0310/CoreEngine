#pragma once

#ifdef _DEBUG

#include "CameraEditorContext.h"

namespace CoreEngine
{
    /// @brief カメラエディター機能を差し替え/追加可能にするモジュールインターフェース
    class ICameraEditorModule {
    public:
        virtual ~ICameraEditorModule() = default;

        /// @brief タブ名を返す
        virtual const char* GetTabName() const = 0;

        /// @brief フレーム更新（タブ外でも実行したい処理に使用）
        virtual void Update(const CameraEditorContext& context) = 0;

        /// @brief タブ内容を描画
        virtual void Draw(const CameraEditorContext& context) = 0;
    };
}

#endif // _DEBUG
