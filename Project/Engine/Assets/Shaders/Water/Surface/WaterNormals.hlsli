// ============================================================
// 水面法線の解決（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// ピクセル単位の面法線（FFT 3 カスケード合成＋距離フェード AA）と、
// フレネル評価用の低周波法線（うねりまだら対策の鉛直ブレンド付き）。
//
// 【include 位置の契約】Water.PS.hlsl のリソース宣言・WaterPSInput・
// WaterFrameConstants(b5) の後で include すること。以下に暗黙依存する:
//   資源    : gFFTOceanNormal / gSampler / gCamera（Object3dForward.hlsli）
//   cbuffer : gUseFFTOceanNormalMap
//   型      : WaterPSInput
//   関数    : ComputeFFTCascadeUV / RotateFromFFTCascadeGrid /
//             ComputeFFTWaveGroupEnvelope（Common/FFTOceanCascade.hlsli）
// ============================================================
#ifndef WATER_NORMALS_INCLUDED
#define WATER_NORMALS_INCLUDED

/// @brief FFT 法線マップのエンコード値をワールド空間法線へ展開する
float3 BuildWorldNormalFromFFTSample(float3 encodedNormal, WaterPSInput input)
{
    float3 localNormal = normalize(encodedNormal * 2.0f - 1.0f);
    float3 vertexNormal = normalize(input.normal);
    float3 tangent = normalize(input.tangent);
    float3 bitangent = normalize(input.bitangent);
    return normalize(localNormal.x * tangent + localNormal.y * vertexNormal + localNormal.z * bitangent);
}

/// @brief ピクセル単位の法線を解決する
/// @details Gerstner Wave は頂点シェーダーで解析的に計算した法線をそのまま補間して使えるが、
///          FFT Ocean はテクスチャベースの法線マップであるため、頂点解像度で補間すると
///          メッシュの三角形境界に沿った法線のファセット化（IBL 反射のギザギザ）が発生する。
///          FFT Ocean 使用時は texcoord から法線マップを直接再サンプリングし、
///          頂点密度に依存しない滑らかな法線を得る。
///          サンプリングは WRAP アドレスの gSampler で行う（frac 不要でタイル境界の
///          微分不連続が出ず、ミップチェーン＋異方性フィルタが自動LODで効くため、
///          遠方・かすめ角で法線がピクセル毎に暴れるエイリアシングを抑制できる）。
/// @brief カスケードごとの法線スライスを合算し、距離フェードでAAした面法線を作る
/// @details 各カスケードの傾き（勾配 = nLocal.xz / nLocal.y）を加算してから鉛直へ再構成する。
///          小さいパッチ（高周波）は遠方でフェードアウトさせ、法線ミップ連鎖の代わりに
///          遠距離・かすめ角のスペックル（フレネルの高周波ノイズ）を抑える。
float3 ResolveSurfaceNormal(WaterPSInput input)
{
    float3 vertexNormal = normalize(input.normal);
    if (gUseFFTOceanNormalMap == 0)
    {
        return vertexNormal;
    }

    float3 tangent = normalize(input.tangent);
    float3 bitangent = normalize(input.bitangent);

    // ★フェード判定は「距離」ではなく「1 ピクセルが覆うテクセル数」で行う★
    // FFT の法線テクスチャは MipLevels=1 で生成されており（FFTOceanManager.cpp）、
    // Sample() は異方性サンプラでもミップを選べない＝縮小フィルタが一切効かない。
    // そのため 1 ピクセルが多数テクセルを跨ぐ状況では法線がピクセル毎に暴れる。
    // 旧実装はこれをカメラ距離でフェードして誤魔化していたが、**かすめ角では
    // 距離が近くてもフットプリントが巨大になる**ため全く効かず、水面すれすれの
    // 視点で明暗がピクセル単位に切り替わる黒いギザギザとして現れていた
    // （2026-08-09 修正）。UV の画面微分から実測フットプリントを求めれば、
    // 距離と角度の両方を正しく織り込める。
    uint normalTexWidth = 1;
    uint normalTexHeight = 1;
    uint normalTexSlices = 1;
    gFFTOceanNormal.GetDimensions(normalTexWidth, normalTexHeight, normalTexSlices);
    const float normalTexelCount = (float)max(normalTexWidth, 1u);

    float2 slope = float2(0.0f, 0.0f);
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        // 参照格子座標を回転格子系へ（FFTWater.VS の変位サンプリングと同一の引数）。
        // ここを input.worldPosition.xz にすると、変位後の点で法線を引くことになり
        // 水平変位ぶん法線が幾何からズレる（2026-08-08 修正）。
        float2 cuv = ComputeFFTCascadeUV(input.baseWorldXZ, ci);
        float3 enc = gFFTOceanNormal.Sample(gSampler, float3(cuv, (float)ci)).xyz;
        float3 nLocal = normalize(enc * 2.0f - 1.0f); // (x=+texU, y=up, z=+texV)

        // このカスケードのテクセルを 1 ピクセルが何個跨ぐか（= 縮小率）。
        // 2 テクセル/ピクセルでナイキストを割るので、そこから落として 4 で消す。
        const float2 duvdx = ddx(cuv);
        const float2 duvdy = ddy(cuv);
        const float texelsPerPixel =
            max(length(duvdx), length(duvdy)) * normalTexelCount;
        const float fade = 1.0f - smoothstep(1.0f, 4.0f, texelsPerPixel);

        // テクスチャ格子系の傾きをワールドへ逆回転してから合算する
        float2 slopeTex = nLocal.xz / max(nLocal.y, 1.0e-3f);
        slope += RotateFromFFTCascadeGrid(slopeTex, ci) * fade;
    }

    // 波群エンベロープ: 変位（FFTWater.VS）と同じ変調を傾きへ掛け、幾何と法線を一致させる
    // （VS は baseWorldPos.xz で評価しているので引数も揃える）
    slope *= ComputeFFTWaveGroupEnvelope(input.baseWorldXZ);

    float3 combinedLocal = normalize(float3(slope.x, 1.0f, slope.y));
    return normalize(combinedLocal.x * tangent + combinedLocal.y * vertexNormal + combinedLocal.z * bitangent);
}

// （旧 kFresnelNormalMipBias は撤去。カスケード化により「最大パッチのスライスを
//   単独サンプルする」方式へ移行し、ミップバイアスによる高周波ぼかしは使わなくなった。
//   定数だけが残って ResolveFresnelNormal のコメントと食い違っていた）

// フレネル評価用法線を鉛直へブレンドする強さ（0=波法線そのまま, 1=完全に平坦）。
// ★まだらの根本対策★
// フレネル混合比を「うねり（低周波の大波）の傾き」で評価すると、うねりがカメラを
// 向く面＝低フレネル＝暗い透過、うねりが寝る面＝高フレネル＝明るい空反射、となり
// うねりスケールの大きな明暗の塊（青/黒のまだら）が出る。これは細かいさざ波ではなく
// うねり＝低周波成分が原因なので、ミップバイアス（高周波ぼかし）では消せない
// （うねりは全ミップに存在するため。過去に平坦化を試して効果が無かった真因）。
// フレネル法線を鉛直へ強くブレンドし、うねりの傾き自体を減衰させることで、
// 反射/透過の混合比が「視線角度に応じた滑らかなグラデーション」になり塊が消える。
// 反射像の歪み（geomNormal 使用）や鏡面ハイライトはフル法線のままなので、
// 波のディテールは失わない。
// 0.75 → 0.35: 平坦化が強すぎると波ごとの反射/透過の切り替わり（水面らしい
// きらめきのコントラスト）まで消えて一様な膜に見えるため緩和。
// まだらの真因（反射ビューへの水面自己描画）は修正済みなので、うねり由来の
// フレネル変化はある程度残してよい。夜間にまだらが再発しないか要確認。
static const float kFresnelNormalFlatten = 0.35f;

/// @brief フレネル（反射/透過の混合比）評価に使う低周波の面法線を解決する
float3 ResolveFresnelNormal(WaterPSInput input)
{
    float3 waveNormal;
    if (gUseFFTOceanNormalMap != 0)
    {
        // フレネルは「うねりスケールの低周波法線」で評価する。カスケード化により、
        // 最大パッチ（低周波の大波）のスライスを単独でサンプルするだけで、旧来の
        // ミップバイアスぼかしと同じ「うねりスケールの滑らかな法線」が得られる
        // （小さいパッチ＝さざ波は混ぜない）。カスケード0は回転恒等なので uv 回転は不要。
        float2 cuv = input.baseWorldXZ / kFFTCascadePatch[0];
        float3 encodedNormal = gFFTOceanNormal.Sample(gSampler, float3(cuv, 0.0f)).xyz;
        // 波群エンベロープを傾きへ掛け、実ジオメトリ（変位×エンベロープ）と整合させる
        float3 nLocal = normalize(encodedNormal * 2.0f - 1.0f);
        float2 slopeTex = (nLocal.xz / max(nLocal.y, 1.0e-3f))
            * ComputeFFTWaveGroupEnvelope(input.baseWorldXZ);
        float3 envLocal = normalize(float3(slopeTex.x, 1.0f, slopeTex.y));
        waveNormal = BuildWorldNormalFromFFTSample(envLocal * 0.5f + 0.5f, input);
    }
    else
    {
        // Gerstner Wave など法線マップが無い経路では、変位適用後のワールド座標の
        // 画面微分から面法線を再構成する（頂点法線は常に真上でうねりを含まないため）。
        float3 faceNormal = normalize(cross(ddy(input.worldPosition), ddx(input.worldPosition)));
        waveNormal = (faceNormal.y < 0.0f) ? -faceNormal : faceNormal;
    }

    // うねりの傾きを鉛直へブレンドして減衰させる（まだらの根本対策・上記コメント参照）。
    float3 flattened = normalize(lerp(waveNormal, float3(0.0f, 1.0f, 0.0f), kFresnelNormalFlatten));
    return flattened;
}

#endif // WATER_NORMALS_INCLUDED
