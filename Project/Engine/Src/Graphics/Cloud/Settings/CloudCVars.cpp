#include "pch.h"
#include "CloudCVars.h"

#include <algorithm>

namespace CoreEngine
{
    namespace CloudCVars
    {
        // ---- 機能トグル ----
        CVar<bool> Enabled{
            "r.Cloud.Enabled", true,
            "ボリュメトリック雲を有効にする",
            CVarRange{}, CVarFlags::NoUI };

        // ---- 雲層ジオメトリ ----
        CVar<float> LayerBottomAltitudeM{
            "r.Cloud.LayerBottomAltitude", 1500.0f,
            "雲底の高度 [m]",
            CVarRange{ 0.0f, 20000.0f } };

        CVar<float> LayerThicknessM{
            "r.Cloud.LayerThickness", 4000.0f,
            "雲層の厚み [m]",
            CVarRange{ 100.0f, 20000.0f } };

        // ---- カバレッジ・密度 ----
        CVar<float> GlobalCoverage{
            "r.Cloud.GlobalCoverage", 0.62f,
            "全体の雲量。大きいほど空を覆う",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> DensityScale{
            "r.Cloud.DensityScale", 0.12f,
            "雲の密度スケール",
            CVarRange{ 0.0f, 1.0f } };

        // ---- ノイズスケール ----
        CVar<float> BaseNoiseScaleM{
            "r.Cloud.BaseNoiseScale", 9000.0f,
            "ベース形状ノイズ 1 タイルの実寸 [m]",
            CVarRange{ 500.0f, 50000.0f } };

        CVar<float> DetailNoiseScaleM{
            "r.Cloud.DetailNoiseScale", 600.0f,
            "ディテールノイズ 1 タイルの実寸 [m]",
            CVarRange{ 50.0f, 5000.0f } };

        CVar<float> DetailErosionStrength{
            "r.Cloud.DetailErosionStrength", 0.22f,
            "ディテールノイズによる輪郭の削り込み量",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> WeatherMapScaleM{
            "r.Cloud.WeatherMapScale", 60000.0f,
            "天候マップ 1 タイルの実寸 [m]",
            CVarRange{ 5000.0f, 200000.0f } };

        // ---- 風 ----
        CVar<float> WindDirX{
            "r.Cloud.WindDirX", 1.0f,
            "風向 X（XZ 平面。内部で正規化される）",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> WindDirZ{
            "r.Cloud.WindDirZ", 0.0f,
            "風向 Z（XZ 平面。内部で正規化される）",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> WindSpeedMPerS{
            "r.Cloud.WindSpeed", 8.0f,
            "風速 [m/s]。雲の流れる速さ",
            CVarRange{ 0.0f, 100.0f } };

        // ---- ライティング ----
        CVar<float> PhaseG0{
            "r.Cloud.PhaseG0", 0.8f,
            "HG 位相関数の前方散乱ローブ",
            CVarRange{ -0.99f, 0.99f } };

        CVar<float> PhaseG1{
            "r.Cloud.PhaseG1", -0.3f,
            "HG 位相関数の後方散乱ローブ",
            CVarRange{ -0.99f, 0.99f } };

        CVar<float> PhaseBlend{
            "r.Cloud.PhaseBlend", 0.5f,
            "2 つの位相ローブのブレンド率",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> AmbientIntensity{
            "r.Cloud.AmbientIntensity", 1.15f,
            "雲に当たる環境光の強さ",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> BeerPowderStrength{
            "r.Cloud.BeerPowderStrength", 0.5f,
            "Beer-Powder 効果の強さ（雲の縁の暗さ）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> LightMarchStepM{
            "r.Cloud.LightMarchStep", 200.0f,
            "サンライトマーチの 1 歩 [m]。小さいほど高品質だが重い",
            CVarRange{ 10.0f, 2000.0f } };

        // ---- 太陽散乱スケールと多重散乱 ----
        CVar<float> SunLightScale{
            "r.Cloud.SunLightScale", 3.5f,
            "雲に当たる太陽光の倍率",
            CVarRange{ 0.0f, 20.0f } };

        CVar<float> MsAttenuation{
            "r.Cloud.MultiScatterAttenuation", 0.6f,
            "多重散乱オクターブごとの減衰",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> MsContribution{
            "r.Cloud.MultiScatterContribution", 0.5f,
            "多重散乱の寄与率",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> MsEccentricity{
            "r.Cloud.MultiScatterEccentricity", 0.5f,
            "オクターブごとの位相非対称度の減衰",
            CVarRange{ 0.0f, 1.0f } };

        // ---- マーチング ----
        CVar<float> EarlyExitTransmittance{
            "r.Cloud.EarlyExitTransmittance", 0.005f,
            "レイマーチを打ち切る透過率のしきい値",
            CVarRange{ 0.0f, 0.1f } };

        CVar<float> MaxMarchDistanceM{
            "r.Cloud.MaxMarchDistance", 120000.0f,
            "ビューレイマーチの最大距離 [m]",
            CVarRange{ 1000.0f, 500000.0f } };

        CVar<int> MaxSteps{
            "r.Cloud.MaxSteps", 160,
            "レイマーチの最大ステップ数。多いほど高品質だが重い",
            CVarRange{ 8.0f, 512.0f } };

        CVar<int> ResolutionDivisor{
            "r.Cloud.ResolutionDivisor", 2,
            "雲バッファの解像度分割数。2 = 1/2 解像度",
            CVarRange{ 1.0f, 4.0f } };

        // ---- ゴッドレイ ----
        CVar<bool> GodRayEnabled{
            "r.Cloud.GodRayEnabled", true,
            "ゴッドレイ（雲の切れ間から差す光芒）を有効にする" };

        CVar<float> GodRayIntensity{
            "r.Cloud.GodRayIntensity", 1.0f,
            "遮蔽差分（物理項）のスケール。1 が物理値",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> GodRayMieBoost{
            "r.Cloud.GodRayMieBoost", 0.8f,
            "加算ミー項（演出）。0 で完全物理",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> GodRayMaxDistanceM{
            "r.Cloud.GodRayMaxDistance", 25000.0f,
            "ゴッドレイのビューレイマーチ最大距離 [m]",
            CVarRange{ 1000.0f, 100000.0f } };

        CVar<int> GodRayStepCount{
            "r.Cloud.GodRayStepCount", 32,
            "ゴッドレイのレイマーチステップ数",
            CVarRange{ 4.0f, 128.0f } };

        CVar<float> CloudShadowRegionSizeM{
            "r.Cloud.ShadowRegionSize", 60000.0f,
            "雲シャドウマップのカバー範囲（一辺）[m]",
            CVarRange{ 5000.0f, 200000.0f } };

        namespace
        {
            // CVar とパラメータの対応表。取り込みはこの表からのみ行うので、
            // パラメータを増やすときは CVar 定義とこの 1 行だけを足せばよい
            template <class CVarType, class MemberType>
            struct Binding {
                CVarType* cvar;
                MemberType VolumetricCloudParameters::* member;
            };

            constexpr Binding<CVar<float>, float> kFloatBindings[] = {
                { &LayerBottomAltitudeM,  &VolumetricCloudParameters::layerBottomAltitudeM },
                { &LayerThicknessM,       &VolumetricCloudParameters::layerThicknessM },
                { &GlobalCoverage,        &VolumetricCloudParameters::globalCoverage },
                { &DensityScale,          &VolumetricCloudParameters::densityScale },
                { &BaseNoiseScaleM,       &VolumetricCloudParameters::baseNoiseScaleM },
                { &DetailNoiseScaleM,     &VolumetricCloudParameters::detailNoiseScaleM },
                { &DetailErosionStrength, &VolumetricCloudParameters::detailErosionStrength },
                { &WeatherMapScaleM,      &VolumetricCloudParameters::weatherMapScaleM },
                { &WindDirX,              &VolumetricCloudParameters::windDirX },
                { &WindDirZ,              &VolumetricCloudParameters::windDirZ },
                { &WindSpeedMPerS,        &VolumetricCloudParameters::windSpeedMPerS },
                { &PhaseG0,               &VolumetricCloudParameters::phaseG0 },
                { &PhaseG1,               &VolumetricCloudParameters::phaseG1 },
                { &PhaseBlend,            &VolumetricCloudParameters::phaseBlend },
                { &AmbientIntensity,      &VolumetricCloudParameters::ambientIntensity },
                { &BeerPowderStrength,    &VolumetricCloudParameters::beerPowderStrength },
                { &LightMarchStepM,       &VolumetricCloudParameters::lightMarchStepM },
                { &SunLightScale,         &VolumetricCloudParameters::sunLightScale },
                { &MsAttenuation,         &VolumetricCloudParameters::msAttenuation },
                { &MsContribution,        &VolumetricCloudParameters::msContribution },
                { &MsEccentricity,        &VolumetricCloudParameters::msEccentricity },
                { &EarlyExitTransmittance,&VolumetricCloudParameters::earlyExitTransmittance },
                { &MaxMarchDistanceM,     &VolumetricCloudParameters::maxMarchDistanceM },
                { &GodRayIntensity,       &VolumetricCloudParameters::godRayIntensity },
                { &GodRayMieBoost,        &VolumetricCloudParameters::godRayMieBoost },
                { &GodRayMaxDistanceM,    &VolumetricCloudParameters::godRayMaxDistanceM },
                { &CloudShadowRegionSizeM,&VolumetricCloudParameters::cloudShadowRegionSizeM },
            };

            constexpr Binding<CVar<int>, uint32_t> kUintBindings[] = {
                { &MaxSteps,          &VolumetricCloudParameters::maxSteps },
                { &ResolutionDivisor, &VolumetricCloudParameters::resolutionDivisor },
                { &GodRayStepCount,   &VolumetricCloudParameters::godRayStepCount },
            };

            constexpr Binding<CVar<bool>, bool> kBoolBindings[] = {
                { &GodRayEnabled, &VolumetricCloudParameters::godRayEnabled },
            };
        }

        void LoadInto(VolumetricCloudParameters& params)
        {
            for (const auto& b : kFloatBindings) {
                params.*(b.member) = b.cvar->Get();
            }
            for (const auto& b : kUintBindings) {
                params.*(b.member) = static_cast<uint32_t>(std::max(b.cvar->Get(), 0));
            }
            for (const auto& b : kBoolBindings) {
                params.*(b.member) = b.cvar->Get();
            }
        }
    }
}
