#pragma once

#include <imgui.h>
#include <d3d12.h>
#include <functional>

namespace CoreEngine
{
    /// @brief Scene/Gameビューポート描画結果（レイアウト情報）
    struct SceneViewportWindowResult {
        bool isWindowOpen = false;
        bool hasImage = false;
        ImVec2 imageMin{};
        ImVec2 imageMax{};
        ImVec2 imageSize{};
        bool isImageHovered = false;
    };

    /// @brief SceneViewportのウィンドウ描画責務を分離するレンダラー
    class SceneViewportWindowRenderer {
    public:
        /// @brief 指定ウィンドウへテクスチャをアスペクト維持で描画する
        /// @param windowName 描画先ウィンドウ名
        /// @param textureHandle 描画するテクスチャ
        /// @param postImageDraw 画像描画後、同一ウィンドウ内で追加描画したい処理
        /// @return 画像領域の位置/サイズ/ホバー状態
        SceneViewportWindowResult DrawWindow(
            const char* windowName,
            D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
            const std::function<void(const SceneViewportWindowResult&)>& postImageDraw = nullptr);
    };
}
