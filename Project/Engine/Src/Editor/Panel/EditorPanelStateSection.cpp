#include "pch.h"
#include "Editor/Panel/EditorPanelStateSection.h"

#include "Editor/Panel/EditorPanelRegistry.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <string>
#include <unordered_map>

namespace CoreEngine::Editor
{
    void EditorPanelStateSection::Serialize(nlohmann::json& out) const
    {
        nlohmann::json visible = nlohmann::json::object();
        EditorPanelRegistry::Get().ForEachPersistedVisibility([&visible](const std::string& id, bool shown) {
            visible[id] = shown;
            });
        out["visible"] = std::move(visible);
    }

    void EditorPanelStateSection::Deserialize(const nlohmann::json& in)
    {
        if (!in.contains("visible") || !in["visible"].is_object()) {
            return;
        }

        std::unordered_map<std::string, bool> saved;
        for (const auto& [id, value] : in["visible"].items()) {
            if (value.is_boolean()) {
                saved[id] = value.get<bool>();
            }
        }
        EditorPanelRegistry::Get().ApplySavedVisibility(std::move(saved));
    }
}
