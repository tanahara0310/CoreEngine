// ============================================================
// 水面法線の解決（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// ピクセル単位の面法線（FFT 3 カスケード合成＋フットプリントフェード AA）。
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
/// @param unresolvedMeanSquareSlope フェードで法線から外した分の平均二乗傾斜。
///        外した波は 1 ピクセルの中に入る見えない凹凸なので、呼び出し側でラフネスへ足す
float3 ResolveSurfaceNormal(WaterPSInput input, out float unresolvedMeanSquareSlope)
{
    unresolvedMeanSquareSlope = 0.0f;
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

        // 傾きを fade 倍に弱めた分の分散（全体の分散 = 残した分 fade² ＋ 外した分）
        unresolvedMeanSquareSlope += (1.0f - fade * fade) * gFFTCascadeMeanSquareSlope[ci];
    }

    // 波群エンベロープ: 変位（FFTWater.VS）と同じ変調を傾きへ掛け、幾何と法線を一致させる
    // （VS は baseWorldPos.xz で評価しているので引数も揃える）
    const float waveGroupEnvelope = ComputeFFTWaveGroupEnvelope(input.baseWorldXZ);
    slope *= waveGroupEnvelope;
    unresolvedMeanSquareSlope *= waveGroupEnvelope * waveGroupEnvelope;

    float3 combinedLocal = normalize(float3(slope.x, 1.0f, slope.y));
    return normalize(combinedLocal.x * tangent + combinedLocal.y * vertexNormal + combinedLocal.z * bitangent);
}

#endif // WATER_NORMALS_INCLUDED
