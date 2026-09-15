#include "pch.h"
#include "Scene/Feature/SceneFeatureRegistry.h"

#include "Scene/Feature/ISceneFeature.h"

#include <map>

namespace CoreEngine::SceneFeatureRegistry
{
    namespace
    {
        std::map<std::string, Creator>& Creators()
        {
            static std::map<std::string, Creator> creators;
            return creators;
        }
    }

    void Register(const std::string& name, Creator creator)
    {
        if (name.empty() || !creator) {
            return;
        }
        Creators()[name] = std::move(creator);
    }

    std::unique_ptr<ISceneFeature> Create(const std::string& name)
    {
        const auto& creators = Creators();
        const auto it = creators.find(name);
        return it != creators.end() ? it->second() : nullptr;
    }

    std::vector<std::string> GetNames()
    {
        std::vector<std::string> names;
        names.reserve(Creators().size());
        for (const auto& entry : Creators()) {
            names.push_back(entry.first);
        }
        return names;
    }
}
