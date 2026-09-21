#include "pch.h"
#include "CVarScope.h"
#include "CVar.h"

#include <iterator>

namespace CoreEngine
{
    namespace
    {
        /// @brief シーンが持つ系統
        /// @details シーンの画そのものを決める値だけを並べる。
        ///          `r.Loading`（読み込み画面）と `r.Fade`（シーン遷移）は、シーンが無い間や
        ///          切り替えの最中に出るものなのでプロジェクトのまま。
        ///          SSAO・TAA・CAS・RTShadow は品質の設定なのでプロジェクトのまま。
        constexpr std::string_view kSceneOwnedPrefixes[] = {
            // 環境
            "r.Atmosphere",
            "r.Cloud",
            "r.Fog",
            "r.TimeOfDay",

            // ポストエフェクト
            "r.AutoExposure",
            "r.Bloom",
            "r.Blur",
            "r.ChromaticAberration",
            "r.ColorGrading",
            "r.ColorLUT",
            "r.Dissolve",
            "r.DoF",
            "r.FilmGrain",
            "r.GrayScale",
            "r.Invert",
            "r.LensFlare",
            "r.LocalExposure",
            "r.MotionBlur",
            "r.Outline",
            "r.RadialBlur",
            "r.Random",
            "r.RasterScroll",
            "r.Sepia",
            "r.Shockwave",
            "r.ToneMapping",
            "r.Vignette",
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
        if (scope == CVarScope::Any) {
            return true;
        }
        const bool sceneOwned = IsSceneOwned(cvar.GetName());
        return (scope == CVarScope::Scene) == sceneOwned;
    }
}
