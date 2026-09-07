// 無限グリッド（床）のピクセルシェーダー。
//
// 商用エンジン / Blender のグリッドと同じ作りで、線をラインプリミティブでは引かない。
//   1. カメラレイと y = planeY の交点をピクセルごとに求める（カメラ相対座標で解く）
//   2. 「線までの距離 ÷ 1 ピクセルの足跡」でカバレッジを出す。線幅が常に約 1px へ
//      正規化されるので、勝手にアンチエイリアスが掛かる（MSDF テキストと同じ考え方）
//   3. 段（10 倍ごとの粗さ）はカメラからの距離ではなく足跡の大きさで選ぶ。ミップマップと
//      同じ理屈で、画面上で格子が詰まりすぎることが起きないためモアレ（じりじり）が出ない
#include "GridCommon.hlsli"

struct PSOutput
{
    float4 color : SV_TARGET;
    float depth : SV_DEPTH;   // 床平面上の点の深度。これでシーンの物体に正しく隠れる
};

/// @brief 格子の評価に使う UV（1.0 で 1 マス）を求める
/// @param hitXZ     交点のカメラ相対 XZ [m]
/// @param cameraXZ  カメラのワールド XZ [m]
/// @param spacing   格子間隔 [m]
/// @details ワールド座標を組み立ててから frac を取ると、カメラが原点から遠いときに
///          大きい値の丸めで格子が壊れる。frac(a + b) == frac(frac(a) + frac(b)) を
///          使い、カメラのマス内オフセットだけを足すことで小さい値のまま扱う。
float2 GridCellUV(float2 hitXZ, float2 cameraXZ, float spacing)
{
    return hitXZ / spacing + frac(cameraXZ / spacing);
}

/// @brief 1 段ぶんの格子のカバレッジ（0..1）
/// @param uv      GridCellUV の値（1.0 で 1 マス）
/// @param uvDeriv uv の画面スペース微分。1 ピクセルが何マスぶんかを表す
/// @param widthPixels 線の太さ [px]
float GridCoverage(float2 uv, float2 uvDeriv, float widthPixels)
{
    // 線の中心からの距離をピクセル単位で測る。足跡で割ることで線幅が常に
    // 約 widthPixels px へ正規化され、そのままアンチエイリアス済みの濃さになる
    const float2 distanceCells = (1.0f - abs(frac(uv) * 2.0f - 1.0f)) * 0.5f;
    const float2 distancePixels = distanceCells / max(uvDeriv, 1e-8f);
    float2 coverage = saturate(widthPixels * 0.5f + 0.5f - distancePixels);

    // 1 マスが数ピクセルを割った段は、もう格子ではなく縞（モアレ）にしかならないので消す。
    // 段の選択がこの状態を作らないようにしてあるが、最後の砦として必ず掛けておく
    const float2 cellPixels = 1.0f / max(uvDeriv, 1e-8f);
    coverage *= saturate((cellPixels - 1.5f) / 1.5f);

    // X 方向の線と Z 方向の線を重ねる
    return saturate(max(coverage.x, coverage.y));
}

/// @brief 軸ライン 1 本ぶんのカバレッジ（0..1）
/// @param distanceWorld 線からの距離 [m]
/// @param deriv         その方向の 1 ピクセルぶんのワールド距離 [m]
/// @param widthPixels   線の太さ [px]
float AxisCoverage(float distanceWorld, float deriv, float widthPixels)
{
    const float distancePixels = abs(distanceWorld) / max(deriv, 1e-8f);
    return saturate(widthPixels * 0.5f + 0.5f - distancePixels);
}

PSOutput main(GridVSOutput input)
{
    // ===== カメラレイと床平面の交点（すべてカメラ相対で解く）=====
    const float3 rayStart = input.nearPoint;
    const float3 rayEnd = input.farPoint;
    const float planeYRelative = planeY - cameraPosition.y;
    const float slope = rayEnd.y - rayStart.y;
    const float t = (planeYRelative - rayStart.y) / (abs(slope) < 1e-12f ? 1e-12f : slope);
    const float3 hit = lerp(rayStart, rayEnd, t);

    // 微分は discard より先に取る。同じ 2x2 クアッド内で値が揃っていないと
    // ddx/ddy が壊れるため、捨てる判定はこの後に置く
    const float2 hitDDX = ddx(hit.xz);
    const float2 hitDDY = ddy(hit.xz);
    // 軸ごとの「1 ピクセルが覆うワールド距離」
    const float2 deriv = float2(length(float2(hitDDX.x, hitDDY.x)),
                                length(float2(hitDDX.y, hitDDY.y)));
    const float footprint = max(deriv.x, deriv.y);

    // レイが床平面と交わらない（空を見ている / ファークリップより遠い）ピクセルは捨てる
    if (t < 0.0f || t > 1.0f) {
        discard;
    }

    // ===== 深度 =====
    const float4 clipPosition = mul(float4(hit, 1.0f), viewProjection);
    if (clipPosition.w <= 0.0f) {
        discard;
    }
    const float rawDepth = clipPosition.z / clipPosition.w;
    if (rawDepth < 0.0f || rawDepth > 1.0f) {
        discard;
    }
    // 床メッシュと同一平面に近いので、わずかに手前へ寄せて Z ファイティングを避ける。
    // 遠くでは深度バッファの量子化幅が平面同士の隙間より大きくなり、バイアス無しだと
    // ピクセルごとに前後が入れ替わってちらつく
    const float depth = saturate(rawDepth - depthBias);

    // ===== 段（LOD）の選択 =====
    // 1 マスが minPixelsPerCell px 以上になる最小の段。カメラ距離ではなく足跡で
    // 決めるのがポイントで、これがテクスチャのミップレベル選択に相当する
    float level = log10(max(footprint * minPixelsPerCell / max(baseSpacing, 1e-6f), 1e-8f));
    level = max(level, 0.0f);
    // 一番粗い段でも解像しきれない領域（ほぼ地平線）は素直に消す
    const float horizonFade = saturate(1.0f + maxLevel - level);
    level = min(level, maxLevel);

    const float levelFloor = floor(level);
    const float levelFrac = level - levelFloor;

    const float spacingFine = baseSpacing * pow(10.0f, levelFloor);
    const float2 derivFine = deriv / spacingFine;

    const float coverageFine = GridCoverage(
        GridCellUV(hit.xz, cameraPosition.xz, spacingFine), derivFine, lineWidthPixels);
    const float coverageCoarse = GridCoverage(
        GridCellUV(hit.xz, cameraPosition.xz, spacingFine * 10.0f), derivFine * 0.1f, lineWidthPixels);

    // 細かい段は次の段へ繰り上がるにつれて消し、粗い段は出したままにする。
    // 粗い段の線は細かい段の 10 本ごとに重なるが、max なので二重に濃くはならない
    const float gridCoverage = max(coverageCoarse, coverageFine * (1.0f - levelFrac));

    // ===== 合成（格子 → X 軸 → Z 軸の順で上へ重ねる）=====
    float3 color = gridColor;
    float alpha = gridCoverage;

    // 軸はワールド原点を通るので、カメラ相対の交点にカメラ位置を戻して距離を測る
    const float2 axisDistance = abs(hit.xz + cameraPosition.xz);

    const float xAxisCoverage = AxisCoverage(axisDistance.y, deriv.y, lineWidthPixels); // z = 0 を走る線
    color = lerp(color, xAxisColor, xAxisCoverage);
    alpha = lerp(alpha, axisAlpha, xAxisCoverage);

    const float zAxisCoverage = AxisCoverage(axisDistance.x, deriv.x, lineWidthPixels); // x = 0 を走る線
    color = lerp(color, zAxisColor, zAxisCoverage);
    alpha = lerp(alpha, axisAlpha, zAxisCoverage);

    alpha = saturate(alpha * brightness * horizonFade);
    if (alpha <= 0.001f) {
        discard;
    }

    PSOutput output;
    output.color = float4(color, alpha);
    output.depth = depth;
    return output;
}
