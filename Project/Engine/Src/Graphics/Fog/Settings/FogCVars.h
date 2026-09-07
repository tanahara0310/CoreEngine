#pragma once

#include "Graphics/Fog/Settings/FogSettings.h"
#include "Math/Vector/Vector4.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    /// @brief フォグパラメータの CVar 群（設定と既定値の単一情報源）
    /// @details 定義は FogCVars.cpp。永続化は CVars.json（CVarSettingsSection）が担う。
    ///          FogManager::Update が毎フレーム LoadInto で FogSettings へ取り込む。
    namespace FogCVars
    {
        extern CVar<bool>    Enabled;
        extern CVar<Vector4> Color;
        extern CVar<float>   ColorIntensity;
        extern CVar<float>   Density;
        extern CVar<float>   HeightFalloff;
        extern CVar<float>   HeightRefM;
        extern CVar<float>   StartDistanceM;
        extern CVar<float>   MaxOpacity;
        extern CVar<float>   SkyDistanceM;
        extern CVar<bool>    ApplyToSky;
        extern CVar<float>   SkyColorBlend;
        extern CVar<Vector4> SunTint;
        extern CVar<float>   SunScatteringGain;
        extern CVar<float>   SunScatteringExponent;

        /// @brief CVar の現在値を設定構造体へ取り込む
        void LoadInto(FogSettings& settings);
    }
}
