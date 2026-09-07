#pragma once

#ifdef USE_IMGUI

#include "CameraEditorContext.h"

namespace CoreEngine { class Camera; }

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

        /// @brief ゲームビューポート上の重ね描き（ギズモ・アイコン）を行う
        /// @param context 共通コンテキスト
        /// @param viewCamera いま覗いているカメラ（射影に使う）
        /// @param viewport ビューポートの位置と大きさ
        /// @details ImGuizmo の準備と描画リストの指定はビューポート側で済んでいる前提。
        ///          タブが選ばれているかに関わらず毎フレーム呼ばれるので、
        ///          出す/出さないの判断はモジュール側で行うこと。
        virtual void DrawViewportOverlay(const CameraEditorContext& context,
            const Camera& viewCamera, const CameraEditorViewport& viewport)
        {
            (void)context;
            (void)viewCamera;
            (void)viewport;
        }
    };
}

#endif // _DEBUG
