#pragma once

#ifdef USE_IMGUI

namespace CoreEngine
{
    class CameraManager;
    class GameObjectManager;
    class EngineSystem;

    /// @brief カメラエディター各モジュールへ渡す共通コンテキスト
    struct CameraEditorContext {

        CameraManager*     cameraManager     = nullptr;
        GameObjectManager* gameObjectManager  = nullptr;
        EngineSystem*      engineSystem       = nullptr;
    };
}

#endif // _DEBUG
