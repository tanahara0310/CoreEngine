#pragma once

#include <imgui.h>

namespace CoreEngine
{
    class ObjectSelector;
    class GameObjectManager;
    class ICamera;

    /// @brief SceneViewport上の選択更新責務を分離するコントローラー
    class SceneViewportSelectionController {
    public:
        /// @brief 3Dオブジェクト選択を更新
        void UpdateObjectSelection(
            ObjectSelector* objectSelector,
            GameObjectManager* gameObjectManager,
            const ICamera* camera,
            const ImVec2& viewportPos,
            const ImVec2& viewportSize,
            bool isViewportHovered);

        /// @brief 2Dスプライト選択を更新
        void UpdateSpriteSelection(
            ObjectSelector* objectSelector,
            GameObjectManager* gameObjectManager,
            const ICamera* camera,
            const ImVec2& viewportPos,
            const ImVec2& viewportSize,
            bool isViewportHovered);
    };
}
