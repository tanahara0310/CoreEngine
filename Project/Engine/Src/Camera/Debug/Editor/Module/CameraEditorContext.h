#pragma once

#ifdef _DEBUG

namespace CoreEngine
{
    class CameraManager;
    class GameObjectManager;

    /// @brief カメラエディター各モジュールへ渡す共通コンテキスト
    struct CameraEditorContext {

        CameraManager* cameraManager = nullptr;
        GameObjectManager* gameObjectManager = nullptr;
    };
}

#endif // _DEBUG
