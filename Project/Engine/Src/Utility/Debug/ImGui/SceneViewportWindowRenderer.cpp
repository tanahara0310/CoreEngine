#include "SceneViewportWindowRenderer.h"

#include "WinApp/WinApp.h"

namespace CoreEngine
{
    SceneViewportWindowResult SceneViewportWindowRenderer::DrawWindow(
        const char* windowName,
        D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
        const std::function<void(const SceneViewportWindowResult&)>& postImageDraw)
    {
        SceneViewportWindowResult result{};

        // Scene/Gameウィンドウの基本レイアウトを共通化し、描画ロジックを一箇所へ集約する。
        if (!ImGui::Begin(windowName, nullptr,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground)) {
            ImGui::End();
            return result;
        }

        result.isWindowOpen = true;

        // コンテンツ領域サイズからアスペクトを維持した描画サイズを算出する。
        ImVec2 contentRegionSize = ImGui::GetContentRegionAvail();
        const float aspect = static_cast<float>(WinApp::GetCurrentClientWidthStatic()) /
            static_cast<float>(WinApp::GetCurrentClientHeightStatic());

        float drawW = contentRegionSize.x;
        float drawH = drawW / aspect;
        if (drawH > contentRegionSize.y) {
            drawH = contentRegionSize.y;
            drawW = drawH * aspect;
        }

        // 画像を中央に寄せるためのオフセットを適用する。
        ImVec2 contentPos = ImGui::GetCursorScreenPos();
        float offsetX = (contentRegionSize.x - drawW) * 0.5f;
        float offsetY = (contentRegionSize.y - drawH) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(contentPos.x + offsetX, contentPos.y + offsetY));

        ImTextureID texID = (ImTextureID)textureHandle.ptr;
        ImGui::Image(texID, ImVec2(drawW, drawH));

        result.hasImage = true;
        result.imageMin = ImGui::GetItemRectMin();
        result.imageMax = ImGui::GetItemRectMax();
        result.imageSize = ImVec2(result.imageMax.x - result.imageMin.x, result.imageMax.y - result.imageMin.y);
        result.isImageHovered = ImGui::IsItemHovered();

        // ウィンドウが有効な間にオーバーレイやギズモUIを重ねる。
        if (postImageDraw) {
            postImageDraw(result);
        }

        ImGui::End();
        return result;
    }
}
