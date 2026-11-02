#include "pch.h"
#include "WaterCVars.h"

namespace CoreEngine
{
    namespace WaterCVars
    {
        // 命名は "r.Water.<グループ>.<名前>"（CVar ツリーがドット区切りで階層化する）。

        // ---- 描画 ----
        // ★PSO 構築時に一度だけ読まれる（起動時固定）★
        // 水面 PSO の RT 枚数と WaterSurfacePass の OMSetRenderTargets 枚数を
        // 同時に切り替えるためのフラグ。実行中に変えても PSO は再構築されない。
        CVar<bool> WriteMotionVector{ "r.Water.WriteMotionVector", true,
            "水面が SV_TARGET1 へモーションベクターを書く（TAA の再投影用。起動時に確定）" };

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
        CVar<float> CausticsReceiverNormalStrength{ "r.Water.Caustics.ReceiverNormalStrength", 1.0f,
            "受ける面の向きの効き方（スクリーンスペース方式だけ）", CVarRange{ 0.0f, 2.0f } };
        CVar<float> CausticsAlignmentPower{ "r.Water.Caustics.AlignmentPower", 6.0f,
            "集光の線の鋭さ（スクリーンスペース方式だけ）", CVarRange{ 0.1f, 16.0f } };

        // ---- DXR 屈折 ----
        CVar<float> RefractionMaxOffsetPixels{ "r.Water.Refraction.MaxOffsetPixels", 0.0f,
            "屈折先のスクリーン再投影ずれ量の上限 [px]（0 = 無制限＝物理的に正しい RT 屈折）",
            CVarRange{ 0.0f, 64.0f } };
    }
}
