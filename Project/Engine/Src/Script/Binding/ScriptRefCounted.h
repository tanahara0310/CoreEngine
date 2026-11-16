#pragma once

namespace CoreEngine::Script
{
    /// @brief スクリプトへ渡す参照型の参照カウント（作った時点で 1、0 になったら消える）
    /// @tparam Derived 継ぐクラス自身
    /// @details 登録は BindingRegistrar::RefCountBehaviours<Derived>(型名) で行う。
    /// デストラクタを private にするクラスは `friend class ScriptRefCounted<Derived>;` を書く。
    template <class Derived>
    class ScriptRefCounted
    {
    public:
        ScriptRefCounted(const ScriptRefCounted&) = delete;
        ScriptRefCounted& operator=(const ScriptRefCounted&) = delete;

        void AddRef() const { ++refCount_; }

        void Release() const
        {
            if (--refCount_ == 0) {
                delete static_cast<const Derived*>(this);
            }
        }

    protected:
        ScriptRefCounted() = default;
        ~ScriptRefCounted() = default;

    private:
        mutable int refCount_ = 1;
    };
}
