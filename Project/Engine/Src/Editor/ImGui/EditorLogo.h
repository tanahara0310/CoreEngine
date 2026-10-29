#pragma once

#ifdef CORE_EDITOR

#include <d3d12.h>
#include <imgui.h>
#include <wrl/client.h>

#include <array>

namespace CoreEngine
{
    class GraphicsCore;
}

namespace CoreEngine::Editor
{
    /// @brief エンジンのロゴ（`Engine/Assets/Textures/Logo` の PNG）を ImGui で描ける形にして持つ
    /// @details 初めて聞かれたときに読み込んで GPU へ載せ、以後は同じものを返す。暗い下地に置く絵。
    class EditorLogo
    {
    public:
        /// @brief ロゴの組み方
        enum class Kind
        {
            Mark,       ///< マークだけ
            Horizontal, ///< マークの右に文字
            Stacked,    ///< マークの下に文字
        };

        /// @param graphics 読み込んだ絵を置く GPU（ImGui と同じディスクリプタヒープを使うもの）
        explicit EditorLogo(GraphicsCore& graphics);

        /// @brief kind の絵（読めなければ 0）
        ImTextureID Texture(Kind kind);

        /// @brief kind の絵の縦横比（幅 / 高さ）
        float Aspect(Kind kind);

    private:
        /// @brief 読み込んだ絵 1 つ
        struct Image
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            ImTextureID id = 0;
            float aspect = 1.0f;
            bool attempted = false;
        };

        /// @brief kind の絵（まだなら読み込む）
        const Image& Get(Kind kind);

        GraphicsCore& graphics_;
        std::array<Image, 3> images_{};
    };
}

#endif // CORE_EDITOR
