#pragma once

#ifdef USE_IMGUI

#include <string>

namespace CoreEngine
{
    class GameObject;
    class IComponent;

    /// @brief インスペクタからコンポーネントを足す・外す操作（どちらも Undo に積む）
    namespace ComponentEditing
    {
        /// @brief 型名のコンポーネントを足せるか
        /// @param reason 足せないときの理由を書く先（要らなければ nullptr）
        /// @note ファクトリに載っていて、同じ型がまだ付いていない型だけを足せる。
        bool CanAdd(const GameObject& object, const std::string& typeName, std::string* reason = nullptr);

        /// @brief 型名からコンポーネントを作って足す
        /// @return 足したコンポーネント（足せなければ nullptr）
        /// @note `Awake()` の中で一緒に足されたコンポーネントも、同じ 1 回の Undo で外れる。
        IComponent* Add(GameObject& object, const std::string& typeName);

        /// @brief コンポーネントを外せるか
        /// @param reason 外せないときの理由を書く先（要らなければ nullptr）
        /// @note コードが付けたものと、同じオブジェクトの別のコンポーネントが使っているものは外せない。
        bool CanRemove(const GameObject& object, const IComponent& component, std::string* reason = nullptr);

        /// @brief コンポーネントを外す
        /// @note 実体はオブジェクトが控え、Undo で同じ位置へ付け直す。
        bool Remove(GameObject& object, IComponent& component);

        /// @brief 「＋ コンポーネント追加」ボタンと、足せる型の一覧を今の行の右端に描く
        /// @return 一覧で選ばれた型名（選ばれなければ空）
        std::string DrawAddButton(const GameObject& object);
    }
}

#endif // USE_IMGUI
