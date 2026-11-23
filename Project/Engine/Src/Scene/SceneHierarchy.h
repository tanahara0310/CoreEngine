#pragma once

#include <vector>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;

    /// @brief オブジェクトの親子（トランスフォームと UI トランスフォームの親）をたどる
    namespace SceneHierarchy
    {
        /// @brief 親のオブジェクト
        /// @return トランスフォームを持てばその親、持たなければ UI トランスフォームの親（無ければ nullptr）
        GameObject* FindParent(const GameObject& object);

        /// @brief object が root の子孫か（root 自身は false）
        bool IsDescendantOf(const GameObject& object, const GameObject& root);

        /// @brief 子孫を集める（root は含めない）
        /// @return 親が子より先、同じ深さはシーンの並びの順
        /// @note 削除の印が付いたオブジェクトは含めない。
        std::vector<GameObject*> CollectDescendants(const GameObjectManager& manager, const GameObject& root);

        /// @brief 削除の印が付いたオブジェクトの子孫にも印を付ける
        void MarkDescendantsOfDestroyed(GameObjectManager& manager);
    }
}
