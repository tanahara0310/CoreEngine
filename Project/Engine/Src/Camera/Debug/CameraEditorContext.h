#pragma once

#ifdef _DEBUG

namespace CoreEngine
{
    class CameraManager;

    /// @brief カメラエディター各モジュールへ渡す共通コンテキスト
    struct CameraEditorContext {
        // エディター基盤段階では最低限の依存のみ公開し、
        // 新エディター機能の追加に合わせて必要な依存を段階的に拡張する。
        CameraManager* cameraManager = nullptr;
    };
}

#endif // _DEBUG
