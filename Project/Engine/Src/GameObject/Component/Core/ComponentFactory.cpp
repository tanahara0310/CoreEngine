#include "pch.h"
#include "ComponentFactory.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <utility>

namespace CoreEngine
{
    ComponentFactory& ComponentFactory::Get()
    {
        static ComponentFactory instance;
        return instance;
    }

    void ComponentFactory::Reserve(Creator creator, ProbeFunction probe)
    {
        if (!creator || !probe) { return; }
        reservations_.push_back(Reservation{ creator, probe });
    }

    void ComponentFactory::Prime()
    {
        if (primed_) { return; }
        primed_ = true;

        for (const Reservation& reservation : reservations_) {
            Probe probe = reservation.probe();
            if (probe.typeName.empty()) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "ComponentFactory: 型名が空のコンポーネントは登録しません");
                continue;
            }

            Entry entry{ reservation.creator, probe.descriptor };
#ifdef USE_IMGUI
            entry.inspectorName = std::move(probe.inspectorName);
#endif
            auto [it, inserted] = entries_.try_emplace(std::move(probe.typeName), entry);
            if (!inserted) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "ComponentFactory: 型名 \"{}\" が重複しています。先に登録した方を使います",
                    it->first);
            }
        }
        reservations_.clear();
        reservations_.shrink_to_fit();
    }

    std::unique_ptr<IComponent> ComponentFactory::Create(const std::string& typeName) const
    {
        const auto it = entries_.find(typeName);
        if (it == entries_.end()) { return nullptr; }
        return it->second.creator();
    }

    const Reflection::TypeDescriptor* ComponentFactory::FindDescriptor(const std::string& typeName) const
    {
        const auto it = entries_.find(typeName);
        return it != entries_.end() ? it->second.descriptor : nullptr;
    }

    bool ComponentFactory::IsRegistered(const std::string& typeName) const
    {
        return entries_.find(typeName) != entries_.end();
    }

#ifdef USE_IMGUI
    std::string ComponentFactory::GetInspectorName(const std::string& typeName) const
    {
        const auto it = entries_.find(typeName);
        return it != entries_.end() ? it->second.inspectorName : std::string{};
    }
#endif

    std::vector<std::string> ComponentFactory::GetRegisteredTypeNames() const
    {
        std::vector<std::string> names;
        names.reserve(entries_.size());
        for (const auto& [name, entry] : entries_) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }
}
