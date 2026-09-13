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

    void ComponentFactory::Reserve(Creator creator, TypeNameProbe probe)
    {
        if (!creator || !probe) { return; }
        reservations_.push_back(Reservation{ creator, probe });
    }

    void ComponentFactory::Prime()
    {
        if (primed_) { return; }
        primed_ = true;

        for (const Reservation& reservation : reservations_) {
            std::string typeName = reservation.probe();
            if (typeName.empty()) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::System,
                    "ComponentFactory: 型名が空のコンポーネントは登録しません");
                continue;
            }

            auto [it, inserted] = creators_.try_emplace(std::move(typeName), reservation.creator);
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
        const auto it = creators_.find(typeName);
        if (it == creators_.end()) { return nullptr; }
        return it->second();
    }

    bool ComponentFactory::IsRegistered(const std::string& typeName) const
    {
        return creators_.find(typeName) != creators_.end();
    }

    std::vector<std::string> ComponentFactory::GetRegisteredTypeNames() const
    {
        std::vector<std::string> names;
        names.reserve(creators_.size());
        for (const auto& [name, creator] : creators_) {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        return names;
    }
}
