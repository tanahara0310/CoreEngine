#include "pch.h"
#include "ComponentFactory.h"

#include "Reflection/PropertySerializer.h"
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

            Entry entry;
            entry.creator = reservation.creator;
            entry.descriptor = probe.descriptor;
#ifdef USE_IMGUI
            entry.inspectorName = std::move(probe.inspectorName);
#endif
            auto [it, inserted] = entries_.try_emplace(std::move(probe.typeName), std::move(entry));
            if (inserted) {
                continue;
            }
            if (it->second.runtime) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "ComponentFactory: 型名 \"{}\" が実行時に登録した型と重なっています。C++ の型を使います",
                    it->first);
                it->second = std::move(entry);
                continue;
            }
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                "ComponentFactory: 型名 \"{}\" が重複しています。先に登録した方を使います",
                it->first);
        }
        reservations_.clear();
        reservations_.shrink_to_fit();
    }

    bool ComponentFactory::RegisterRuntime(const std::string& typeName, RuntimeCreator creator,
        const Reflection::TypeDescriptor* descriptor, const std::string& inspectorName,
        std::filesystem::path sourceFile)
    {
        if (typeName.empty() || !creator || entries_.contains(typeName)) { return false; }

        Entry entry;
        entry.creator = std::move(creator);
        entry.descriptor = descriptor;
        entry.runtime = true;
#ifdef USE_IMGUI
        entry.inspectorName = inspectorName;
        entry.sourceFile = std::move(sourceFile);
#else
        (void)inspectorName;
        (void)sourceFile;
#endif
        entries_.emplace(typeName, std::move(entry));
        return true;
    }

    void ComponentFactory::UnregisterRuntimeTypes()
    {
        std::erase_if(entries_, [](const auto& item) { return item.second.runtime; });
    }

    std::unique_ptr<IComponent> ComponentFactory::Create(const std::string& typeName) const
    {
        const auto it = entries_.find(typeName);
        if (it == entries_.end()) { return nullptr; }
        return it->second.creator ? it->second.creator() : nullptr;
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

    bool ComponentFactory::IsRuntimeType(const std::string& typeName) const
    {
        const auto it = entries_.find(typeName);
        return it != entries_.end() && it->second.runtime;
    }

#ifdef USE_IMGUI
    std::string ComponentFactory::GetInspectorName(const std::string& typeName) const
    {
        const auto it = entries_.find(typeName);
        return it != entries_.end() ? it->second.inspectorName : std::string{};
    }

    std::filesystem::path ComponentFactory::GetSourceFile(const std::string& typeName) const
    {
        const auto it = entries_.find(typeName);
        return it != entries_.end() ? it->second.sourceFile : std::filesystem::path{};
    }

    const json* ComponentFactory::GetDefaultParameters(const std::string& typeName)
    {
        const auto it = entries_.find(typeName);
        if (it == entries_.end() || !it->second.descriptor) {
            return nullptr;
        }

        Entry& entry = it->second;
        if (!entry.defaults) {
            json values = json::object();
            if (const std::unique_ptr<IComponent> probe = entry.creator ? entry.creator() : nullptr) {
                if (const void* const instance = probe->GetReflectionInstance()) {
                    Reflection::PropertySerializer::Save(*entry.descriptor, instance, values);
                }
            }
            entry.defaults = std::move(values);
        }
        return &*entry.defaults;
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
