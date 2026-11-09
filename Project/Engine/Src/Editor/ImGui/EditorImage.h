#pragma once

#ifdef CORE_EDITOR

#include <d3d12.h>
#include <imgui.h>
#include <wrl/client.h>

namespace CoreEngine
{
    class GraphicsCore;
}

namespace CoreEngine::Editor
{
    /// @brief エディタで描く絵 1 枚（GPU に載せたテクスチャと ImGui のテクスチャ ID）
    struct EditorImage
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> texture;
        ImTextureID id = 0;
        float width = 0.0f;
        float height = 0.0f;
    };

    /// @brief エンジンの PNG を sRGB として読み、縮小用のミップを付けて GPU へ載せる
    /// @param graphics 読み込んだ絵を置く GPU（ImGui と同じディスクリプタヒープを使うもの）
    /// @param file プロジェクトからのパス（`Engine/Assets/...`）
    /// @return 読めなければ id = 0
    EditorImage LoadEditorImage(GraphicsCore& graphics, const char* file);
}

#endif // CORE_EDITOR
