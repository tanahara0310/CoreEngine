#pragma once

#ifdef USE_IMGUI

namespace CoreEngine
{
    class CameraManager;
    class GameObjectManager;
    class EngineSystem;

    /// @brief ゲームビューポートの位置と大きさ [px]
    /// @details ワールド座標を画面座標へ射影するのに必要。ImGui へ依存させたくないので
    ///          ImVec2 ではなく素の float で持つ。
    struct CameraEditorViewport {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        bool IsValid() const { return width > 0.0f && height > 0.0f; }
    };

    /// @brief カメラエディター各モジュールへ渡す共通コンテキスト
    struct CameraEditorContext {

        CameraManager*     cameraManager     = nullptr;
        GameObjectManager* gameObjectManager  = nullptr;
        EngineSystem*      engineSystem       = nullptr;
    };
}

#endif // _DEBUG
