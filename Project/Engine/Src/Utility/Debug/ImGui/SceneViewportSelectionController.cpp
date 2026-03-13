#include "SceneViewportSelectionController.h"

#include "ObjectSelector.h"
#include "ObjectCommon/GameObjectManager.h"
#include "Camera/ICamera.h"

namespace CoreEngine
{
    void SceneViewportSelectionController::UpdateObjectSelection(
        ObjectSelector* objectSelector,
        GameObjectManager* gameObjectManager,
        const ICamera* camera,
        const ImVec2& viewportPos,
        const ImVec2& viewportSize,
        bool isViewportHovered)
    {
        if (!objectSelector || !gameObjectManager || !camera) {
            return;
        }

        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
            return;
        }

        // マウス座標をビューポート正規化座標へ変換して選択更新に渡す。
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector2 normalizedMousePos = Vector2(
            (mousePos.x - viewportPos.x) / viewportSize.x,
            (mousePos.y - viewportPos.y) / viewportSize.y);

        objectSelector->Update(gameObjectManager, camera, normalizedMousePos, isViewportHovered);
    }

    void SceneViewportSelectionController::UpdateSpriteSelection(
        ObjectSelector* objectSelector,
        GameObjectManager* gameObjectManager,
        const ICamera* camera,
        const ImVec2& viewportPos,
        const ImVec2& viewportSize,
        bool isViewportHovered)
    {
        if (!objectSelector || !gameObjectManager || !camera) {
            return;
        }

        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
            return;
        }

        // マウス座標をビューポート正規化座標へ変換して2D選択更新に渡す。
        ImVec2 mousePos = ImGui::GetMousePos();
        Vector2 normalizedMousePos = Vector2(
            (mousePos.x - viewportPos.x) / viewportSize.x,
            (mousePos.y - viewportPos.y) / viewportSize.y);

        objectSelector->Update2D(gameObjectManager, camera, normalizedMousePos, isViewportHovered);
    }
}
