// ============================================================
// 波峰のサブサーフェス透過（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 逆光のとき、波の薄い部分（波頭）を光が通り抜けて前方散乱し、波の背が
// エメラルドに光る現象。実海の見た目で最も強い記号のひとつ。
//
// なぜ今まで出なかったか:
//   既存の体積色（WaterVolume.hlsli）が扱うのは「視線の上り光路」だけで、
//   インスキャッタも上向き法線の等方 ambient のみ。視線方向にも太陽方向にも
//   依存しないため、どの角度から見ても同じ暗い青にしかならなかった。
//
// 色は決め打ちのティントではなく、既存の σa / σs から単一散乱の解析解で導く。
// 赤が先に吸収されて緑〜シアンが残るという物理がそのまま色になるので、
// Jerlov プリセットや濁度を変えると透過色も自動で追従する。
//
// 【include 位置の契約】Water.PS.hlsl のリソース宣言・WaterFrameConstants(b5) の
// 後で include すること。以下に暗黙依存する:
//   資源 : gLightCounts / gDirectionalLights（Object3dForward.hlsli）
//   型   : WaterPSInput（waveHeight を持つこと）
// ============================================================
#ifndef WATER_SUBSURFACE_INCLUDED
#define WATER_SUBSURFACE_INCLUDED

// 効果が飽和する波頭の高さ [m]。これ以上高くても薄さは頭打ちにする。
// 1.5 → 3.0: 1.5 だと風速14m/s で「海面より上」の大半が飽和してしまい、
// 波頭のハイライトではなく逆光側の水面全体が緑に染まった（実測）。
// 実際に光が抜けるのは波の一番高い薄い部分だけなので、そこまで届かせない。
static const float kSubsurfaceCrestHeight = 3.0f;
// 光が波を抜けるときの実効光路長の倍率（波頭高さに対する比）。
// 大きいほど吸収が進み、緑がより濃く・暗くなる。
static const float kSubsurfacePathScale = 1.2f;
// 前方散乱方向を法線側へ歪ませる量（Barré-Brisebois の近似）。
// 0 = 純粋な逆光方向のみ、大きいほど波の面の向きに反応する。
static const float kSubsurfaceDistortion = 0.35f;
// 前方散乱ローブの鋭さ。大きいほど「太陽の真正面だけ」に絞られる。
// 4 → 6: 4 だと逆光側の海面が一様に緑がかって「水面全体に色を塗った」ように見えた。
static const float kSubsurfacePower = 6.0f;
// 全体の強さ。0 で無効化できる（シェーダーは実行時コンパイルなので再ビルド不要）。
// crest マスクを二乗＋飽和高さ3.0m へ厳しくした分、強度は 1.0 のままでよい
// （マスク側で約1/4になっている）。強すぎ／弱すぎはまずここを触る。
static const float kSubsurfaceScale = 1.0f;

/// @brief 波峰を透過してくる前方散乱光を求める
/// @param surfaceNormal 水面法線（フルディテール）
/// @param viewDir       水面 → 視点
/// @param waveHeight    静止水面からの波の高さ [m]（VS からの補間値）
/// @param sigmaS        散乱係数 σs [1/m]
/// @param sigmaT        消散係数 σt = σa + σs [1/m]
/// @param foamCoverage  泡の被覆率（泡は不透明な散乱層なので透過を持たない）
/// @return 水面から視点へ出てくる放射輝度（合成後に加算する）
float3 ComputeWaterSubsurfaceScattering(
    float3 surfaceNormal,
    float3 viewDir,
    float waveHeight,
    float3 sigmaS,
    float3 sigmaT,
    float foamCoverage)
{
    // 静止水面より上に持ち上がった部分だけが「薄い水」。谷は下に厚い水があるので通さない。
    // 二乗して波頭寄りへ偏らせる。線形だと「海面より上」全部が均等に光ってしまい、
    // 波頭のハイライトではなく水面全体への色塗りに見える（実測）。
    const float crestLinear = saturate(waveHeight / kSubsurfaceCrestHeight);
    const float crest = crestLinear * crestLinear;
    if (crest <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    // 光が波の中を進む実効光路長。長いほど散乱量は増えるが吸収も進む。
    const float pathLength = crest * kSubsurfaceCrestHeight * kSubsurfacePathScale;

    // 均質媒質の単一散乱解析解。ComputeWaterVolumetricColor のインスキャッタ項と同形なので、
    // 「水面越しに見える色」と「波を透過してくる色」が同じ光学定数から導かれ整合する。
    const float3 inscatter = (sigmaS / sigmaT) * (1.0f - exp(-sigmaT * pathLength));

    float3 transmitted = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        const float3 lightVec = -normalize(gDirectionalLights[i].direction); // 水面 → 光源

        // 前方散乱の射出方向。逆光（視線と光源が向かい合う）ほど強くなる。
        // 法線で歪ませることで、波の面の傾きに応じて光り方が変わる。
        const float3 scatterDir = normalize(lightVec + surfaceNormal * kSubsurfaceDistortion);
        const float backLit = pow(saturate(dot(viewDir, -scatterDir)), kSubsurfacePower);
        if (backLit <= 0.0f)
        {
            continue;
        }

        // 太陽色には大気の Transmittance 減衰が乗算済みなので、日没では透過光も赤みへ寄る。
        // 拡散的な射出なので放射照度 → 放射輝度の /π を掛ける（ComputeFoamColor と同じ規約）。
        transmitted += gDirectionalLights[i].color.rgb
            * gDirectionalLights[i].intensity * backLit / PI;
    }

    // 泡は不透明な気泡層なので透過しない。
    return transmitted * inscatter * crest * kSubsurfaceScale * (1.0f - saturate(foamCoverage));
}

#endif // WATER_SUBSURFACE_INCLUDED
