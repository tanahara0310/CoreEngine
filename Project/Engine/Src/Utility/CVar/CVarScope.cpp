#include "pch.h"
#include "CVarScope.h"
#include "CVar.h"

#include <iterator>

namespace CoreEngine
{
    namespace
    {
        /// @brief シーンが持つ系統
        /// @details シーンの画そのものを決める値（空・大気・雲・霧・時刻）だけを並べる。
        constexpr std::string_view kSceneOwnedPrefixes[] = {
            "r.Atmosphere",
            "r.Cloud",
            "r.Fog",
            "r.TimeOfDay",
        };
    }

    std::vector<std::string_view> CVarScopes::SceneOwnedPrefixes()
    {
        return { std::begin(kSceneOwnedPrefixes), std::end(kSceneOwnedPrefixes) };
    }

    bool CVarScopes::IsSceneOwned(std::string_view name)
    {
        for (const std::string_view prefix : kSceneOwnedPrefixes) {
            // 系統名の直後が "." のものだけを見る（"r.Fog" は "r.FogVolume.*" を含まない）
            if (name.size() > prefix.size() && name.starts_with(prefix)
                && name[prefix.size()] == '.') {
                return true;
            }
        }
        return false;
    }

    bool CVarScopes::Matches(const ICVar& cvar, CVarScope scope)
    {
        const bool sceneOwned = IsSceneOwned(cvar.GetName());
        return (scope == CVarScope::Scene) == sceneOwned;
    }
}
