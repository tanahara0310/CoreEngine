#pragma once

#include "Graphics/Cloud/Settings/CloudSettings.h"
#include "Utility/CVar/CVar.h"

namespace CoreEngine
{
    /// @brief 雲パラメータの CVar 群（設定と既定値の単一情報源）
    /// @details 定義は CloudCVars.cpp。永続化は CVars.json（CVarSettingsSection）が担う。
    ///          VolumetricCloudManager::Update が毎フレーム LoadInto でパラメータへ取り込む。
    namespace CloudCVars
    {
        // ---- 機能トグル ----
        extern CVar<bool> Enabled;

        // ---- 雲層ジオメトリ ----
        extern CVar<float> LayerBottomAltitudeM;
        extern CVar<float> LayerThicknessM;

        // ---- カバレッジ・密度 ----
        extern CVar<float> GlobalCoverage;
        extern CVar<float> DensityScale;

        // ---- ノイズスケール ----
        extern CVar<float> BaseNoiseScaleM;
        extern CVar<float> DetailNoiseScaleM;
        extern CVar<float> DetailErosionStrength;
        extern CVar<float> WeatherMapScaleM;

        // ---- 風 ----
        extern CVar<float> WindDirX;
        extern CVar<float> WindDirZ;
        extern CVar<float> WindSpeedMPerS;

        // ---- ライティング ----
        extern CVar<float> PhaseG0;
        extern CVar<float> PhaseG1;
        extern CVar<float> PhaseBlend;
        extern CVar<float> AmbientIntensity;
        extern CVar<float> BeerPowderStrength;
        extern CVar<float> LightMarchStepM;

        // ---- 太陽散乱スケールと多重散乱 ----
        extern CVar<float> SunLightScale;
        extern CVar<float> MsAttenuation;
        extern CVar<float> MsContribution;
        extern CVar<float> MsEccentricity;

        // ---- マーチング ----
        extern CVar<float> EarlyExitTransmittance;
        extern CVar<float> MaxMarchDistanceM;
        extern CVar<int> MaxSteps;
        extern CVar<int> ResolutionDivisor;

        // ---- ゴッドレイ ----
        extern CVar<bool> GodRayEnabled;
        extern CVar<float> GodRayIntensity;
        extern CVar<float> GodRayMieBoost;
        extern CVar<float> GodRayMaxDistanceM;
        extern CVar<int> GodRayStepCount;
        extern CVar<float> CloudShadowRegionSizeM;

        /// @brief CVar の現在値をパラメータ構造体へ取り込む
        void LoadInto(VolumetricCloudParameters& params);
    }
}
