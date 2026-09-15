#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/ObjectId.h"
#include "Reflection/PropertyDescriptor.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace CoreEngine
{
    class GameObject;
    class GameObjectManager;

    /// @brief オブジェクトが持つコンポーネントから、参照で指せるものを探す
    /// @param accepts 指せるコンポーネントかの判定（nullptr ならどれでも指せる）
    /// @param componentType この型名のものを優先する（無ければ最初に指せたもの）
    /// @return 見つからなければ nullptr
    IComponent* FindReferencedComponent(const GameObject& object,
        Reflection::PropertyDescriptor::ComponentFilter accepts, std::string_view componentType);

    /// @brief プロパティの記述（判定の関数と繋ぎ先の型名）で、オブジェクトから参照で指せるコンポーネントを探す
    /// @param componentType この型名のものを優先する（無ければ最初に指せたもの）
    /// @return 見つからなければ nullptr
    IComponent* FindReferencedComponent(const GameObject& object,
        const Reflection::PropertyDescriptor& property, std::string_view componentType);

    /// @brief シーン内の別オブジェクトのコンポーネントを ID で指す参照の、型に依らない部分
    class ObjectRefBase
    {
    public:
        /// @brief 指す先のオブジェクトの ID（何も指していなければ未割り当て）
        ObjectId GetObjectId() const noexcept { return objectId_; }

        /// @brief 指す先のコンポーネントの型名（`IComponent::GetTypeName()`）
        const std::string& GetComponentType() const noexcept { return componentType_; }

        /// @brief 型記述子とやり取りする値にする
        Reflection::ObjectRefValue GetValue() const;

        /// @brief 型記述子から受け取った値を設定する
        /// @param holder この参照を持つコンポーネント。実体を引くシーンをここから決める
        void SetValue(const Reflection::ObjectRefValue& value, const IComponent* holder);

        /// @brief 何も指さない状態に戻す
        void Reset();

    protected:
        ObjectRefBase() = default;

        /// @brief 実体を指す（nullptr なら `Reset()` と同じ）
        void Assign(IComponent* target);

        /// @brief 前回引いた実体がまだ使えるならそれを返す（使えなければ nullptr）
        IComponent* GetCached() const noexcept
        {
            return (cached_ && epoch_ && *epoch_ == cachedEpoch_) ? cached_ : nullptr;
        }

        /// @brief ID から実体を引き直す
        /// @param accepts 指せるコンポーネントかの判定
        IComponent* Resolve(Reflection::PropertyDescriptor::ComponentFilter accepts) const;

    private:
        void BindScene(const GameObjectManager* manager);

        ObjectId                 objectId_{};
        std::string              componentType_;
        const GameObjectManager* manager_ = nullptr;
        const std::uint64_t*     epoch_ = nullptr;  ///< `GameObjectManager::GetReferenceEpoch()` の実体
        mutable IComponent*      cached_ = nullptr;
        mutable std::uint64_t    cachedEpoch_ = 0;
    };

    /// @brief `T` 型のコンポーネントを指す参照
    /// @details 型記述子に `REFLECT_OBJECT_REF` で載せると `{"ref": ID, "comp": 型名}` で保存され、
    ///          インスペクタで繋ぎ先を選べる。
    template <class T>
    class ObjectRef final : public ObjectRefBase
    {
    public:
        ObjectRef() = default;
        explicit ObjectRef(T* target) { Set(target); }

        /// @brief 実体を指す（nullptr なら何も指さない）
        void Set(T* target) { Assign(target); }

        /// @brief 指す先の実体（指していない・見つからない・解放済みなら nullptr）
        T* Get() const
        {
            if (IComponent* cached = GetCached()) {
                return static_cast<T*>(cached);
            }
            return static_cast<T*>(Resolve(&Accepts));
        }

        T* operator->() const { return Get(); }
        explicit operator bool() const { return Get() != nullptr; }

        /// @brief `T` として指せるコンポーネントか
        static bool Accepts(const IComponent* component)
        {
            return dynamic_cast<const T*>(component) != nullptr;
        }
    };
}
