// ============================================================
// 水中ライティング（DeferredLighting.PS.hlsl 専用の関数群）
// ------------------------------------------------------------
// RT コースティクスによる「メインライト直接光の置換」まわりの一式:
//   - 水中判定コンテキスト（coverage α・鉛直水深・アンビエント透過率）
//   - 濡れ暗色化（wet surface darkening）
//   - コースティクスの物理合成（×kD×albedo/PI ＋ 水上影の引き継ぎ）
//   - 波打ち際アーティファクト切り分け用のデバッグ可視化（モード 3/4）
//
// 以前は DeferredLighting.PS.hlsl 本体に約 208 行（30%）が直書きされていた。
// ライトループ内の置換（underwaterFactor によるクロスフェード）だけは
// albedo/F0/metallic を要するため本体に残る。
//
// 【include の前提】PI（PBR.hlsli）と Luminance（ColorSpace.hlsli）を使うため、
// それらの後で include すること。リソース宣言は持たない（値渡しのみ）。
// ============================================================
#ifndef UNDERWATER_LIGHTING_INCLUDED
#define UNDERWATER_LIGHTING_INCLUDED

/// @brief 水中ライティング置換に必要な値一式
struct UnderwaterContext
{
    float factor;                 // 0 = 水上（通常ライティング）、1 = 水中（コースティクスが直接光を代替）
    float submergedDepth;         // 平坦基準水面からの鉛直水深 [m]
    float3 ambientTransmittance;  // アンビエント用 Beer–Lambert 透過率
};

/// @brief 水中判定コンテキストを構築する
/// @details ★置換率は自前で計算せず、コースティクス出力の α をそのまま使う★（2026-07-27）
///          旧実装は平坦な waterHeight による深度判定で、実際の水面（波変位後の汀線）との
///          間に「水は被っているのに置換もコースティクスも効かない帯」が生じていた。
///          RTWaterCaustics.hlsl は全画面ピクセルで波を評価し、波込みの被覆率
///          （crossFade × regionFade）を α に書いてくるため、それを使えば置換率と
///          コースティクス放射がピクセル単位で必ず一致する（帯が構造的に出ない）。
UnderwaterContext BuildUnderwaterContext(
    float4 waterCausticsSample,
    uint waterVolumeEnabled,
    float waterHeight,
    float3 absorptionCoeff,
    float3 worldPos)
{
    UnderwaterContext ctx;
    ctx.factor = 0.0f;
    ctx.submergedDepth = 0.0f;
    ctx.ambientTransmittance = 1.0f.xxx;

    if (waterVolumeEnabled != 0)
    {
        ctx.factor = saturate(waterCausticsSample.a);
        ctx.submergedDepth = waterHeight - worldPos.y;
        // アンビエント用の Beer–Lambert 透過率（鉛直水深の平坦近似。
        // 低周波成分なので平坦基準のままで視覚差は出ない）
        ctx.ambientTransmittance = lerp(
            1.0f.xxx,
            exp(-absorptionCoeff * max(ctx.submergedDepth, 0.0f)),
            ctx.factor);
    }
    return ctx;
}

/// @brief 濡れ暗色化（wet surface darkening）を albedo へ適用する
/// @details 水柱が数 cm の極浅域は 透過率 ≈ 1・集光率 ≈ 1 で、乾いた砂のアルベドのまま
///          描かれてしまう。その上に水面のフレネル照り返しだけが薄く乗るため、
///          「明るい砂の上に青白い膜が浮いた層」に見える（波線と平行な薄い層の正体の一つ）。
///          実際の濡れた多孔質面（砂・岩）は、隙間が水で満たされて屈折率差が減り、
///          内部全反射と前方散乱の増加で拡散反射率が乾燥時の 0.5〜0.7 倍へ落ちる。
///          F0（鏡面反射率）は濡れても変わらないので触らない
///          （★この関数は F0 計算より後に呼ぶこと★）。
float3 ApplyWetDarkening(float3 albedo, float underwaterFactor)
{
    const float kWetAlbedoFactor = 0.62f;
    return albedo * lerp(1.0f, kWetAlbedoFactor, underwaterFactor);
}

/// @brief RT コースティクス（水中の透過直接光 = 放射照度 E）を放射輝度へ合成する
/// @details ★「/ PI」を必ず入れること★
///          置換前のメインライト直接光は CalculatePBRLighting() → DiffuseLambertPBR() を
///          通っており、その中身は kD * albedo / PI（ランバート面の BRDF）である。
///          放射照度 E に albedo を掛けただけでは PI ≈ 3.14 倍（+1.65EV）の過大評価になり、
///          水面線をまたいだ瞬間に砂が 3 倍明るくなる。水柱が薄い波打ち際ほど吸収で
///          減衰しないため「岸に張り付いた白い線」として最も強く出る（2026-07-26 の回帰）。
/// @param mainLightShadow メインライトの RT シャドウマスク値（未使用時は 1.0 を渡す）。
///        コースティクスのレイトレースは水中区間しか遮蔽判定していないため、
///        水上（太陽→水面）のヤシ・岩・島の影をここで引き継ぐ。置換前の直接光と
///        同一マスク・同じ 0.3 フロアなので水面をまたいでも影の濃さが連続する。
///        水中区間の遮蔽はコースティクス側が 0 を返すため二重にはならない。
float3 CompositeUnderwaterCaustics(
    float3 causticsRadiance,
    float3 albedo,
    float3 F0,
    float metallic,
    float mainLightShadow)
{
    // kD も直接光と同じく (1 - F) * (1 - metallic) で揃える。屈折後の光線は
    // ほぼ鉛直なので Fresnel は法線入射の F0 で近似する。
    const float3 causticsKD = (1.0f.xxx - F0) * (1.0f - metallic);
    float3 result = causticsRadiance * causticsKD * albedo / PI;
    result *= lerp(0.3f, 1.0f, mainLightShadow);
    return result;
}

/// @brief レガシー合成（スクリーンスペース版バックエンド等、置換モード外）
float3 CompositeLegacyCaustics(
    float3 causticsRadiance,
    float3 albedo,
    float ao,
    float metallic)
{
    const float3 causticsAlbedoScale = lerp(0.35f.xxx, albedo, 0.65f);
    const float causticsAOScale = lerp(0.45f, 1.0f, ao);
    const float causticsMetallicScale = lerp(1.0f, 0.25f, metallic);
    static const float kWaterCausticsCompositeScale = 4.5f;
    return causticsRadiance * causticsAlbedoScale * causticsAOScale * causticsMetallicScale
        * kWaterCausticsCompositeScale;
}

/// @brief 波打ち際アーティファクト切り分け用の可視化（デバッグモード 3/4）
/// @details 出力はトーンマッパ＋自動露出を通るため、飽和した原色だけで符号化する
///          （0-1 のグレースケールや虹色は明るいシーンで白飛びして読めない）。
/// @param handled true ならこの色をそのまま出力して return してよい
float4 BuildUnderwaterDebugColor(
    uint debugViewMode,
    UnderwaterContext ctx,
    float4 waterCausticsSample,
    float3 replacedMainLight,
    float3 compositedCaustics,
    out bool handled)
{
    handled = false;

    if (debugViewMode == 3)
    {
        // 水中判定マップ。R = underwaterFactor（メインライト直接光を消した割合）、
        // G = coverage α、B = 基準水面より 30cm 以上深い領域。
        handled = true;
        return float4(
            ctx.factor,
            saturate(waterCausticsSample.a),
            saturate(ctx.submergedDepth / 0.3f),
            1.0f);
    }

    if (debugViewMode == 4)
    {
        // 水面線での明るさ連続性チェック。
        // 「合成したコースティクス」÷「置換で消したメインライト直接光」の輝度比を EV で表示する。
        //   緑 = 比 1.0（連続） / 赤 = コースティクス過大（+2EV で真っ赤・白線の原因）
        //   青 = 過小（-2EV で真っ青・黒い縁の原因） / マゼンタ = 水中判定ゼロ
        handled = true;
        const float replacedLuma = Luminance(replacedMainLight);
        const float causticsLuma = Luminance(compositedCaustics);
        if (ctx.factor < 0.01f || replacedLuma < 1.0e-6f)
        {
            return float4(1.0f, 0.0f, 1.0f, 1.0f);
        }
        const float ev = log2(max(causticsLuma, 1.0e-6f) / replacedLuma);
        const float3 ratioColor = (ev >= 0.0f)
            ? lerp(float3(0.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f), saturate(ev * 0.5f))
            : lerp(float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f), saturate(-ev * 0.5f));
        return float4(ratioColor, 1.0f);
    }

    return 0.0f.xxxx;
}

#endif // UNDERWATER_LIGHTING_INCLUDED
