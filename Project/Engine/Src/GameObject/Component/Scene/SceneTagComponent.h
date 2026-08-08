#pragma once

#include "GameObject/Component/Core/IComponent.h"

namespace CoreEngine
{
/// @brief 「シーン内の特定の役割」を型で示すタグコンポーネント。
/// @details `dynamic_cast<具象型*>` でのシーン走査を置き換える。役割を持つクラスが
///          コンストラクタで `AddComponent<SceneTagComponent<自分>>(this)` を付け、
///          探す側は `GameObjectManager::FindFirstComponent<SceneTagComponent<T>>()` で引く。
template <typename T>
class SceneTagComponent : public IComponent {
public:
    explicit SceneTagComponent(T* target) : target_(target) {}

    const char* GetTypeName() const override { return "SceneTag"; }

    /// @brief タグが指す実体（アタッチした本人）
    T* Get() const { return target_; }

private:
    T* target_ = nullptr;
};
}
