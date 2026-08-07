#include "pch.h"
#include "ComponentHost.h"

#include <algorithm>

namespace CoreEngine
{
    ComponentHost::~ComponentHost()
    {
        // 破棄経路を通らずに直接 delete された場合の保険。
        // GameObjectManager 経由なら DispatchComponentDestroy() が先に走っている。
        DispatchComponentDestroy();
    }

    size_t ComponentHost::GetComponentCount() const
    {
        return static_cast<size_t>(std::count_if(
            components_.begin(), components_.end(),
            [](const std::unique_ptr<IComponent>& entry) { return entry != nullptr; }));
    }

    void ComponentHost::RetireSlot(std::unique_ptr<IComponent>& slot)
    {
        if (!slot) { return; }

        slot->OnDestroy();

        // 実体は即 delete しない。呼び出し元やイテレーション中のループが
        // 生ポインタを保持している可能性があるため、フレーム末まで残す。
        // 配列からは消さず nullptr にすることで、進行中のインデックス走査をずらさない。
        retired_.push_back(std::move(slot));
    }

    bool ComponentHost::RemoveComponent(IComponent* component)
    {
        if (!component) { return false; }

        auto it = std::find_if(components_.begin(), components_.end(),
            [component](const std::unique_ptr<IComponent>& entry) {
                return entry.get() == component;
            });
        if (it == components_.end()) {
            return false;
        }

        RetireSlot(*it);
        return true;
    }

    void ComponentHost::RemoveAllComponents()
    {
        for (auto& component : components_) {
            RetireSlot(component);
        }
    }

    void ComponentHost::ReleaseRetiredComponents()
    {
        // 先に nullptr スロットを詰める（retired_ の解放より先。解放中に
        // components_ を触られても矛盾しないようにするため）
        components_.erase(
            std::remove(components_.begin(), components_.end(), nullptr),
            components_.end());

        retired_.clear();
    }

    void ComponentHost::DispatchComponentStart()
    {
        // Start() の中で AddComponent される可能性があるため、
        // 今フレームの分だけを対象にする（追加分は次フレームの Start で拾う）。
        const size_t count = components_.size();
        for (size_t i = 0; i < count; ++i) {
            IComponent* component = components_[i].get();
            if (!component || component->startCalled_) { continue; }
            component->startCalled_ = true;
            component->Start();
        }
    }

    void ComponentHost::DispatchComponentUpdate()
    {
        const size_t count = components_.size();
        for (size_t i = 0; i < count; ++i) {
            IComponent* component = components_[i].get();
            if (!component || !component->IsEnabled()) { continue; }
            component->Update();
        }
    }

    void ComponentHost::DispatchComponentLateUpdate()
    {
        const size_t count = components_.size();
        for (size_t i = 0; i < count; ++i) {
            IComponent* component = components_[i].get();
            if (!component || !component->IsEnabled()) { continue; }
            component->LateUpdate();
        }
    }

    void ComponentHost::DispatchComponentDestroy()
    {
        if (destroyDispatched_) { return; }
        destroyDispatched_ = true;

        for (auto& component : components_) {
            if (component) {
                component->OnDestroy();
            }
        }
    }
}
