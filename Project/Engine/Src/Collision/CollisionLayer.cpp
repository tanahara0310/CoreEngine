#include "pch.h"
#include "Collision/CollisionLayer.h"

#include "EngineSystem/Settings/ProjectNameList.h"

#include <utility>

namespace CoreEngine
{
    namespace
    {
        ProjectNameList& Table()
        {
            static ProjectNameList names({
                .settingsPath = "Application/Config/EngineSettings/Layers.json",
                .key = "layers",
                .noun = "レイヤー",
                .first = "Default",
                .maxCount = kMaxCollisionLayers,
                .defaults = { "Default" },
            });
            return names;
        }
    }

    std::size_t CollisionLayers::Count()
    {
        return Table().Names().size();
    }

    const std::vector<std::string>& CollisionLayers::Names()
    {
        return Table().Names();
    }

    bool CollisionLayers::SetNames(std::vector<std::string> names, std::string* outError)
    {
        return Table().SetNames(std::move(names), outError);
    }

    void CollisionLayers::Reload()
    {
        Table().Reload();
    }

    const std::string& ToString(CollisionLayer layer)
    {
        const std::vector<std::string>& names = Table().Names();
        const auto index = static_cast<std::size_t>(layer);
        return index < names.size() ? names[index] : names.front();
    }

    bool TryParseCollisionLayer(std::string_view name, CollisionLayer& out)
    {
        const std::vector<std::string>& names = Table().Names();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                out = static_cast<CollisionLayer>(i);
                return true;
            }
        }
        return false;
    }
}
