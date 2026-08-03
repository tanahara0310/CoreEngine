#include "pch.h"
#include "WaterCVars.h"

namespace CoreEngine
{
    namespace WaterCVars
    {
        // 既定値は旧 Water.json（2026-08 時点の調整値）を引き継ぐ。
        // 命名は "r.Water.<グループ>.<名前>"（CVar ツリーがドット区切りで階層化する）。

        // ---- 見た目 ----
        CVar<Vector4> BaseColor{ "r.Water.BaseColor",
            Vector4{ 0.04f, 0.18f, 0.28f, 0.85f },
            "水面のベースカラー（αはフレネル反射成分の不透明度として働く）" };
        CVar<float> Roughness{ "r.Water.Roughness", 0.03f,
            "水面マテリアルのラフネス", CVarRange{ 0.0f, 1.0f } };
        CVar<float> Metallic{ "r.Water.Metallic", 0.0f,
            "水面マテリアルのメタリック", CVarRange{ 0.0f, 1.0f } };
        CVar<bool> IBLEnabled{ "r.Water.IBLEnabled", true,
            "水面マテリアルの IBL を有効にする" };
        CVar<float> FresnelScale{ "r.Water.FresnelScale", 1.0f,
            "フレネル反射スケール", CVarRange{ 0.0f, 2.0f } };
        CVar<float> FresnelF0{ "r.Water.FresnelF0", 0.02f,
            "正面入射時の反射率 F0（水 ≈ 0.02）", CVarRange{ 0.0f, 0.1f } };
        CVar<Vector2> ScrollSpeed{ "r.Water.ScrollSpeed",
            Vector2{ 0.03f, 0.01f },
            "UV スクロール速度 (U, V)", CVarRange{ -1.0f, 1.0f } };
        CVar<Vector2> UVTiling{ "r.Water.UVTiling",
            Vector2{ 4.0f, 4.0f },
            "UV タイリング (U, V)", CVarRange{ 0.1f, 32.0f } };
        CVar<bool> DepthFadeEnabled{ "r.Water.DepthFadeEnabled", true,
            "Depth Fade（水柱厚さによる Beer-Lambert 透過）を有効にする" };

        // ---- 水質 ----
        CVar<Vector3> AbsorptionBase{ "r.Water.AbsorptionBase",
            Vector3{ 0.45f, 0.09f, 0.06f },
            "吸収係数 σa のベース値 [1/m]（RGB。赤 > 緑 > 青 が水の青さの物理）",
            CVarRange{ 0.0f, 4.0f } };
        CVar<Vector3> ScatteringBase{ "r.Water.ScatteringBase",
            Vector3{ 0.010f, 0.032f, 0.028f },
            "散乱係数 σs のベース値 [1/m]（RGB。深瀬のインスキャッタ色を決める）",
            CVarRange{ 0.0f, 2.0f } };
        CVar<float> Turbidity{ "r.Water.Turbidity", 0.0f,
            "濁度。青の吸収と粒子散乱を加算する（緑濁り方向）", CVarRange{ 0.0f, 1.0f } };
        CVar<int> JerlovPreset{ "r.Water.JerlovPreset", 0,
            "Jerlov 水型コンボの選択インデックス（実体は AbsorptionBase / ScatteringBase）",
            CVarRange{}, CVarFlags::NoUI };

        // ---- 泡（whitecap）----
        // 既定値は WaterFrameConstants の初期値と一致させる（cascadeWeights {1,0.5,0.2} は
        // 飽和対策の較正値。無重みでは 31m カスケードが支配して detJ が広範囲で飽和する）
        CVar<bool> FoamEnabled{ "r.Water.Foam.Enabled", true,
            "泡（whitecap / 岸際泡）を有効にする（FFTOcean 専用）" };
        CVar<float> FoamBias{ "r.Water.Foam.Bias", 0.806f,
            "発生しきい値（合成 detJ がこれ未満で泡）", CVarRange{ 0.0f, 1.5f } };
        CVar<float> FoamGain{ "r.Water.Foam.Gain", 4.0f,
            "しきい値からの立ち上がり勾配", CVarRange{ 0.5f, 16.0f } };
        CVar<float> FoamOpacity{ "r.Water.Foam.Opacity", 0.9f,
            "泡レイヤの不透明度（1.0 の白ベタは禁止・水面下の情報を残す）", CVarRange{ 0.0f, 1.0f } };
        CVar<Vector3> FoamCascadeWeights{ "r.Water.Foam.CascadeWeights",
            Vector3{ 1.0f, 0.5f, 0.2f },
            "カスケード別の勾配寄与の重み (大/中/小)", CVarRange{ 0.0f, 1.0f } };
        CVar<float> FoamDecaySeconds{ "r.Water.Foam.DecaySeconds", 3.07f,
            "泡の寿命 τ [s]（e^-1 減衰時間）", CVarRange{ 0.2f, 10.0f } };

        // ---- FFT Ocean ----
        // 波高は Pierson-Moskowitz 較正（Hs ≈ 0.21 v²/g）で風速から決まる。
        // AmplitudeScale は較正済み波高に対する相対倍率（基本 1.0 前後）。
        CVar<bool> FFTEnabled{ "r.Water.FFT.Enabled", true,
            "FFT Ocean（マルチスケール海面）を使用する（off = Gerstner）" };
        CVar<float> FFTAmplitudeScale{ "r.Water.FFT.AmplitudeScale", 1.0f,
            "較正済み波高に対する相対倍率", CVarRange{ 0.0f, 4.0f } };
        CVar<Vector2> FFTWindDirection{ "r.Water.FFT.WindDirection",
            Vector2{ 0.8411784172f, 0.5407575965f },
            "風向（XZ。適用時に正規化される）", CVarRange{ -1.0f, 1.0f } };
        CVar<float> FFTWindSpeed{ "r.Water.FFT.WindSpeed", 18.0f,
            "風速 [m/s]（波高はこれだけで決まる。上限 32m/s で飽和）", CVarRange{ 0.0f, 64.0f } };
        CVar<float> FFTChoppiness{ "r.Water.FFT.Choppiness", 1.8f,
            "水平変位の強さ（波頭の尖り）", CVarRange{ 0.0f, 4.0f } };
        CVar<int> FFTActiveComponentCount{ "r.Water.FFT.ActiveComponentCount", 48,
            "スペクトル成分数（帯域幅）", CVarRange{ 1.0f, 64.0f } };
        CVar<float> FFTGravity{ "r.Water.FFT.Gravity", 9.81f,
            "重力加速度 [m/s²]（分散関係 ω = √(gk) に効く）", CVarRange{ 0.1f, 20.0f } };

        // ---- コースティクス ----
        CVar<int> CausticsBackend{ "r.Water.Caustics.Backend", 0,
            "生成方式（0: レイトレーシング / 1: スクリーンスペース。SS は Gerstner 専用）",
            CVarRange{}, CVarFlags::NoUI };
        CVar<float> CausticsIntensity{ "r.Water.Caustics.Intensity", 0.35f,
            "コースティクス強度（1.0 = 物理値）", CVarRange{ 0.0f, 4.0f } };
        CVar<float> CausticsDepthAttenuation{ "r.Water.Caustics.DepthAttenuation", 1.5f,
            "深度による減衰", CVarRange{ 0.0f, 8.0f } };
        CVar<float> CausticsCurvatureScale{ "r.Water.Caustics.CurvatureScale", 1.5f,
            "波面曲率による集光のスケール", CVarRange{ 0.0f, 8.0f } };
        CVar<float> CausticsSurfaceSampleRadius{ "r.Water.Caustics.SurfaceSampleRadius", 0.35f,
            "水面サンプル半径", CVarRange{ 0.01f, 4.0f } };
        CVar<float> CausticsRefractiveIndex{ "r.Water.Caustics.RefractiveIndex", 1.333f,
            "屈折率（RT / スクリーンスペース両方へ同値を反映）", CVarRange{ 1.0f, 2.0f } };
        CVar<float> CausticsReceiverNormalStrength{ "r.Water.Caustics.ReceiverNormalStrength", 1.0f,
            "受光面法線の寄与", CVarRange{ 0.0f, 2.0f } };
        CVar<float> CausticsAlignmentPower{ "r.Water.Caustics.AlignmentPower", 6.0f,
            "集光の整列強調", CVarRange{ 0.1f, 16.0f } };

        // ---- DXR 屈折 ----
        CVar<float> RefractionMaxOffsetPixels{ "r.Water.Refraction.MaxOffsetPixels", 0.0f,
            "屈折先のスクリーン再投影ずれ量の上限 [px]（0 = 無制限＝物理的に正しい RT 屈折）",
            CVarRange{ 0.0f, 64.0f } };

        namespace
        {
            // 濁度 1.0 のときにベース係数へ加算する量 [1/m]。
            // 吸収は CDOM・植物プランクトンによる短波長（青）優位の吸収、
            // 散乱は懸濁粒子によるほぼ無選択（わずかに短波長寄り）の散乱を表す。
            // （旧 WaterSurfaceParameterPanel の kTurbidity*Gain から移設）
            constexpr float kTurbidityAbsorptionGain[3] = { 0.05f, 0.08f, 0.35f };
            constexpr float kTurbidityScatteringGain[3] = { 0.22f, 0.26f, 0.20f };
        }

        Vector3 EffectiveAbsorption()
        {
            const Vector3& base = AbsorptionBase.Get();
            const float turbidity = Turbidity.Get();
            return Vector3{
                base.x + turbidity * kTurbidityAbsorptionGain[0],
                base.y + turbidity * kTurbidityAbsorptionGain[1],
                base.z + turbidity * kTurbidityAbsorptionGain[2] };
        }

        Vector3 EffectiveScattering()
        {
            const Vector3& base = ScatteringBase.Get();
            const float turbidity = Turbidity.Get();
            return Vector3{
                base.x + turbidity * kTurbidityScatteringGain[0],
                base.y + turbidity * kTurbidityScatteringGain[1],
                base.z + turbidity * kTurbidityScatteringGain[2] };
        }
    }
}
