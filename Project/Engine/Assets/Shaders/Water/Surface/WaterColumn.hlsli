// ============================================================
// 水柱厚さ（水中光路長）の解決（Water.PS.hlsl 専用）
// ------------------------------------------------------------
// 水柱厚さの 4 供給源（RT実測 / スクリーン空間近似 / 無限水柱 / 解析的鉛直水深）から、
// 屈折像と同じ経路の水柱厚さを求める。屈折レイがヒットしなかった画素は残りの 3 つを合成する。
//
// 【include 位置の契約】Water.PS.hlsl のリソース宣言・WaterPSInput・
// WaterFrameConstants(b5) の後で include すること。以下に暗黙依存する:
//   資源    : gSceneDepth / gLinearClamp / gRTWaterRefractionColor / gCamera(b0)
//   cbuffer : gDepthFadeEnabled / gCameraNearZ / gCameraFarZ
//   型      : WaterPSInput
//   関数    : IsRTPathValid / DecodeRTOpticalPath（Common/WaterRefractionEncoding.hlsli）
// ============================================================
#ifndef WATER_COLUMN_INCLUDED
#define WATER_COLUMN_INCLUDED

/// @brief 水の屈折率（空気 1 に対する比）
static const float kWaterRefractiveIndex = 1.333f;

/// @brief NDC 深度値をビュー空間線形深度（メートル単位）に変換する
/// @param ndcDepth  深度テクスチャから読んだ NDC 深度 [0,1]
/// @param nearZ     ニアクリップ距離
/// @param farZ      ファークリップ距離
float LinearizeDepth(float ndcDepth, float nearZ, float farZ)
{
    // D3D12 のデプスは [0, 1]（0 = near, 1 = far）
    // NDC → ビュー空間深度に変換する
    return (nearZ * farZ) / (farZ - ndcDepth * (farZ - nearZ));
}

/// @brief ビュー空間Z差をスクリーンレイ上の実際の光路長へ変換する
/// @param viewDepthDelta 背景と水面のビュー空間Z差
/// @param waterDepthView 水面のビュー空間Z深度
/// @param worldPos 水面ピクセルのワールド座標
/// @return Beer-Lambert に使う水中光路長
float ComputeWaterOpticalPathLength(float viewDepthDelta, float waterDepthView, float3 worldPos)
{
    float rayDistanceToWater = length(gCamera.worldPosition - worldPos);
    float viewToRayScale = rayDistanceToWater / max(waterDepthView, 1.0e-4f);
    return max(0.0f, viewDepthDelta * viewToRayScale);
}

/// @brief 屈折を無視した視線光路長を、実際の屈折光路長へ換算する係数を返す
/// @param viewDir       水面上の点 → カメラ方向（正規化済み）
/// @param surfaceNormal 水面法線（正規化済み）
/// @details ComputeWaterOpticalPathLength は「屈折させていない視線」が水中を進む距離を返すが、
///          waterColumn の利用側（Beer-Lambert の transmittance）は
///          「屈折後の光路長」を前提にしている。RTWaterRefraction が返す実測値も屈折後の
///          光路長なので、換算しないと両者が別物の量になり、RT の成功/失敗が切り替わる
///          境界で透過率が段差になる（波打ち際の白線が二重に見える原因）。
///
///          水柱の鉛直厚さ t は視線・屈折線のどちらで測っても同じなので、
///            t = L_view × |viewDown.y| = L_refract × |refracted.y|
///          より L_refract = L_view × |viewDown.y| / |refracted.y|。
///          かすめ角ほど屈折線は立つ（|refracted.y| が大きい）ので係数は 1 未満になる。
float ComputeRefractedPathScale(float3 viewDir, float3 surfaceNormal)
{
    const float kEtaAirToWater = 1.0f / kWaterRefractiveIndex;
    const float3 refractedView = refract(-viewDir, surfaceNormal, kEtaAirToWater);
    // 全反射・上向き屈折（荒れた波面法線で起こりうる）は換算不能なので等倍に落とす
    if (dot(refractedView, refractedView) <= 1.0e-6f || refractedView.y >= -1.0e-4f)
    {
        return 1.0f;
    }
    // viewDir は水面 → カメラ。その y 成分がカメラ → 水面の下向き成分の大きさに等しい
    const float viewDownY = saturate(viewDir.y);
    return saturate(viewDownY / max(-refractedView.y, 1.0e-4f));
}

/// @brief 水中へ屈折した視線方向（下向き・正規化済み）を返す
/// @param viewDir       水面上の点 → カメラ方向（正規化済み）
/// @param surfaceNormal 水面法線（正規化済み）
/// @details 全反射・上向き屈折（荒れた波面法線で起こりうる）のときは真下へ退避する。
///          水中では屈折角が臨界角 48.6° に制限されるため、正常時の -y は 0.66 以上になる。
float3 ComputeRefractedViewDir(float3 viewDir, float3 surfaceNormal)
{
    const float kEtaAirToWater = 1.0f / kWaterRefractiveIndex;
    const float3 refractedView = refract(-viewDir, surfaceNormal, kEtaAirToWater);
    if (dot(refractedView, refractedView) <= 1.0e-6f || refractedView.y >= -1.0e-4f)
    {
        return float3(0.0f, -1.0f, 0.0f);
    }
    return normalize(refractedView);
}

/// @brief 水面の点と、視線の延長が海底に当たる点の高さの差（鉛直水深 [m]）を返す
/// @param worldPos       水面ピクセルのワールド座標（頂点変位適用後 ＝ 水面の高さそのもの）
/// @param sceneDepthView 背景（海底）のビュー空間線形深度 [m]
/// @param waterDepthView 水面自身のビュー空間線形深度 [m]
float ComputeViewRayVerticalDepth(float3 worldPos, float sceneDepthView, float waterDepthView)
{
    const float3 cameraToWater = worldPos - gCamera.worldPosition;
    const float distanceToWater = length(cameraToWater);
    if (distanceToWater <= 1.0e-4f || waterDepthView <= 1.0e-4f)
    {
        return 0.0f;
    }

    const float3 rayDir = cameraToWater / distanceToWater;
    // ビュー空間Z（深度）からレイ長へ戻す係数。視線とカメラ前方軸のなす角の余弦。
    const float cosAxis = max(waterDepthView / distanceToWater, 1.0e-4f);
    const float3 groundPos = gCamera.worldPosition + rayDir * (sceneDepthView / cosAxis);
    return max(worldPos.y - groundPos.y, 0.0f);
}

// 屈折レイが当たらなかった画素で、解析水柱厚さとスクリーン空間近似をブレンドする範囲 [m]。
// 浅い側（〜1m）は解析値 100%、深い側（4m〜）はスクリーン空間近似 100%。
static const float kAnalyticColumnFullMeters = 1.0f;
static const float kAnalyticColumnBlendEndMeters = 4.0f;

// 背景深度が取得できない（水面の背後が far plane = 外洋の水平線など）場合に
// 使う光路長。σt が最小クラス（青 ≈ 0.02/m）でも exp(-σt·d) ≈ 0 になる十分な深さ。
static const float kInfiniteWaterColumnMeters = 1.0e4f;

float4 SampleRTWaterRefraction(uint2 pixelCoord)
{
    return gRTWaterRefractionColor.Load(int3(pixelCoord, 0));
}

/// @brief 水柱厚さの解決結果（デバッグ表示が参照する中間量も含む）
struct WaterColumnResult
{
    float column;         ///< Beer-Lambert に渡す水中光路長 [m]
    float sceneDepthNDC;  ///< 背景の NDC 深度
    float sceneDepthView; ///< 背景のビュー空間線形深度 [m]
    float waterDepthView; ///< 水面自身のビュー空間線形深度 [m]
    /// 視線の延長で測った鉛直水深 [m]。海底の高さが無い所の岸の泡に使う。
    /// 背景ジオメトリが無い / Depth Fade 無効時は kInfiniteWaterColumnMeters
    float viewRayVerticalDepth;
    bool hasValidDepth;   ///< 水柱厚さが有効に求まったか
};

/// @brief 水中光路長（水柱厚さ）を解決する
/// @param surfaceNormal main() で 1 度だけ解決した水面法線
/// @details 水柱厚さの供給源は 4 つあり、この順で上書き・合成される。
///
///   (A) スクリーン空間近似  … 背景と水面のビュー空間Z差 × 屈折換算。
///   (B) 無限水柱            … 背景が far plane（外洋・水平線）。透過ゼロへ収束させる。
///   (C) RT 実測光路長       … 屈折レイがヒットしていれば水深によらずこれを使う。
///                             表示している屈折像と同じ経路の厚みになる。
///   (D) 解析的な鉛直水深    … 屈折レイがヒットしなかった画素で、浅瀬（〜1m）では 100% これを使い、
///                             4m へ向けて (A) へ滑らかに移行する。
WaterColumnResult ResolveWaterColumn(
    WaterPSInput input, float3 surfaceNormal, float2 screenUV, uint2 pixelCoord)
{
    WaterColumnResult result;
    result.column = 0.0f;
    result.sceneDepthNDC = 1.0f;
    result.sceneDepthView = 0.0f;
    result.waterDepthView = 0.0f;
    result.viewRayVerticalDepth = kInfiniteWaterColumnMeters;
    result.hasValidDepth = false;

    if (!gDepthFadeEnabled)
    {
        return result;
    }

    // 実際に描画しているカメラのクリップ距離を使う。
    // 0 が来た場合（未設定フレーム）だけ既定値へ退避する。
    const float kNear = max(gCameraNearZ, 1.0e-4f);
    const float kFar = max(gCameraFarZ, kNear + 1.0e-3f);

    const float waterDepthNDC = saturate(input.position.z);
    const float3 toEyeDir = normalize(gCamera.worldPosition - input.worldPosition);
    const float3 refractedView = ComputeRefractedViewDir(toEyeDir, surfaceNormal);

    // スクリーン空間近似を RT 実測値と同じ「屈折後の光路長」へ揃えるための係数
    const float refractedPathScale = ComputeRefractedPathScale(toEyeDir, surfaceNormal);

    // シーン深度を NDC → ビュー空間線形深度（m）に変換する
    result.sceneDepthNDC = gSceneDepth.Sample(gLinearClamp, screenUV).r;
    result.waterDepthView = LinearizeDepth(waterDepthNDC, kNear, kFar);

    float analyticColumn = 0.0f;
    const bool hasBackgroundGeometry = (result.sceneDepthNDC < 0.99999f);

    if (hasBackgroundGeometry)
    {
        result.sceneDepthView = LinearizeDepth(result.sceneDepthNDC, kNear, kFar);

        // (D) 解析的な鉛直水深（分岐なしの連続場）
        result.viewRayVerticalDepth = ComputeViewRayVerticalDepth(
            input.worldPosition, result.sceneDepthView, result.waterDepthView);
        // 鉛直水深 → 屈折後の光路長。臨界角があるので -y は 0.66 以上のはずだが安全側に切る
        analyticColumn = result.viewRayVerticalDepth / max(-refractedView.y, 0.2f);

        if (result.sceneDepthView > result.waterDepthView + 1.0e-4f)
        {
            // (A) スクリーン空間近似
            result.hasValidDepth = true;
            result.column = ComputeWaterOpticalPathLength(
                result.sceneDepthView - result.waterDepthView,
                result.waterDepthView,
                input.worldPosition) * refractedPathScale;
        }
    }
    else
    {
        // (B) 背景が far plane（＝水面の先に何もない外洋・水平線）。
        // 水柱が実質無限に続くとみなし、透過ゼロ＝インスキャッタのみの
        // 「水固有の色」へ収束させる。
        result.hasValidDepth = true;
        result.column = kInfiniteWaterColumnMeters;
    }

    // (C) RT 実測光路長。ヒットしていれば (A)/(B)/(D) より優先する。
    // 色が取れなかった（画面外・深度の不一致）画素でも光路長は有効なのでそのまま使う。
    const float rtAlpha = SampleRTWaterRefraction(pixelCoord).a;
    const bool hasRTPath = IsRTPathValid(rtAlpha) > 0.5f;
    if (hasRTPath)
    {
        result.column = DecodeRTOpticalPath(rtAlpha);
        result.hasValidDepth = true;
    }

    // 屈折レイがヒットしなかった画素は、浅瀬を (D) へ寄せる。背景が far plane のときは
    // 海底が無く解析値が定義できないため除外する（そこは (B) の無限水柱が効く）。
    if (hasBackgroundGeometry && !hasRTPath)
    {
        const float deepWeight = smoothstep(
            kAnalyticColumnFullMeters, kAnalyticColumnBlendEndMeters, analyticColumn);
        result.column = lerp(analyticColumn, result.column, deepWeight);
        result.hasValidDepth = true;
    }

    return result;
}

#endif // WATER_COLUMN_INCLUDED
