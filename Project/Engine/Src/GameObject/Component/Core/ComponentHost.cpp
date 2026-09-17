#include "pch.h"
#include "ComponentHost.h"

#include "ComponentFactory.h"
#include "MissingComponent.h"
#ifdef CORE_EDITOR
#include "Editor/Command/EditorCommandStack.h"
#endif
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
        /// @brief このコンポーネントを握っている Undo 操作を履歴から外す（エディタを含まないビルドは何もしない）
        /// @note 外さないと、解放済みのコンポーネントへ Ctrl+Z が書き込む
        void ForgetInHistory([[maybe_unused]] const IComponent* component)
        {
#ifdef CORE_EDITOR
            if (component) {
                Editor::EditorCommandStack::Get().RemoveCommandsReferencing(component);
            }
#endif
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
        for (const auto& component : detached_) {
            ForgetInHistory(component.get());
        }
    }

    IComponent* ComponentHost::AttachComponent(std::unique_ptr<IComponent> component, bool invokeAwake)
    {
        if (!component) { return nullptr; }

        IComponent* raw = component.get();
        raw->owner_ = ownerObject_;
        raw->attachedByCode_ = dataAttachDepth_ == 0;
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

    std::optional<std::size_t> ComponentHost::DetachComponent(IComponent* component)
    {
        if (!component) { return std::nullopt; }

        std::size_t position = 0;
        for (auto& slot : components_) {
            if (!slot) { continue; }
            if (slot.get() == component) {
                // スロットは nullptr にして残し、フレーム末に詰める
                detached_.push_back(std::move(slot));
                return position;
            }
            ++position;
        }
        return std::nullopt;
    }

    bool ComponentHost::ReattachComponent(IComponent* component, std::size_t position)
    {
        if (!component) { return false; }

        const auto detached = std::find_if(detached_.begin(), detached_.end(),
            [component](const std::unique_ptr<IComponent>& entry) {
                return entry.get() == component;
            });
        if (detached == detached_.end()) {
            return false;
        }

        std::unique_ptr<IComponent> owned = std::move(*detached);
        detached_.erase(detached);
        InsertAtPosition(std::move(owned), position);
        return true;
    }

    IComponent* ComponentHost::FindDetachedComponent(const IComponent* component) const
    {
        if (!component) { return nullptr; }

        for (const auto& entry : detached_) {
            if (entry.get() == component) {
                return entry.get();
            }
        }
        return nullptr;
    }

    IComponent* ComponentHost::RestoreComponent(const json& entry, std::size_t position)
    {
        if (!entry.is_object() || !entry.contains("type") || !entry["type"].is_string()) {
            return nullptr;
        }

        const std::string type = entry["type"].get<std::string>();
        std::unique_ptr<IComponent> component = ComponentFactory::Get().Create(type);
        if (!component) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "ComponentHost: 型 \"{}\" はファクトリに無いので、保存データを持ったまま読み込めないコンポーネントとして置きます", type);
            component = std::make_unique<MissingComponent>(type);
        }

        // 付けてから値を流し、最後に Awake() を呼ぶ
        IComponent* const raw = component.get();
        raw->owner_ = ownerObject_;
        raw->attachedByCode_ = false;
        InsertAtPosition(std::move(component), position);

        DataAttachScope dataScope(*this);
        LoadComponentEntry(*raw, entry);
        raw->Awake();
        return raw;
    }

    void ComponentHost::InsertAtPosition(std::unique_ptr<IComponent> component, std::size_t position)
    {
        // 取り外し済みのスロットを飛ばして数え、position 番目の手前へ入れる
        auto insertAt = components_.end();
        std::size_t live = 0;
        for (auto slot = components_.begin(); slot != components_.end(); ++slot) {
            if (!*slot) { continue; }
            if (live == position) {
                insertAt = slot;
                break;
            }
            ++live;
        }

        components_.insert(insertAt, std::move(component));
    }

    std::optional<std::size_t> ComponentHost::FindComponentPosition(const IComponent* component) const
    {
        if (!component) { return std::nullopt; }

        std::size_t position = 0;
        for (const auto& slot : components_) {
            if (!slot) { continue; }
            if (slot.get() == component) {
                return position;
            }
            ++position;
        }
        return std::nullopt;
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
        // 外して控えているものも、オブジェクトと一緒に後始末する
        for (auto& component : detached_) {
            component->OnDestroy();
        }
    }

    // ===== シリアライズ =====

    json ComponentHost::SerializeComponents() const
    {
        json components = json::array();
        for (const auto& component : components_) {
            if (component) {
                components.push_back(SerializeComponent(*component));
            }
        }
        return components;
    }

    json ComponentHost::SerializeComponent(const IComponent& component)
    {
        json parameters;
        const Reflection::TypeDescriptor* descriptor = component.GetTypeDescriptor();
        if (!descriptor || descriptor->partial) {
            parameters = component.OnSerialize();
        }
        if (descriptor) {
            // 記述子を持つ型は宣言 1 箇所から値を取る。一部だけを載せた型は OnSerialize の値へ足す
            if (!parameters.is_object()) {
                parameters = json::object();
            }
            IComponent& mutableComponent = const_cast<IComponent&>(component);
            Reflection::PropertySerializer::Save(
                *descriptor, mutableComponent.GetReflectionInstance(), parameters);
        }

        json entry = {
            { "type", component.GetTypeName() },
            { "enabled", component.IsEnabled() },
        };
        if (descriptor) {
            Reflection::PropertySerializer::WriteComponentVersion(entry, descriptor->version);
        } else if (const auto* missing = dynamic_cast<const MissingComponent*>(&component)) {
            Reflection::PropertySerializer::WriteComponentVersion(entry, missing->GetSavedVersion());
        }
        if (!parameters.empty()) {
            entry["parameters"] = std::move(parameters);
        }
        return entry;
    }

    void ComponentHost::DeserializeComponents(const json& components)
    {
        if (!components.is_array()) { return; }

        // ここで付くもの（Awake の中で足されるものを含む）は、コードが付けたものとして扱わない
        DataAttachScope dataScope(*this);

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
                std::unique_ptr<IComponent> component = ComponentFactory::Get().Create(type);
                if (!component) {
                    // 型が無くても保存データは捨てず、次に保存したときへ持ち越す
                    Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                        "ComponentHost: 型 \"{}\" は実体にもファクトリにも無いので、保存データを持ったまま読み込めないコンポーネントとして置きます", type);
                    component = std::make_unique<MissingComponent>(type);
                }
                target = AttachComponent(std::move(component), false);
                searchIndex = components_.size();
                created = target != nullptr;
            }

            if (!target) {
                continue;
            }

            LoadComponentEntry(*target, entry);

            if (created) {
                target->Awake();
            }
        }
    }

    void ComponentHost::LoadComponentEntry(IComponent& target, const json& entry)
    {
        if (entry.contains("enabled") && entry["enabled"].is_boolean()) {
            target.SetEnabled(entry["enabled"].get<bool>());
        }

        // 古い版で保存した値は、型の今の版の形へ書き換えてから流す
        const Reflection::TypeDescriptor* descriptor = target.GetTypeDescriptor();
        const uint32_t savedVersion = Reflection::PropertySerializer::ReadComponentVersion(entry);
        // 型が見つからないものは、保存データの版を次の保存へ持ち越す
        if (auto* missing = dynamic_cast<MissingComponent*>(&target)) {
            missing->SetSavedVersion(savedVersion);
        }
        json upgraded;
        const json* source = &entry;
        if (descriptor && savedVersion != descriptor->version) {
            upgraded = entry;
            if (Reflection::PropertySerializer::UpgradeComponentEntry(*descriptor, upgraded)) {
                source = &upgraded;
            } else {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "ComponentHost: 型 \"{}\" の保存値は版 {} で、今の版 {} より新しいので、そのまま読みます",
                    target.GetTypeName(), savedVersion, descriptor->version);
            }
        }

        if (source->contains("parameters") && source->at("parameters").is_object()) {
            const json& parameters = source->at("parameters");
            if (descriptor) {
                Reflection::PropertySerializer::Load(
                    *descriptor, target.GetReflectionInstance(), parameters);
            }
            if (!descriptor || descriptor->partial) {
                target.OnDeserialize(parameters);
            }
        }
    }
}
