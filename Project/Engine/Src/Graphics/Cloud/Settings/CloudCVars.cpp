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

        CVar<float> BaseNoiseVerticalScale{
            "r.Cloud.BaseNoiseVerticalScale", 0.5f,
            "ベース形状ノイズの縦方向スケール倍率。小さいほど層内の縦の変化が細かい",
            CVarRange{ 0.05f, 2.0f } };

        CVar<float> HeightSkewM{
            "r.Cloud.HeightSkew", 500.0f,
            "高い位置ほど風下へずらす量 [m]。雲を斜めに立たせる",
            CVarRange{ 0.0f, 5000.0f } };

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
        CVar<float> DropletDiameterUm{
            "r.Cloud.DropletDiameter", 20.0f,
            "雲粒の直径 [µm]。大きいほど前方散乱が鋭くなり逆光の縁が強く光る",
            CVarRange{ 5.0f, 50.0f } };

        CVar<float> MaxPhase{
            "r.Cloud.MaxPhase", 500.0f,
            "位相関数の上限。Mie の前方ピークは太陽の視直径より鋭いので上限で丸める",
            CVarRange{ 1.0f, 20000.0f } };

        CVar<float> AmbientIntensity{
            "r.Cloud.AmbientIntensity", 1.15f,
            "雲に当たる環境光の強さ",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> AmbientCosZenith{
            "r.Cloud.AmbientCosZenith", 0.7f,
            "アンビエントで Sky-View LUT を引く仰角の cos（半球平均の代用）",
            CVarRange{ -1.0f, 1.0f } };

        CVar<float> AmbientBottomOcclusion{
            "r.Cloud.AmbientBottomOcclusion", 0.45f,
            "雲底に届く環境光の割合。1 で遮蔽なし",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> AmbientChroma{
            "r.Cloud.AmbientChroma", 0.35f,
            "環境光に残す彩度。0 で完全な灰色",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> AmbientGroundStrength{
            "r.Cloud.AmbientGroundStrength", 0.0f,
            "雲底へ届く地表反射光の倍率。明るい地表では雲塊の上下の陰影を消すので既定は 0",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> BeerPowderStrength{
            "r.Cloud.BeerPowderStrength", 0.5f,
            "Beer-Powder 効果の強さ（雲の縁の暗さ）",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> LightMarchCoverage{
            "r.Cloud.LightMarchCoverage", 1.3f,
            "サンライトマーチが覆う層内経路長の倍率。1 で層をちょうど貫く",
            CVarRange{ 0.25f, 4.0f } };

        CVar<float> LightMarchConeSpread{
            "r.Cloud.LightMarchConeSpread", 0.15f,
            "サンライトマーチのコーン半径（進んだ距離に対する比）。0 で直線マーチ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> MaxSunOpticalDepth{
            "r.Cloud.MaxSunOpticalDepth", 8.0f,
            "サンライトマーチの光学的深さの上限。小さいほど暗部に階調が残る",
            CVarRange{ 0.5f, 50.0f } };

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

        CVar<float> DetailFadeDistanceM{
            "r.Cloud.DetailFadeDistance", 90000.0f,
            "ディテール侵食の強さを弱めきる距離 [m]。短いほど遠方の雲が丸くなる",
            CVarRange{ 1000.0f, 200000.0f } };

        CVar<float> NoiseLodBias{
            "r.Cloud.NoiseLodBias", 0.0f,
            "ノイズのミップ段のオフセット。負で細かく（ざらつく）、正で粗く（ぼける）",
            CVarRange{ -2.0f, 2.0f } };

        CVar<float> CloudStreetStretch{
            "r.Cloud.CloudStreetStretch", 1.0f,
            "天候マップを風方向へ引き伸ばす倍率。1 で等方、大きいほど雲が筋状に並ぶ",
            CVarRange{ 1.0f, 6.0f } };

        CVar<float> CloudTopVariation{
            "r.Cloud.CloudTopVariation", 0.0f,
            "雲頂の高さを場所ごとにばらつかせる量。0 で全ての雲が同じ背丈になる",
            CVarRange{ 0.0f, 0.8f } };

        CVar<float> FarFadeWidthM{
            "r.Cloud.FarFadeWidth", 8000.0f,
            "マーチ最大距離の手前で密度をフェードさせる幅 [m]",
            CVarRange{ 100.0f, 50000.0f } };

        CVar<float> HazeDistanceM{
            "r.Cloud.HazeDistance", 60000.0f,
            "遠方の雲が空色へ溶けるまでの消散距離 [m]",
            CVarRange{ 1000.0f, 500000.0f } };

        CVar<int> MaxSteps{
            "r.Cloud.MaxSteps", 160,
            "レイマーチの最大ステップ数。多いほど高品質だが重い",
            CVarRange{ 8.0f, 512.0f } };

        CVar<int> ResolutionDivisor{
            "r.Cloud.ResolutionDivisor", 2,
            "雲バッファの解像度分割数。2 = 1/2 解像度",
            CVarRange{ 1.0f, 4.0f } };

        CVar<float> UpsampleDepthTolerance{
            "r.Cloud.UpsampleDepthTolerance", 0.15f,
            "アップサンプルでタップを半減させる相対距離差。0 で深度を見ない",
            CVarRange{ 0.0f, 1.0f } };

        CVar<bool> ReprojectEnabled{
            "r.Cloud.ReprojectEnabled", true,
            "前フレームのレイマーチ結果を混ぜて実効サンプル数を稼ぐ" };

        CVar<float> ReprojectBlendMin{
            "r.Cloud.ReprojectBlendMin", 0.12f,
            "履歴が使える画素の現フレーム寄与率。小さいほど収束が深いがゴーストに寄る",
            CVarRange{ 0.02f, 1.0f } };

        CVar<float> ReprojectTolerance{
            "r.Cloud.ReprojectTolerance", 0.25f,
            "履歴を棄却しはじめる透過率の食い違い量。小さいほど動きに敏感",
            CVarRange{ 0.01f, 1.0f } };

        // ---- 巻雲シェル ----
        CVar<float> CirrusAltitudeM{
            "r.Cloud.CirrusAltitude", 8000.0f,
            "巻雲シェルの高度 [m]。積雲層の雲頂より上に置くこと",
            CVarRange{ 3000.0f, 16000.0f } };

        CVar<float> CirrusCoverage{
            "r.Cloud.CirrusCoverage", 0.0f,
            "巻雲の量。0 で巻雲を出さない。密度場が筋雲にならないため既定は 0",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> CirrusDensity{
            "r.Cloud.CirrusDensity", 2.0f,
            "巻雲の光学的深さのスケール。大きいほど濃く不透明になる",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> CirrusScaleM{
            "r.Cloud.CirrusScale", 18000.0f,
            "巻雲の模様 1 タイルの実寸 [m]",
            CVarRange{ 10000.0f, 400000.0f } };

        CVar<float> CirrusStretch{
            "r.Cloud.CirrusStretch", 3.0f,
            "巻雲の模様を風方向へ引き伸ばす倍率。大きいほど筋が細長くなる",
            CVarRange{ 1.0f, 20.0f } };

        CVar<float> CirrusWindScale{
            "r.Cloud.CirrusWindScale", 2.5f,
            "下層に対する巻雲の移流速度倍率",
            CVarRange{ 0.0f, 10.0f } };

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

        CVar<float> SceneShadowStrength{
            "r.Cloud.SceneShadowStrength", 1.0f,
            "シーンへ落とす雲影の強さ。0 で地面に雲影が出なくなる",
            CVarRange{ 0.0f, 1.0f } };

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
                { &BaseNoiseVerticalScale, &VolumetricCloudParameters::baseNoiseVerticalScale },
                { &HeightSkewM,          &VolumetricCloudParameters::heightSkewM },
                { &WindDirX,              &VolumetricCloudParameters::windDirX },
                { &WindDirZ,              &VolumetricCloudParameters::windDirZ },
                { &WindSpeedMPerS,        &VolumetricCloudParameters::windSpeedMPerS },
                { &DropletDiameterUm,     &VolumetricCloudParameters::dropletDiameterUm },
                { &MaxPhase,              &VolumetricCloudParameters::maxPhase },
                { &AmbientIntensity,      &VolumetricCloudParameters::ambientIntensity },
                { &AmbientCosZenith,       &VolumetricCloudParameters::ambientCosZenith },
                { &AmbientBottomOcclusion, &VolumetricCloudParameters::ambientBottomOcclusion },
                { &AmbientChroma,         &VolumetricCloudParameters::ambientChroma },
                { &AmbientGroundStrength, &VolumetricCloudParameters::ambientGroundStrength },
                { &BeerPowderStrength,    &VolumetricCloudParameters::beerPowderStrength },
                { &LightMarchCoverage,    &VolumetricCloudParameters::lightMarchCoverage },
                { &LightMarchConeSpread,  &VolumetricCloudParameters::lightMarchConeSpread },
                { &MaxSunOpticalDepth,    &VolumetricCloudParameters::maxSunOpticalDepth },
                { &SunLightScale,         &VolumetricCloudParameters::sunLightScale },
                { &MsAttenuation,         &VolumetricCloudParameters::msAttenuation },
                { &MsContribution,        &VolumetricCloudParameters::msContribution },
                { &MsEccentricity,        &VolumetricCloudParameters::msEccentricity },
                { &EarlyExitTransmittance,&VolumetricCloudParameters::earlyExitTransmittance },
                { &MaxMarchDistanceM,     &VolumetricCloudParameters::maxMarchDistanceM },
                { &DetailFadeDistanceM,   &VolumetricCloudParameters::detailFadeDistanceM },
                { &NoiseLodBias,          &VolumetricCloudParameters::noiseLodBias },
                { &CloudStreetStretch,    &VolumetricCloudParameters::cloudStreetStretch },
                { &CloudTopVariation,     &VolumetricCloudParameters::cloudTopVariation },
                { &FarFadeWidthM,         &VolumetricCloudParameters::farFadeWidthM },
                { &HazeDistanceM,         &VolumetricCloudParameters::hazeDistanceM },
                { &CirrusAltitudeM,       &VolumetricCloudParameters::cirrusAltitudeM },
                { &CirrusCoverage,        &VolumetricCloudParameters::cirrusCoverage },
                { &CirrusDensity,         &VolumetricCloudParameters::cirrusDensity },
                { &CirrusScaleM,          &VolumetricCloudParameters::cirrusScaleM },
                { &CirrusStretch,         &VolumetricCloudParameters::cirrusStretch },
                { &CirrusWindScale,       &VolumetricCloudParameters::cirrusWindScale },
                { &GodRayIntensity,       &VolumetricCloudParameters::godRayIntensity },
                { &GodRayMieBoost,        &VolumetricCloudParameters::godRayMieBoost },
                { &GodRayMaxDistanceM,    &VolumetricCloudParameters::godRayMaxDistanceM },
                { &CloudShadowRegionSizeM,&VolumetricCloudParameters::cloudShadowRegionSizeM },
                { &SceneShadowStrength,    &VolumetricCloudParameters::sceneShadowStrength },
                { &UpsampleDepthTolerance, &VolumetricCloudParameters::upsampleDepthTolerance },
                { &ReprojectBlendMin,      &VolumetricCloudParameters::reprojectBlendMin },
                { &ReprojectTolerance,     &VolumetricCloudParameters::reprojectTolerance },
            };

            constexpr Binding<CVar<int>, uint32_t> kUintBindings[] = {
                { &MaxSteps,          &VolumetricCloudParameters::maxSteps },
                { &ResolutionDivisor, &VolumetricCloudParameters::resolutionDivisor },
                { &GodRayStepCount,   &VolumetricCloudParameters::godRayStepCount },
            };

            constexpr Binding<CVar<bool>, bool> kBoolBindings[] = {
                { &GodRayEnabled,     &VolumetricCloudParameters::godRayEnabled },
                { &ReprojectEnabled,  &VolumetricCloudParameters::reprojectEnabled },
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
