#include "pch.h"
#include "WaterCVars.h"

#include "Graphics/Water/WaterFoamDefaults.h"

namespace CoreEngine
{
    namespace WaterCVars
    {
        // 既定値は旧 Water.json（2026-08 時点の調整値）を引き継ぐ。
        // 命名は "r.Water.<グループ>.<名前>"（CVar ツリーがドット区切りで階層化する）。

        // ---- 見た目 ----
        CVar<Vector4> BaseColor{ "r.Water.BaseColor",
            Vector4{ 0.04f, 0.18f, 0.28f, 0.85f },
            "反射が無いときの水面の色（DXR の反射も空のキューブも無いときだけ使う。αは使わない）" };
        CVar<float> Roughness{ "r.Water.Roughness", 0.03f,
            "水面の粗さ（ハイライトの広がり）", CVarRange{ 0.0f, 1.0f } };
        CVar<float> FresnelScale{ "r.Water.FresnelScale", 1.0f,
            "反射の強さの倍率（1.0 = 物理値）", CVarRange{ 0.0f, 2.0f } };
        CVar<float> FresnelF0{ "r.Water.FresnelF0", 0.02f,
            "正面入射時の反射率 F0（水 ≈ 0.02）", CVarRange{ 0.0f, 0.1f } };
        CVar<bool> DepthFadeEnabled{ "r.Water.DepthFadeEnabled", true,
            "Depth Fade（水柱厚さによる Beer-Lambert 透過）を有効にする" };
        // ★PSO 構築時に一度だけ読まれる（起動時固定）★
        // 水面 PSO の RT 枚数と WaterSurfacePass の OMSetRenderTargets 枚数を
        // 同時に切り替えるためのフラグ。実行中に変えても PSO は再構築されない。
        CVar<bool> WriteMotionVector{ "r.Water.WriteMotionVector", true,
            "水面が SV_TARGET1 へモーションベクターを書く（TAA の再投影用。起動時に確定）" };

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

        // ---- 泡（whitecap）----
        // 既定値は WaterFoamDefaults を参照する（WaterFrameConstants /
        // WaterFoamSystem::Settings / WhitecapConstants と同じ定数）。以前はここだけ旧 Water.json の
        // 調整値（Bias 0.806 / DecaySeconds 3.07）が残り、他 3 か所と食い違っていた。
        CVar<bool> FoamEnabled{ "r.Water.Foam.Enabled", WaterFoamDefaults::kEnabled,
            "泡（whitecap / 岸際泡）を有効にする（FFTOcean 専用）" };
        CVar<float> FoamWhitecapScale{ "r.Water.Foam.WhitecapScale", WaterFoamDefaults::kWhitecapScale,
            "白波の被覆率の倍率（1.0 = Monahan の観測式 W = 3.84e-6·U^3.41）", CVarRange{ 0.0f, 4.0f } };
        CVar<float> FoamOpacity{ "r.Water.Foam.Opacity", WaterFoamDefaults::kOpacity,
            "泡レイヤの不透明度（1.0 の白ベタは禁止・水面下の情報を残す）", CVarRange{ 0.0f, 1.0f } };
        CVar<Vector3> FoamCascadeWeights{ "r.Water.Foam.CascadeWeights",
            Vector3{ WaterFoamDefaults::kCascadeWeights[0],
                     WaterFoamDefaults::kCascadeWeights[1],
                     WaterFoamDefaults::kCascadeWeights[2] },
            "カスケード別の勾配寄与の重み (大/中/小)", CVarRange{ 0.0f, 1.0f } };
        CVar<float> FoamDecaySeconds{ "r.Water.Foam.DecaySeconds", WaterFoamDefaults::kDecaySeconds,
            "泡の寿命 τ [s]（e^-1 減衰時間）", CVarRange{ 0.2f, 10.0f } };

        // ---- FFT Ocean ----
        // 波高とピーク波長は JONSWAP のフェッチ制限成長則で「風速 × 吹送距離」から
        // 決まる（完全発達 = Pierson-Moskowitz でクランプ）。
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
        CVar<float> FFTFetchKilometers{ "r.Water.FFT.FetchKm", 200.0f,
            "吹送距離 [km]（風速と合わせてピーク波長と波高を決める。短い=若く尖った風波 / "
            "長い=完全発達したうねり。完全発達を超えては育たない）", CVarRange{ 1.0f, 2000.0f } };
        CVar<float> FFTChoppiness{ "r.Water.FFT.Choppiness", 1.8f,
            "水平変位の強さ（波頭の尖り）", CVarRange{ 0.0f, 4.0f } };
        CVar<int> FFTActiveComponentCount{ "r.Water.FFT.ActiveComponentCount", 48,
            "スペクトル成分数（帯域幅）", CVarRange{ 1.0f, 64.0f } };

        // ---- うねり（swell）----
        // 遠方の低気圧で発生し分散しながら到達した長周期波。その場の風とは独立なので、
        // 風速ではなく波高・周期・向きで直接与える。風波と別方向にすると斜交する波列
        // （クロスシー）になり、風速を落としてもうねりだけが残る。
        CVar<bool> SwellEnabled{ "r.Water.Swell.Enabled", true,
            "うねり成分（遠方由来の長周期波）を重ねる" };
        CVar<float> SwellHeight{ "r.Water.Swell.Height", 1.2f,
            "うねりの有義波高 Hs [m]", CVarRange{ 0.0f, 12.0f } };
        CVar<float> SwellPeriod{ "r.Water.Swell.Period", 12.0f,
            "うねりのピーク周期 [s]（波長 λ = gT²/2π。12s → 約225m）",
            CVarRange{ 2.0f, 30.0f } };
        CVar<Vector2> SwellDirection{ "r.Water.Swell.Direction",
            Vector2{ 0.3420201433f, -0.9396926208f },
            "うねりの進行方向（XZ。適用時に正規化。風向と変えると斜交波になる）",
            CVarRange{ -1.0f, 1.0f } };
        CVar<float> SwellRelativeWidth{ "r.Water.Swell.RelativeWidth", 0.06f,
            "うねりの周波数帯域幅（ωs に対する比。小さいほど周期が揃う）",
            CVarRange{ 0.01f, 0.5f } };
        CVar<float> SwellSpread{ "r.Water.Swell.Spread", 24.0f,
            "うねりの指向の鋭さ s（大きいほど一方向に揃う）", CVarRange{ 1.0f, 60.0f } };

        // ---- 岸 ----
        CVar<bool> ShoreFineMesh{ "r.Water.Shore.FineMesh", true,
            "カメラの周りの範囲（海底の高さを測る範囲）を細かい格子で描き、短い波まで形に入れる（FFTOcean 専用）" };

        // ---- 海の範囲マップ ----
        CVar<bool> SeaMapEnabled{ "r.Water.SeaMap.Enabled", true,
            "水面の範囲全体の水深と上の物までの高さをレイトレーシングで測り、陸の下・浅瀬・桟橋の下で波を弱める" };
        CVar<int> SeaMapResolution{ "r.Water.SeaMap.Resolution", 2048,
            "海の範囲マップの長い辺のテクセル数（細かいほど岸の形に沿う）", CVarRange{ 256.0f, 4096.0f } };

        // ---- 濡れ跡 ----
        CVar<bool> WetnessEnabled{ "r.Water.Wetness.Enabled", true,
            "水面が下がったあと、水に浸かっていた面を濡れた見た目（暗く・なめらか）にする" };
        CVar<float> WetnessDrySeconds{ "r.Water.Wetness.DrySeconds", 60.0f,
            "水から出た面の濡れ具合が 1/e になるまでの時間 [s]", CVarRange{ 1.0f, 3600.0f } };

        // ---- コースティクス ----
        CVar<int> CausticsBackend{ "r.Water.Caustics.Backend", 0,
            "生成方式（0: レイトレーシング / 1: スクリーンスペース。SS は Gerstner 専用）",
            CVarRange{}, CVarFlags::NoUI };
        CVar<float> CausticsIntensity{ "r.Water.Caustics.Intensity", 0.35f,
            "コースティクスの強さ（スクリーンスペース方式だけ。1.0 = 物理値）", CVarRange{ 0.0f, 4.0f } };
        CVar<float> CausticsDepthAttenuation{ "r.Water.Caustics.DepthAttenuation", 1.5f,
            "深さによる減衰（スクリーンスペース方式だけ）", CVarRange{ 0.0f, 8.0f } };
        CVar<float> CausticsCurvatureScale{ "r.Water.Caustics.CurvatureScale", 1.5f,
            "波面の曲がりによる集光の倍率（スクリーンスペース方式だけ）", CVarRange{ 0.0f, 8.0f } };
        CVar<float> CausticsSurfaceSampleRadius{ "r.Water.Caustics.SurfaceSampleRadius", 0.35f,
            "水面を調べる半径 [m]（スクリーンスペース方式だけ）", CVarRange{ 0.01f, 4.0f } };
        CVar<float> CausticsRefractiveIndex{ "r.Water.Caustics.RefractiveIndex", 1.333f,
            "屈折率（RT / スクリーンスペース両方へ同値を反映）", CVarRange{ 1.0f, 2.0f } };
        CVar<float> CausticsReceiverNormalStrength{ "r.Water.Caustics.ReceiverNormalStrength", 1.0f,
            "受ける面の向きの効き方（スクリーンスペース方式だけ）", CVarRange{ 0.0f, 2.0f } };
        CVar<float> CausticsAlignmentPower{ "r.Water.Caustics.AlignmentPower", 6.0f,
            "集光の線の鋭さ（スクリーンスペース方式だけ）", CVarRange{ 0.1f, 16.0f } };

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
