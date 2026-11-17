#include "pch.h"
#include "GameObject/GameObjectTags.h"

#include "EngineSystem/Settings/ProjectNameList.h"

#include <utility>

namespace CoreEngine
{
    namespace
    {
        ProjectNameList& Table()
        {
            static ProjectNameList names({
                .settingsPath = "Application/Config/EngineSettings/Tags.json",
                .key = "tags",
                .noun = "タグ",
                .first = GameObjectTags::kUntagged,
                .maxCount = 0,
                .defaults = { GameObjectTags::kUntagged, "Player" },
            });
            return names;
        }
    }

    const std::vector<std::string>& GameObjectTags::Names()
    {
        return Table().Names();
    }

    bool GameObjectTags::IsDefined(std::string_view tag)
    {
        return Table().Contains(tag);
    }

    bool GameObjectTags::SetNames(std::vector<std::string> names, std::string* outError)
    {
        return Table().SetNames(std::move(names), outError);
    }

    void GameObjectTags::Reload()
    {
        Table().Reload();
    }
}
