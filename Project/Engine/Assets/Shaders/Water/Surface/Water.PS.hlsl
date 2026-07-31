#include "Object3dForward.hlsli"
// 大気散乱の空気遠近感を水面（フォワード半透明）にも適用する（b6 / t22 / t23 を使用）
#include "AtmosphereApply.hlsli"
// カスケード定数・回転写像・波群エンベロープの唯一の情報源
#include "../Common/FFTOceanCascade.hlsli"
// RT 屈折アルファのエンコード規約（RTWaterRefraction.hlsl と共有）
#include "../Common/WaterRefractionEncoding.hlsli"
#include "ColorSpace.hlsli" // Luminance

// ===== 反射テクスチャ（RTWaterReflectionPass の DXR 出力）=====
// 鏡像カメラによる Planar Reflection は廃止済み。スクリーン空間・水面ピクセル単位の
// 反射シーン色が入っており、alpha >= 0.5 が成功・< 0.5 がミス（空環境マップへ落とす）。
Texture2D<float4> gReflectionTexture : register(t14);
SamplerState gLinearClamp : register(s2);

// ===== シーン深度テクスチャ（GBuffer 深度 / Depth Stencil SRV）=====
// WaterPlaneObject::BindCustomResources() が t15 にバインドする
// Depth Fade（Beer-Lambert）で水柱の厚さを計算するために使用する
Texture2D<float> gSceneDepth : register(t15);

// ===== シーンカラーテクスチャ（OffScreen Color SRV）=====
// WaterPlaneObject::BindCustomResources() が t16 にバインドする
// 水面越しに見える背景色の取得に使用する
Texture2D<float4> gSceneColor : register(t16);

// ===== DXR 水面屈折カラー（RTWaterRefractionPass 出力）=====
// WaterPlaneObject::BindCustomResources() が t17 にバインドする
Texture2D<float4> gRTWaterRefractionColor : register(t17);

// ===== FFT Ocean 法線マップ（FFTOceanFinalize.CS.hlsl 出力）=====
// 頂点解像度に依存しない法線を得るため、ピクセルシェーダーで直接再サンプリングする。
// カスケード（マルチスケールFFT）は Texture2DArray のスライスに格納される。
Texture2DArray<float4> gFFTOceanNormal : register(t19);

// ===== 空の放射照度 SH9 係数（SkyIrradianceSH.CS.hlsl 出力）=====
// WaterPlaneObject::BindCustomResources() が t24 にバインドする。
// 係数には太陽色・強度が焼き込み済みで、Σ c_i・Y_i(N) がそのまま
// 「法線 N に対する空からの放射照度 / π」になる（DeferredLighting と同じ規約）。
StructuredBuffer<float4> gWaterSkyIrradianceSH : register(t24);

// ===== プリフィルタ済み空スペキュラキューブマップ（空＋雲。Phase 3b）=====
// AtmosphereManager が Sky-View LUT＋雲レイマーチから毎フレーム生成する。
// rgb は空（SkyAtmosphere.PS）と同一の輝度ドメイン（平面反射像と直接ブレンド可能）。
// α には雲の透過率が入っており、平面反射（雲を含まない）へ雲を被せる不透明度に使う。
TextureCube<float4> gSkyEnvironmentMap : register(t25);

struct WaterPSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float4 jacobianData : TEXCOORD1;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 lightSpacePos : POSITION1;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
};

// ===== フレーム定数バッファ（VS と共有）=====
cbuffer WaterFrameConstants : register(b5)
{
    int gReflectionEnabled; // 1 = 反射テクスチャ有効，0 = IBL フォールバック
    float gFresnelReflectanceScale; // Fresnel 反射率スケール
    float gFresnelBaseReflectance; // 正面入射時の反射率（F0）

    // ---- Depth Fade ----
    int gDepthFadeEnabled; // 1 = Depth Fade 有効
    int gDepthFadeDebugEnabled; // 1 = 水深デバッグ表示
    float gDepthFadeDebugScale; // 水深デバッグ表示倍率
    // 空アンビエントの輝度単位 → サーフェス光単位の変換係数（AtmosphereManager::GetSkyAmbientScale と同値）
    float gSkyAmbientScale;
    // 1 = 大気散乱の Sky Irradiance SH を天空光として使う（大気アクティブ＋SH生成済みのシーンのみ）
    int gSkyAmbientEnabled;

    // ---- 水の光学特性（波長依存 Beer-Lambert）----
    // 水の色は shallow/deep の色指定ではなく、吸収・散乱係数と光源から導出する。
    // 赤 > 緑 > 青 の順に吸収が強いことが「水が青い」物理の本体。
    float3 gAbsorptionCoeff; // 吸収係数 σa [1/m]（RGB 波長別）
    float gAbsorptionPad;
    float3 gScatteringCoeff; // 散乱係数 σs [1/m]（RGB 波長別）
    float gScatteringPad;

    // ---- デバッグ表示 ----
    uint gDepthDebugViewMode;
    // FFT Ocean 使用時、頂点解像度に依存しないピクセル単位の法線マップ再サンプリングを行うか
    int gUseFFTOceanNormalMap;
    // 大気散乱の空気遠近感を水面へ適用するか（大気アクティブなシーンでのみ 1）
    int gAerialPerspectiveEnabled;
    // 空スペキュラキューブマップで平面反射へ雲を合成するか（大気アクティブ＋生成済みのみ 1）
    int gSkyEnvReflectionEnabled;

    // ---- 描画カメラのクリップ距離（LinearizeDepth 用）----
    // C++ 側 WaterFrameConstants::cameraNearZ / cameraFarZ と一致させること。
    // ここをハードコードしてはいけない（エディタ保存カメラは far=100000 で、
    // 既定 1000 と食い違うと水柱厚さが数十%狂う）。
    float gCameraNearZ;
    float gCameraFarZ;
    float2 gCameraClipPadding;
};

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
    const float kEtaAirToWater = 1.0f / 1.333f;
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
    const float kEtaAirToWater = 1.0f / 1.333f;
    const float3 refractedView = refract(-viewDir, surfaceNormal, kEtaAirToWater);
    if (dot(refractedView, refractedView) <= 1.0e-6f || refractedView.y >= -1.0e-4f)
    {
        return float3(0.0f, -1.0f, 0.0f);
    }
    return normalize(refractedView);
}

/// @brief 水面と海底の「高さの差」から水中光路長を解析的に求める
/// @param worldPos       水面ピクセルのワールド座標（頂点変位適用後 ＝ 水面の高さそのもの）
/// @param sceneDepthView 背景（海底）のビュー空間線形深度 [m]
/// @param waterDepthView 水面自身のビュー空間線形深度 [m]
/// @param refractedView  水中へ屈折した視線（ComputeRefractedViewDir）
/// @details ★波打ち際に線が出続けた問題の恒久対策★（2026-07-27）
///          浅瀬は吸収がゼロ（exp(-σt·d), d≈0 → 1）なので、水柱厚さの推定誤差が
///          そのまま素通しで見える。そこで水柱厚さが「RT実測光路長 / スクリーン空間近似 /
///          無限水柱 / ゼロ」に分岐すると、分岐の境界が必ず 1 ピクセルの等高線＝線になる。
///          過去の白線・二重線・紺色のヘアラインは全てこの構図だった。
///
///          この関数は分岐を一切持たない連続場として水柱厚さを与える:
///            鉛直水深 = 水面の高さ − 海底の高さ
///          水面の高さは変位適用後の worldPos.y がそのまま使える。海底の高さは
///          シーン深度から視線に沿って復元する（レイ距離 = ビュー空間Z / cos(視線軸角)）。
///          汀線は「この場のゼロ等高線」になるため、段差が原理的に発生しない。
float ComputeAnalyticWaterColumn(
    float3 worldPos, float sceneDepthView, float waterDepthView, float3 refractedView)
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

    const float verticalDepth = max(worldPos.y - groundPos.y, 0.0f);
    // 鉛直水深 → 屈折後の光路長。臨界角があるので -y は 0.66 以上のはずだが安全側に切る
    return verticalDepth / max(-refractedView.y, 0.2f);
}

// 解析水柱厚さと RT 実測光路長のブレンド範囲 [m]。
// 浅い側（〜1m）は解析値 100%: 分岐が無いので線が出ない。ここは吸収がほぼ効かず
// 誤差が丸見えになる領域なので、連続性を最優先する。
// 深い側（4m〜）は RT 実測 100%: 屈折で曲がった先の距離が効くので水中オブジェクトの
// 見え方が正しくなる。
// 0.3〜1.5m から 1.0〜4.0m へ拡大（2026-07-27）: 遷移域では RT 実測値の不連続が
// 重み分だけ漏れて薄い線として残るため、その重みが立ち上がる深さを、吸収が十分効いて
// 差が視覚的に潰れる所まで押し出す（赤の σa≈0.45/m なら 4m で透過率 0.16）。
static const float kAnalyticColumnFullMeters = 1.0f;
static const float kAnalyticColumnBlendEndMeters = 4.0f;

/// @brief Schlick 近似による Fresnel 係数を計算する
/// @param cosTheta  視線と法線のなす角の余弦（saturate 済み推奨）
/// @param f0        法線入射時の反射率（水面 ≈ 0.02）
float FresnelSchlick(float cosTheta, float f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 VisualizeDepthValue(float value)
{
    return float3(value, value, value);
}

float3 VisualizeJacobian(float4 jacobianData)
{
    const float detJ = jacobianData.x;
    const float breakingCandidate = saturate(jacobianData.y);
    const float compression = saturate(jacobianData.z);
    const float foldover = saturate(jacobianData.w);

    const float detVisualization = saturate((1.0f - detJ) * 0.5f + 0.5f);
    return saturate(float3(detVisualization, breakingCandidate, max(compression, foldover)));
}

// 背景深度が取得できない（水面の背後が far plane = 外洋の水平線など）場合に
// 使う光路長。σt が最小クラス（青 ≈ 0.02/m）でも exp(-σt·d) ≈ 0 になる十分な深さ。
static const float kInfiniteWaterColumnMeters = 1.0e4f;

/// @brief SH9 係数から法線方向の空の放射照度/π を評価する
/// @details DeferredLighting.PS.hlsl の EvaluateSkyIrradiance と同じ評価式。
///          係数（gWaterSkyIrradianceSH）には放射照度畳み込みと太陽色・強度が焼き込み済み。
float3 EvaluateWaterSkyIrradiance(float3 n)
{
    float basis[9];
    basis[0] = 0.282095f;
    basis[1] = 0.488603f * n.y;
    basis[2] = 0.488603f * n.z;
    basis[3] = 0.488603f * n.x;
    basis[4] = 1.092548f * n.x * n.y;
    basis[5] = 1.092548f * n.y * n.z;
    basis[6] = 0.315392f * (3.0f * n.z * n.z - 1.0f);
    basis[7] = 1.092548f * n.x * n.z;
    basis[8] = 0.546274f * (n.x * n.x - n.y * n.y);

    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        irradiance += gWaterSkyIrradianceSH[i].rgb * basis[i];
    }
    // SH の帯域打ち切りによる負のリンギングを防ぐ
    return max(irradiance, 0.0f);
}

/// @brief 水中インスキャッタリングの環境光（水面に入射する光の放射輝度近似）を求める
/// @details 平行光源（太陽）の下向き放射照度と天空拡散光を合算する。
///          太陽ライトの GPU 転送色には LightManager が大気の Transmittance LUT による
///          減衰（日没の赤方偏移・減光）を乗算済みのため、ここで読むだけで大気に追従する。
///          天空光は大気アクティブ時は Sky Irradiance SH（時刻・太陽高度に連動）、
///          非アクティブ時は静的な拡散 IBL キューブマップへフォールバックする。
float3 ComputeUnderwaterAmbientLight()
{

    // 太陽など平行光源: 水平な水面へ入る下向き放射照度を Lambert 面の放射輝度へ換算（/π）
    // downwelling は太陽高度に対してほぼ線形に 0 へ落ちる（sin(elevation) 相当）ため、
    // 太陽を下げていくと直射成分だけが急激に暗転する。
    float3 sunAmbient = float3(0.0f, 0.0f, 0.0f);
    float maxDownwelling = 0.0f;
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float downwelling = saturate(-normalize(gDirectionalLights[i].direction).y);
        maxDownwelling = max(maxDownwelling, downwelling);
        sunAmbient += gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * downwelling / PI;
    }

    // 天空拡散光
    float3 skyAmbient = float3(0.0f, 0.0f, 0.0f);
    if (gSkyAmbientEnabled != 0)
    {
        // 大気散乱の空を SH9 で評価（上向き法線＝水面へ降り注ぐ天空放射照度/π）。
        // DeferredLighting と同じスケールでサーフェス光単位へ変換する
        skyAmbient = EvaluateWaterSkyIrradiance(float3(0.0f, 1.0f, 0.0f)) * gSkyAmbientScale;
    }
    else if (gIBLParams.sceneIBLEnabled != 0)
    {
        // Irradiance マップは既に「albedo に掛けるだけ」の放射輝度相当なのでそのまま加算
        skyAmbient = gIrradianceMap.SampleLevel(gSampler, float3(0.0f, 1.0f, 0.0f), 0.0f).rgb
                 * gIBLParams.environmentIntensity;
    }

    // 天空光はブーストせずそのまま使う。
    // 以前ここに「太陽が低いほど天空光を最大2.5倍へ底上げする」ハックがあったが、
    // 夜間に水中インスキャッタ（透過側）だけが空の反射（ほぼ黒）より明るくなり、
    // フレネルの振れに応じて青/黒のうねりスケールのまだらを生む原因になったため撤去。
    // 水中に入る光と水面に映る空は同じ空なので、両者の明るさは常に整合させる。
    return sunAmbient + skyAmbient;
}

// ★太陽の下り光路（太陽→海底）の吸収はこのシェーダーの責務ではない★（2026-07-27 撤去）
// DeferredLighting の水中ライティング置換（RT コースティクス）が、海底ピクセルの
// 直接光として exp(-σa·実光路長) を含む透過直接光を既に合成している。
// ここで再度掛けると下り光路が二重計上になるため、以前あった
// ComputeSunDownwellingTransmittance() は呼び出しごと削除した。
// 本シェーダーが担当するのは「視線の上り光路」（transmittance）のみ。

/// @brief 水柱を通過した背景色に波長依存の吸収・散乱を適用する
/// @param refractionColor 水面越しに見える背景（海底・水中物体）の色
/// @param transmittance   exp(-σt·d) 視線（上り）光路の波長別透過率
/// @param sigmaS          散乱係数 σs [1/m]
/// @param sigmaT          消散係数 σt = σa + σs [1/m]
/// @param ambientLight    水面に入射する環境光（ComputeUnderwaterAmbientLight）
/// @details 透過項 + 均質媒質の単一散乱解析解。
///          浅瀬では transmittance がまだ緑・青を通すため海底アルベド（白砂）が
///          エメラルドに、深瀬では透過が消えて (σs/σt)·L の水固有の青に収束する。
///          太陽 → 海底の下り光路の減衰は DeferredLighting の水中ライティング置換
///          （RT コースティクス）が海底色に織り込み済みなので、ここでは掛けない
///          （掛けると二重計上。呼び出し側のコメント参照）。
float3 ComputeWaterVolumetricColor(
    float3 refractionColor, float3 transmittance,
    float3 sigmaS, float3 sigmaT, float3 ambientLight)
{
    float3 transmitted = refractionColor * transmittance;
    float3 inscatter = (sigmaS / sigmaT) * ambientLight * (1.0f - transmittance);
    return transmitted + inscatter;
}

// アルファのエンコード規約・IsRTPathValid / IsRTColorValid / DecodeRTOpticalPath は
// Common/WaterRefractionEncoding.hlsli（RTWaterRefraction.hlsl と共有）が唯一の情報源。
// 「光路長が有効か」と「色が有効か」を分けているのが要点。色が取れないだけの
// ピクセルでも光路長は正しいので、水柱厚さの推定を別の量へ切り替えてはいけない
// （切り替えると境界が透過率の段差＝波打ち際の二重線になる）。

float3 ResolveWaterTransmissionColor(uint2 pixelCoord, float2 screenUV)
{
    float4 rtRefraction = gRTWaterRefractionColor.Load(int3(pixelCoord, 0));

    // ★ここで IsRTColorValid の 2 値で切り替えてはいけない★
    // RTWaterRefraction.hlsl は「屈折先のシーン色」と「屈折させていない自分の
    // ピクセルのシーン色（フォールバック）」を depthConfidence × edgeFade で
    // 連続ブレンドした結果を rgb に書いている。ここで 2 値切替を入れると、
    // 岸際に帯状に出る色フォールバック領域の縁が色の段差になり、
    // 波打ち際に沿った細い暗線として見える（光路長で起きたのと同じ事故）。
    //
    // ヒットしていないピクセルの rgb もフォールバック色（＝この関数の else と
    // 同じ値）なので、実質どちらでも同じだが、RT パスが走っていないフレーム
    // （テクスチャがダミー）のために else 側は残す。
    if (IsRTPathValid(rtRefraction.a) > 0.5f) {
        return rtRefraction.rgb;
    }

    return gSceneColor.Sample(gLinearClamp, screenUV).rgb;
}

float4 SampleRTWaterRefraction(uint2 pixelCoord)
{
    return gRTWaterRefractionColor.Load(int3(pixelCoord, 0));
}

float3 VisualizeRTRefractionReason(float reasonCode)
{
    // 緑 = 光路長も色も有効 / 橙 = ヒット済みで光路長のみ有効（色はフォールバック）
    if (reasonCode > 1.0f) return float3(1.0f, 0.5f, 0.0f);
    if (reasonCode >= kRTSuccessRangeMin) return float3(0.0f, 1.0f, 0.0f);

    const float reasonIndex = floor(reasonCode * 255.0f + 0.5f);

    if (reasonIndex == 1.0f) return float3(1.0f, 0.0f, 0.0f);
    if (reasonIndex == 2.0f) return float3(1.0f, 0.5f, 0.0f);
    if (reasonIndex == 3.0f) return float3(1.0f, 1.0f, 0.0f);
    if (reasonIndex == 4.0f) return float3(0.8f, 0.0f, 1.0f);
    if (reasonIndex == 5.0f) return float3(0.0f, 1.0f, 1.0f);
    if (reasonIndex == 6.0f) return float3(0.0f, 0.0f, 1.0f);
    if (reasonIndex == 7.0f) return float3(1.0f, 0.0f, 1.0f);
    if (reasonIndex == 9.0f) return float3(1.0f, 1.0f, 1.0f);

    return float3(0.15f, 0.15f, 0.15f);
}

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
    float dist = length(gCamera.worldPosition - input.worldPosition);

    float2 slope = float2(0.0f, 0.0f);
    [unroll]
    for (int ci = 0; ci < kFFTCascadeCount; ++ci)
    {
        // ワールドXZ を回転格子系へ（FFTWater.VS / RT と同一の写像）
        float2 cuv = ComputeFFTCascadeUV(input.worldPosition.xz, ci);
        float3 enc = gFFTOceanNormal.Sample(gSampler, float3(cuv, (float)ci)).xyz;
        float3 nLocal = normalize(enc * 2.0f - 1.0f); // (x=+texU, y=up, z=+texV)
        // 小パッチほど近距離でフェードアウト（パッチ長比例のフェード区間）。
        float fade = 1.0f - smoothstep(kFFTCascadePatch[ci] * 8.0f, kFFTCascadePatch[ci] * 40.0f, dist);
        // テクスチャ格子系の傾きをワールドへ逆回転してから合算する
        float2 slopeTex = nLocal.xz / max(nLocal.y, 1.0e-3f);
        slope += RotateFromFFTCascadeGrid(slopeTex, ci) * fade;
    }

    // 波群エンベロープ: 変位（FFTWater.VS）と同じ変調を傾きへ掛け、幾何と法線を一致させる
    slope *= ComputeFFTWaveGroupEnvelope(input.worldPosition.xz);

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
        float2 cuv = input.worldPosition.xz / kFFTCascadePatch[0];
        float3 encodedNormal = gFFTOceanNormal.Sample(gSampler, float3(cuv, 0.0f)).xyz;
        // 波群エンベロープを傾きへ掛け、実ジオメトリ（変位×エンベロープ）と整合させる
        float3 nLocal = normalize(encodedNormal * 2.0f - 1.0f);
        float2 slopeTex = (nLocal.xz / max(nLocal.y, 1.0e-3f))
            * ComputeFFTWaveGroupEnvelope(input.worldPosition.xz);
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

// ===== 反射のグロッシー化（ラフネスを考慮した反射）=====
// 平面反射(gReflectionTexture)は完全な鏡像であり、明るい空がそのままフレネルで
// 波面へ塗られるため、うねりの向きに沿ってハードな明暗の斑（水色/青のまだら）が出る。
// 実際の水面は未解像の微細なさざ波が「ラフネス」として働き、
//  (1) 反射をにじませ（グロッシー反射）、
//  (2) かすめ角では微細斜面同士の幾何遮蔽で反射スパイクを抑える。
// この 2 つを再現して、穏やかな海が均一に見えるようにする。
// 0.55 → 0.20: 実際の穏やかな水面の実効マイクロラフネスは 0.02〜0.1 程度で、
// 0.55 は嵐の海に相当する過大な値だった。かすめ角の「鏡のような空の映り込み」が
// 幾何遮蔽で半減し、水面の輝き・透明感を大きく損なっていたため緩和
// （まだらの真因＝反射ビューへの水面自己描画は修正済み）。
static const float kWaterReflectionMicroRoughness = 0.20f; // 未解像さざ波の実効ラフネス
static const float kWaterReflectionBlurTexels = 3.0f; // 反射のにじみ半径（テクセル基準。5→3: 反射のシャープさを回復）

/// @brief 反射テクスチャをラフネス相当でにじませてサンプリングする（グロッシー反射）
/// @param screenUV スクリーンUV
/// @param grazing  かすめ具合 = 1 - cosθ（大きいほど反射が伸び・ぼける）
/// @note 現在この関数を呼ぶのは Water.Debug.hlsli の可視化モード 19 だけ。
///       本体の合成は DXR 反射（RT レイが波法線で反射方向を計算済み）を
///       screenUV でそのまま引くため、にじませ処理を通していない。
float3 SampleGlossyReflection(float2 screenUV, float grazing)
{
    uint reflWidth = 1;
    uint reflHeight = 1;
    gReflectionTexture.GetDimensions(reflWidth, reflHeight);
    const float2 texel = 1.0f / float2(reflWidth, reflHeight);

    // 反射像は面が寝るほど鉛直方向へ伸びるため縦を強めに、かすめ角ほど広くぼかす。
    const float2 radius = kWaterReflectionBlurTexels * texel * float2(1.0f, 2.0f) * (1.0f + grazing * 2.0f);

    const float2 kOffsets[9] = {
        float2( 0.0f,  0.0f),
        float2(-1.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
        float2( 0.0f, -1.0f), float2( 0.0f,  1.0f),
        float2(-1.0f,  0.0f), float2( 1.0f,  0.0f)
    };
    const float kWeights[9] = { 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 2.0f };

    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        const float2 uv = saturate(screenUV + kOffsets[i] * radius);
        sum += gReflectionTexture.Sample(gLinearClamp, uv).rgb * kWeights[i];
        weightSum += kWeights[i];
    }
    return sum / weightSum;
}

/// @brief かすめ角の反射スパイクを微細さざ波の幾何遮蔽で抑える係数
/// @details Schlick-GGX の視線側幾何項に相当。cosθ→0（かすめ角）で 0 に近づき、
///          明るい空の反射が波の裏面へハードに乗るのを弱める。cosθ→1 では 1（無影響）。
float ReflectionGeometricOcclusion(float cosTheta)
{
    const float k = kWaterReflectionMicroRoughness;
    return cosTheta / max(cosTheta * (1.0f - k) + k, 1.0e-4f);
}

// ===== 太陽のスペキュラグリッター（解析的 GGX）=====
// 反射有効時、PBR フォワード出力（太陽の解析的スペキュラを含む）は平面反射像で
// 「置き換え」られるため、太陽ハイライトは鏡像の太陽ディスク頼みになる。
// だが鏡像の太陽はグロッシーぼかし＋輝度圧縮でほぼ消えてしまい、
// 参照画像のような波のきらめき（サングリッター）が全く出ない。
// ここでは太陽の鏡面反射だけをディテール法線＋GGX で解析的に計算し、
// フレネル合成後に加算で復元する。空・環境光は平面反射側にのみ含まれるので
// エネルギーの二重計上はない（太陽ディスク分の平面反射側エネルギーは
// ぼかし・圧縮で実質失われているため、加算しても過大にならない）。
float3 ComputeSunGlintSpecular(float3 normal, float3 viewDir)
{
    // 水の垂直入射反射率 F0 ≈ 0.02
    const float3 kWaterF0 = float3(0.02f, 0.02f, 0.02f);
    // グリッターの広がり。マテリアルのラフネスと連動させる
    // （小さいほど鋭く狭いきらめき、大きいほど広く柔らかい光の帯）
    const float glintRoughness = max(gMaterial.roughness, 0.04f);

    float3 totalGlint = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gLightCounts.directionalLightCount; ++i)
    {
        if (gDirectionalLights[i].enabled == 0)
        {
            continue;
        }
        float3 lightVec = -normalize(gDirectionalLights[i].direction);
        float ndotl = saturate(dot(normal, lightVec));
        if (ndotl <= 0.0f)
        {
            continue;
        }
        float3 brdf = CookTorranceBRDF(normal, viewDir, lightVec, glintRoughness, kWaterF0);
        totalGlint += brdf * gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * ndotl;
    }
    return totalGlint;
}

/// @brief 水面専用フォワードパス処理（ForwardMain の discard 処理を削除したバージョン）
/// 水面は常に描画され、alpha 値で透明度を制御する
VertexShaderOutput ToVertexShaderOutput(WaterPSInput input)
{
    VertexShaderOutput output;
    output.position = input.position;
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    output.worldPosition = input.worldPosition;
    output.lightSpacePos = input.lightSpacePos;
    output.tangent = input.tangent;
    output.bitangent = input.bitangent;
    output.clipPosCurrent = input.clipPosCurrent;
    output.clipPosPrev = input.clipPosPrev;
    return output;
}

/// @brief 水面専用フォワード PBR 出力
/// @param surfaceNormal main() で 1 度だけ解決した水面法線（ResolveSurfaceNormal の結果）
/// @details 法線は同一ピクセルで何度も評価すると（FFT では 3 カスケード分の
///          テクスチャサンプルを伴うため）無視できないコストになるので、
///          呼び出し側で 1 度だけ求めて渡す。
PixelShaderOutput WaterForwardMain(WaterPSInput input, float3 surfaceNormal)
{
    VertexShaderOutput forwardInput = ToVertexShaderOutput(input);
    PixelShaderOutput output;

    forwardInput.normal = surfaceNormal;

    // 視線方向と alpha
    float3 toEye = normalize(gCamera.worldPosition - forwardInput.worldPosition);
    float finalAlpha = gMaterial.color.a;

    // アンリット
    if (gMaterial.enableLighting == 0)
    {
        output.color = gMaterial.color;
        return output;
    }

    // 水面はテクスチャを使わず、マテリアル値のみで PBR パラメータを構成する
    float metallic = gMaterial.metallic;
    float roughness = max(gMaterial.roughness, 0.01f);
    float ao = 1.0f; // 水面は AO マップを持たないため遮蔽なし固定

    float3 albedo = gMaterial.color.rgb;

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(forwardInput, albedo, metallic, roughness, ao, toEye, float4(1.0f, 1.0f, 1.0f, 1.0f));
    output.color.a = finalAlpha;

    // IBL
    output.color.rgb += ApplyIBL(forwardInput, albedo, metallic, roughness, ao, toEye);

    return output;
}

// デバッグ可視化（22 モード）は本体から分離してある。
// このファイルが宣言する資源・関数に依存するため、必ずここで include すること
// （依存の一覧は Water.Debug.hlsli の冒頭に明記）。
#include "Water.Debug.hlsli"

/// @brief 水柱厚さの解決結果（デバッグ表示が参照する中間量も含む）
struct WaterColumnResult
{
    float column;         ///< Beer-Lambert に渡す水中光路長 [m]
    float sceneDepthNDC;  ///< 背景の NDC 深度
    float sceneDepthView; ///< 背景のビュー空間線形深度 [m]
    float waterDepthView; ///< 水面自身のビュー空間線形深度 [m]
    bool hasValidDepth;   ///< 水柱厚さが有効に求まったか
};

/// @brief 水中光路長（水柱厚さ）を解決する
/// @param surfaceNormal main() で 1 度だけ解決した水面法線
/// @details 水柱厚さの供給源は 4 つあり、この順で上書き・合成される。
///          いずれも「ピクセル単位で切り替わると境界が線になる」性質があるため、
///          最後に解析値との連続ブレンドで浅瀬側を吸収している。
///
///   (A) スクリーン空間近似  … 背景と水面のビュー空間Z差 × 屈折換算。
///                             背景ジオメトリがあり RT がミスした場合に効く。
///   (B) 無限水柱            … 背景が far plane（外洋・水平線）。透過ゼロへ収束させる。
///   (C) RT 実測光路長       … 屈折レイがヒットしていれば最優先。表示内容と吸収量が一致する。
///   (D) 解析的な鉛直水深    … 分岐を持たない連続場。浅瀬（〜1m）では 100% これを使い、
///                             4m へ向けて (A)/(C) へ滑らかに移行する。
///
///          浅瀬は吸収がほぼ効かないため (A)/(C) の切り替え段差が減衰されずそのまま
///          見えてしまう。(D) で置き換えることで、RT の成功/失敗や深度不一致がどう
///          転んでも波打ち際の見た目に影響しなくなる（白線・二重線の恒久対策）。
WaterColumnResult ResolveWaterColumn(
    WaterPSInput input, float3 surfaceNormal, float2 screenUV, uint2 pixelCoord)
{
    WaterColumnResult result;
    result.column = 0.0f;
    result.sceneDepthNDC = 1.0f;
    result.sceneDepthView = 0.0f;
    result.waterDepthView = 0.0f;
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
        analyticColumn = ComputeAnalyticWaterColumn(
            input.worldPosition, result.sceneDepthView, result.waterDepthView, refractedView);

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

    // (C) RT 実測光路長。ヒットしていれば (A)/(B) より優先する。
    // スクリーン空間近似は水面ピクセル直下の素の深度（屈折前）を使っており、
    // 屈折で表示位置がズレた分だけ吸収量が表示内容と食い違う
    // （＝水中オブジェクトが水面に浮いて見える一因）。
    // RT は「色が取れなかった（画面外・DepthMismatch）」ケースでも光路長は有効なので、
    // ここで別の推定量へ切り替える必要はない（切り替えると境界が透過率の段差になる）。
    const float rtAlpha = SampleRTWaterRefraction(pixelCoord).a;
    if (IsRTPathValid(rtAlpha) > 0.5f)
    {
        result.column = DecodeRTOpticalPath(rtAlpha);
        result.hasValidDepth = true;
    }

    // 浅瀬を (D) へ寄せる。背景が far plane のときは海底が無く解析値が定義できない
    // ため除外する（そこは (B) の無限水柱が連続的に効く）。
    if (hasBackgroundGeometry)
    {
        const float deepWeight = smoothstep(
            kAnalyticColumnFullMeters, kAnalyticColumnBlendEndMeters, analyticColumn);
        result.column = lerp(analyticColumn, result.column, deepWeight);
        result.hasValidDepth = true;
    }

    return result;
}

PixelShaderOutput main(WaterPSInput input)
{
    // ---- 1. 水面法線を 1 度だけ解決する ----
    // FFT 経路では 3 カスケード分のテクスチャサンプルを伴うため、
    // 以降の PBR・水柱厚さ・フレネル・グリッターで使い回す。
    const float3 surfaceNormal = ResolveSurfaceNormal(input);

    // ---- 2. スクリーン UV を計算する ----
    uint sceneDepthWidth = 1;
    uint sceneDepthHeight = 1;
    gSceneDepth.GetDimensions(sceneDepthWidth, sceneDepthHeight);
    float2 screenUV = input.position.xy / float2(sceneDepthWidth, sceneDepthHeight);
    screenUV = saturate(screenUV);
    uint2 pixelCoord = min(uint2(input.position.xy), uint2(sceneDepthWidth - 1, sceneDepthHeight - 1));

    // ---- 3. 水面専用 PBR フォワード出力をベースにする（discard なし）----
    // 反射有効かつ空環境マップ有効のフレームでは、下の合成で reflectColor が
    // 必ず「RT 反射色」か「空キューブマップ色」で置き換わるため、
    // フォワード PBR の rgb は 1 度も読まれない（＝全ライトの Cook-Torrance と
    // IBL サンプルが丸ごと無駄になる）。その場合だけ計算を省く。
    // 空環境マップが無効なときは、RT がミスしたピクセルのフォールバックとして
    // PBR 出力が実際に使われるので省略できない。
    const bool forwardColorUnused = (gReflectionEnabled != 0) && (gSkyEnvReflectionEnabled != 0);
    PixelShaderOutput output;
    if (forwardColorUnused)
    {
        output.color = float4(0.0f, 0.0f, 0.0f, gMaterial.color.a);
    }
    else
    {
        output = WaterForwardMain(input, surfaceNormal);
    }
    float baseCoverage = saturate(output.color.a);

    // ---- 4. 波長依存 Beer-Lambert による透過率の計算 ----
    // σ が RGB で異なるため、同じ光路長でも赤→緑→青の順に減衰し、
    // 浅瀬エメラルド→深瀬青の色相遷移が指数則から自動的に生じる。
    float3 sigmaA = max(gAbsorptionCoeff, 0.0f);
    float3 sigmaS = max(gScatteringCoeff, 0.0f);
    float3 sigmaT = max(sigmaA + sigmaS, 1.0e-4f);

    const WaterColumnResult waterColumnResult =
        ResolveWaterColumn(input, surfaceNormal, screenUV, pixelCoord);
    const float waterColumn = waterColumnResult.column;
    const float waterDepthNDC = saturate(input.position.z);
    const float sceneDepthNDC = waterColumnResult.sceneDepthNDC;
    const float sceneDepthView = waterColumnResult.sceneDepthView;
    const float waterDepthView = waterColumnResult.waterDepthView;
    const bool hasValidDepthFade = waterColumnResult.hasValidDepth;

    // Beer-Lambert 則（波長別）: exp(-σt·d)
    float3 transmittance = exp(-sigmaT * waterColumn);

    // ---- 5. 視線方向と Fresnel 係数を計算する ----
    float3 viewDir = normalize(gCamera.worldPosition - input.worldPosition);
    float3 geomNormal = surfaceNormal;

    // フレネルは「うねりスケールの低周波法線」で評価する（ResolveFresnelNormal 参照）。
    // 短波長のさざ波斜面を (1-cosθ)^5 に直接食わせるとまだら・スペックルになるため、
    // ミップバイアス付き法線マップで未解像斜面を平均してから角度応答を計算する。
    float3 fresnelNormal = ResolveFresnelNormal(input);
    float cosTheta = saturate(dot(fresnelNormal, viewDir));
    float fresnel = FresnelSchlick(cosTheta, saturate(gFresnelBaseReflectance));
    // 微細さざ波の幾何遮蔽でかすめ角の反射スパイクを抑え、うねりに沿った
    // ハードな明暗の斑（水色/青のまだら）を和らげる。
    float reflectanceWeight = saturate(fresnel * ReflectionGeometricOcclusion(cosTheta) * gFresnelReflectanceScale);

    float3 refractionColor = ResolveWaterTransmissionColor(pixelCoord, screenUV);
    float3 underwaterAmbient = ComputeUnderwaterAmbientLight();
    // refractionColor には既に太陽の下り光路の減衰が織り込まれている（上のコメント参照）。
    // ここで掛けるのは視線の上り光路 transmittance のみ。
    float3 transmissionColor = ComputeWaterVolumetricColor(
        refractionColor,
        transmittance,
        sigmaS,
        sigmaT,
        underwaterAmbient);

    // 反射有効時、環境反射は平面反射像で「置き換える」（加算しない）。
    // 平面反射像には空・雲・太陽そのものが含まれるため、PBR 出力（太陽スペキュラ＋
    // 拡散＋環境光）へさらに加算するとエネルギーが二重計上され、フレネルが立つ
    // 波面で空の輝度が飽和して白飛びの原因になる。
    float3 reflectColor = output.color.rgb;
    if (gReflectionEnabled)
    {
        // ===== DXR 水面反射（鏡像カメラ平面反射の置き換え）=====
        // gReflectionTexture は RTWaterReflectionPass の出力（スクリーン空間・
        // 水面ピクセルごとの反射シーン色）。RT レイが既に波法線で反射方向を
        // 計算済みなので、鏡像方式のような screenUV 歪みは不要。自分の screenUV で
        // そのまま引く。alpha >= 0.5 が成功（反射シーン色）、< 0.5 はミス
        // （反射レイが空へ抜けた／画面外／遮蔽）で、空環境マップへフォールバックする。
        float4 rtReflection = gReflectionTexture.SampleLevel(gLinearClamp, screenUV, 0);
        bool rtHit = rtReflection.a >= 0.5f;

        // 空環境マップによる反射フォールバック（空＋雲を含む TextureCube）。
        // 反射方向は波法線ではなくフラット面法線で計算し、波の斜面ごとの
        // まだら混入を避ける（鏡像時代の知見を踏襲）。
        float3 skyReflectColor = reflectColor;
        if (gSkyEnvReflectionEnabled != 0)
        {
            float3 envReflectDir = reflect(-viewDir, float3(0.0f, 1.0f, 0.0f));
            const float kEnvMipCount = 5.0f;
            const float kEnvMip = kWaterReflectionMicroRoughness * (kEnvMipCount - 1.0f);
            skyReflectColor = gSkyEnvironmentMap.SampleLevel(gLinearClamp, envReflectDir, kEnvMip).rgb;
        }

        // RT ヒット時は反射シーン色、ミス時は空環境マップ。
        reflectColor = rtHit ? rtReflection.rgb : skyReflectColor;

        // ---- 反射の輝度圧縮（白飛び端点の除去）----
        // 反射ソース（SceneColorSnapshot）は水面合成前のライティング済み HDR 色。
        // 露出増幅で極端に明るい輝度が lerp(暗い透過, 明るい反射, フレネル) の
        // 白黒まだらを生むのを防ぐため、膝を超えた輝度だけショルダー圧縮する（色相保持）。
        const float kReflectionCompressKnee = 2.0f;
        const float kReflectionCompressMax = 6.0f;
        float reflLuma = Luminance(reflectColor);
        if (reflLuma > kReflectionCompressKnee)
        {
            float excessLuma = reflLuma - kReflectionCompressKnee;
            float compressedLuma = kReflectionCompressKnee
                + excessLuma / (1.0f + excessLuma / kReflectionCompressMax);
            reflectColor *= compressedLuma / max(reflLuma, 1.0e-5f);
        }
    }

    float3 desiredWaterView = lerp(transmissionColor, reflectColor, reflectanceWeight);
    // 水面ピクセルは常に「水面越しに見える像」（屈折 or 反射）そのものであるべきで、
    // 屈折で曲げていない生のスクリーン座標の背景（gSceneColor.Sample(screenUV)）を
    // 混ぜてはいけない（以前はそれが原因で二重像ゴーストが出ていた）。
    // さらに、透明側の端点には「生の屈折色（refractionColor）」ではなく
    // 「吸収・散乱を通した透過色（transmissionColor）」を使う。
    // 以前は lerp(refractionColor, desiredWaterView, α=0.85) だったため、
    // Beer-Lambert もフレネルも迂回した水底の色が常に約15%そのまま混入し、
    // 深い水でも水底（市松床＋コースティクスのセル模様）が薄い水色の斑として
    // 浮き出ていた（穏やかな海ほどセルが大きく明瞭になり顕著）。
    // transmissionColor は refractionColor から導出されるため「同じ位置を指す像」
    // 同士のブレンドであることは変わらず、ゴーストは発生しない。
    // これにより gMaterial.color.a は実質「フレネル反射成分の不透明度」として働く。
    float surfaceCoverage = saturate(baseCoverage);
    float3 finalWaterComposite = lerp(transmissionColor, desiredWaterView, surfaceCoverage);

    if (gDepthFadeDebugEnabled != 0)
    {
        // 診断表示は Water.Debug.hlsli へ分離している。
        // 参照する中間量はすべてコンテキストで明示的に渡す。
        WaterDebugContext debugContext;
        debugContext.screenUV = screenUV;
        debugContext.pixelCoord = pixelCoord;
        debugContext.sceneDepthNDC = sceneDepthNDC;
        debugContext.waterDepthNDC = waterDepthNDC;
        debugContext.sceneDepthView = sceneDepthView;
        debugContext.waterDepthView = waterDepthView;
        debugContext.waterColumn = waterColumn;
        debugContext.hasValidDepthFade = hasValidDepthFade;
        debugContext.transmittance = transmittance;
        debugContext.transmissionColor = transmissionColor;
        debugContext.reflectColor = reflectColor;
        debugContext.reflectanceWeight = reflectanceWeight;
        debugContext.finalWaterComposite = finalWaterComposite;
        debugContext.surfaceCoverage = surfaceCoverage;
        debugContext.geomNormal = geomNormal;
        debugContext.viewDir = viewDir;
        debugContext.cosTheta = cosTheta;
        debugContext.jacobianData = input.jacobianData;

        output.color = float4(ResolveWaterDebugColor(debugContext), 1.0f);
        return output;
    }

    output.color.rgb = finalWaterComposite;

    // 太陽グリッター（きらめき）を加算で復元する（ComputeSunGlintSpecular 参照）。
    // 反射無効時は WaterForwardMain の PBR 出力（太陽スペキュラ入り）が reflectColor
    // 端点として生きているため、加算すると二重計上になる。反射有効時のみ。
    if (gReflectionEnabled)
    {
        output.color.rgb += ComputeSunGlintSpecular(geomNormal, viewDir);
    }

    // ---- 5. 空気遠近感（Aerial Perspective）----
    // 不透明パスへの合成（AerialPerspective.CS）は水面より前に終わっているため、
    // 水面自身の距離で同じ霞をここで適用する。屈折成分（背景）には背景自身の距離の
    // 霞が既に乗っているが、水面までの区間が重複適用されるのは半透明の慣例的な近似として許容する
    if (gAerialPerspectiveEnabled != 0)
    {
        output.color.rgb = ApplyAerialPerspective(
            output.color.rgb, input.worldPosition, screenUV, gLinearClamp);
    }

    output.color.a = 1.0f;

    return output;
}
