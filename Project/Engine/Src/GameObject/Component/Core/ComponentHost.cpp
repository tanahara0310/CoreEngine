#include "pch.h"
#include "ComponentHost.h"

#include "ComponentFactory.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Reflection/PropertySerializer.h"
#include "Reflection/TypeDescriptor.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

namespace CoreEngine
{
    namespace
    {
        /// @brief このコンポーネントを握っている Undo 操作を履歴から外す
        /// @note 外さないと、解放済みのコンポーネントへ Ctrl+Z が書き込む
        void ForgetInHistory(const IComponent* component)
        {
            if (component) {
                Editor::EditorCommandStack::Get().RemoveCommandsReferencing(component);
            }
        }
    }

    ComponentHost::~ComponentHost()
    {
        // 破棄経路を通らずに直接 delete された場合の保険。
        // GameObjectManager 経由なら DispatchComponentDestroy() が先に走っている。
        DispatchComponentDestroy();

        for (const auto& component : components_) {
            ForgetInHistory(component.get());
        }
        for (const auto& component : retired_) {
            ForgetInHistory(component.get());
        }
    }

    IComponent* ComponentHost::AttachComponent(std::unique_ptr<IComponent> component, bool invokeAwake)
    {
        if (!component) { return nullptr; }

        IComponent* raw = component.get();
        raw->owner_ = ownerObject_;
        components_.push_back(std::move(component));
        if (invokeAwake) {
            raw->Awake();
        }
        return raw;
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

    bool ComponentHost::ReleaseRetiredComponents()
    {
        // 先に nullptr スロットを詰める（retired_ の解放より先。解放中に
        // components_ を触られても矛盾しないようにするため）
        components_.erase(
            std::remove(components_.begin(), components_.end(), nullptr),
            components_.end());

        const bool released = !retired_.empty();
        for (const auto& component : retired_) {
            ForgetInHistory(component.get());
        }
        retired_.clear();
        return released;
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

    // ===== シリアライズ =====

    json ComponentHost::SerializeComponents() const
    {
        json components = json::array();
        for (const auto& component : components_) {
            if (!component) { continue; }

            json parameters;
            const Reflection::TypeDescriptor* descriptor = component->GetTypeDescriptor();
            if (!descriptor || descriptor->partial) {
                parameters = component->OnSerialize();
            }
            if (descriptor) {
                // 記述子を持つ型は宣言 1 箇所から値を取る。一部だけを載せた型は OnSerialize の値へ足す
                if (!parameters.is_object()) {
                    parameters = json::object();
                }
                IComponent* mutableComponent = const_cast<IComponent*>(component.get());
                Reflection::PropertySerializer::Save(
                    *descriptor, mutableComponent->GetReflectionInstance(), parameters);
            }

            json entry = {
                { "type", component->GetTypeName() },
                { "enabled", component->IsEnabled() },
            };
            if (descriptor) {
                Reflection::PropertySerializer::WriteComponentVersion(entry, descriptor->version);
            }
            if (!parameters.empty()) {
                entry["parameters"] = std::move(parameters);
            }
            components.push_back(std::move(entry));
        }
        return components;
    }

    void ComponentHost::DeserializeComponents(const json& components)
    {
        if (!components.is_array()) { return; }

        // 同じ型を複数持つ場合に備え、型ごとに「次に対応づける位置」を持って前へ進める
        std::unordered_map<std::string, std::size_t> nextIndex;
        for (const auto& entry : components) {
            if (!entry.is_object() || !entry.contains("type") || !entry["type"].is_string()) {
                continue;
            }
            const std::string type = entry["type"].get<std::string>();

            IComponent* target = nullptr;
            std::size_t& searchIndex = nextIndex[type];
            for (; searchIndex < components_.size(); ++searchIndex) {
                IComponent* component = components_[searchIndex].get();
                if (!component || type != component->GetTypeName()) { continue; }

                target = component;
                ++searchIndex;
                break;
            }

            // ここで作った物は値を流し終えてから Awake() を呼ぶ
            bool created = false;
            if (!target) {
                // 実体が足りない分はファクトリで作る。作った物は末尾に付くので、
                // 同じ型の次の探索がそれを拾い直さないように位置を末尾へ送る。
                target = AttachComponent(ComponentFactory::Get().Create(type), false);
                searchIndex = components_.size();
                created = target != nullptr;
            }

            if (!target) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "ComponentHost: 型 \"{}\" は実体にもファクトリにも無いので読み飛ばします", type);
                continue;
            }

            if (entry.contains("enabled") && entry["enabled"].is_boolean()) {
                target->SetEnabled(entry["enabled"].get<bool>());
            }

            // 古い版で保存した値は、型の今の版の形へ書き換えてから流す
            const Reflection::TypeDescriptor* descriptor = target->GetTypeDescriptor();
            const uint32_t savedVersion = Reflection::PropertySerializer::ReadComponentVersion(entry);
            json upgraded;
            const json* source = &entry;
            if (descriptor && savedVersion != descriptor->version) {
                upgraded = entry;
                if (Reflection::PropertySerializer::UpgradeComponentEntry(*descriptor, upgraded)) {
                    source = &upgraded;
                } else {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                        "ComponentHost: 型 \"{}\" の保存値は版 {} で、今の版 {} より新しいので、そのまま読みます",
                        type, savedVersion, descriptor->version);
                }
            }

            if (source->contains("parameters") && source->at("parameters").is_object()) {
                const json& parameters = source->at("parameters");
                if (descriptor) {
                    Reflection::PropertySerializer::Load(
                        *descriptor, target->GetReflectionInstance(), parameters);
                }
                if (!descriptor || descriptor->partial) {
                    target->OnDeserialize(parameters);
                }
            }

            if (created) {
                target->Awake();
            }
        }
    }
}
