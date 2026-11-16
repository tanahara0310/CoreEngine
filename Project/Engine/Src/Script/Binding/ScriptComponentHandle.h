#pragma once

#include "GameObject/GameObject.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Script/Binding/ScriptRefCounted.h"

namespace CoreEngine::Script
{
    /// @brief スクリプトへ渡す、GameObject に付いたコンポーネントのハンドル
    /// @tparam Derived 継ぐクラス自身
    /// @tparam Component 指すコンポーネントの型
    /// @details GameObject のハンドルの参照を 1 つ持ち、使うたびにコンポーネントを引き直す。
    ///          スクリプトへ登録するメソッド（exists・gameObject など）は継ぐクラスに書く。
    template <class Derived, class Component>
    class ScriptComponentHandle : public ScriptRefCounted<Derived>
    {
    protected:
        explicit ScriptComponentHandle(ScriptGameObject& owner) : owner_(owner) { owner_.AddRef(); }
        ~ScriptComponentHandle() { owner_.Release(); }

        /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
        ScriptGameObject* AcquireOwner() const
        {
            owner_.AddRef();
            return &owner_;
        }

        /// @brief 持ち主の GameObject に付いたコンポーネント（GameObject が無いか付いていなければ nullptr）
        Component* Find() const
        {
            GameObject* const object = owner_.Resolve();
            return object ? object->GetComponent<Component>() : nullptr;
        }

        ScriptGameObject& owner_;
        mutable bool warned_ = false;
    };
}
