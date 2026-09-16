#pragma once

#ifdef USE_IMGUI

#include "Math/Vector/Vector3.h"

#include <functional>
#include <string>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;

    /// @brief エディタからオブジェクトを作る・複製する・消す操作（どれも Undo に積む）
    namespace ObjectEditing
    {
        /// @brief 操作に要るもの
        struct Context
        {
            /// @brief 操作するシーンのオブジェクト
            GameObjectManager* manager = nullptr;

            /// @brief オブジェクトに削除の印を付ける直前に呼ぶ（選択の解除などに使う）
            std::function<void(const GameObject&)> beforeDestroy;
        };

        /// @brief 複製・削除できるか
        /// @param reason できないときの理由を書く先（要らなければ nullptr）
        /// @note 保存形から作り直せるものだけを扱う。コードが付けたコンポーネントを持つものは、
        ///       コードが作ったオブジェクトとみなして扱わない。
        bool CanDuplicateOrDelete(const GameObject& object, std::string* reason = nullptr);

        /// @brief Transform だけを持つ空のオブジェクトを作る
        /// @return 作ったオブジェクト（作れなければ nullptr）
        GameObject* CreateEmpty(const Context& context, const Vector3& position);

        /// @brief オブジェクトを複製する（名前は「名前 (1)」、位置は X へ 1 ずらす）
        /// @return 複製したオブジェクト（複製できなければ nullptr）
        GameObject* Duplicate(const Context& context, const GameObject& source);

        /// @brief オブジェクトを消す
        /// @return 消したら true
        /// @note Undo で、同じ ID・保存キー・値のオブジェクトを作り直す。
        bool Delete(const Context& context, GameObject& object);
    }
}

#endif // USE_IMGUI
