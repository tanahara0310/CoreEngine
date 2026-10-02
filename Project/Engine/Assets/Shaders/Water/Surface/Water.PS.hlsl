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

// ===== FFT Ocean ヤコビアン（FFTOceanFinalize.CS.hlsl 出力）=====
// 各スライス = (Jxx, Jzz, Jxy, detJ_cascade)。泡/砕波の発生判定と可視化に使う。
// 合成 detJ の評価は FFTOceanCascade.hlsli の ComputeFFTCombinedDetJ を必ず通すこと
// （カスケード単体の detJ は和に分配されないため、勝手に足すと誤る）。
Texture2DArray<float4> gFFTOceanJacobian : register(t20);

// ===== FFT Ocean 蓄積泡（FFTOceanFoamAccumulate.CS.hlsl 出力）=====
// カスケード毎の格子空間で時間発展（発生 → 指数減衰）した泡 [0,1]。
// ping-pong の「前フレームで書き終わった側」が毎フレームバインドされる。
Texture2DArray<float> gFFTOceanFoam : register(t21);

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

// ===== 水面の日向率（RTWaterReflectionPass の 2 枚目の出力）=====
// 0 = 水より上の遮蔽物（ヤシ・岩・島）の影 / 1 = 日向。メインライト（0 番）の項だけに掛ける。
// gSunVisibilityEnabled が 1 のフレームだけ読む。
Texture2D<float> gWaterSunVisibility : register(t26);

// ===== 海底の高さ（RTWaterSeabedPass の出力。カメラの周りの範囲のワールド Y）=====
// gSeabedEnabled が 1 のフレームだけ読む。
Texture2D<float> gWaterSeabedHeight : register(t27);

// ===== 岸の泡（WaterShoreFoamPass の出力。(被覆率, 寄せ・引きのずれ x, z, 0)）=====
// gShoreFoamEnabled が 1 のフレームだけ読む。
Texture2D<float4> gWaterShoreFoam : register(t28);

/// @brief 影の中に残す直接光の割合（DeferredLighting・水中コースティクスの影と同じ値）
static const float kShadowedSunFloor = 0.3f;

struct WaterPSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    // ★FFT カスケードを引くときは必ずこれを使う（worldPosition.xz ではない）★
    // FFT の変位・法線・ヤコビアン・蓄積泡はすべて「変位前の参照格子座標 x0」の
    // 関数として書き出されている。描画点は x = x0 + D(x0) なので、worldPosition.xz で
    // 引くと水平変位ぶん（choppiness 2.5・風速12m/s で数 m）ズレた場所を読むことになり、
    // 法線が幾何と一致せず、泡が波頭から外れて出る。
    // VS 側（FFTWater.VS / Water.VS）が変位を足す前の値をここへ渡す。
    float2 baseWorldXZ : TEXCOORD1;
    // 静止水面からの波の高さ [m]。波峰のサブサーフェス透過が使う（WaterSubsurface.hlsli）
    float waveHeight : TEXCOORD2;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float4 lightSpacePos : POSITION1;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
};

// ===== フレーム定数バッファ（VS と共有・b5 の唯一の宣言）=====
#include "../Common/WaterFrameConstants.hlsli"

// ===== 責務別の分割 hlsli =====
// 水柱厚さ（「線を出さない」中核）・体積色・泡（被覆率と見た目）・法線は専用ファイルへ分離している。
// いずれもこのファイルのリソース宣言・b5 に暗黙依存するため、必ずこの位置で
// この順に include する（WaterFoamAppearance は WaterVolume の EvaluateWaterSkyIrradiance を使う。
// 依存の一覧は各ファイル冒頭に明記）。
#include "WaterColumn.hlsli"

/// @brief 水中の放射輝度が水面から空気へ出るときの倍率（1/n²）
static const float kWaterRadianceExitScale = 1.0f / (kWaterRefractiveIndex * kWaterRefractiveIndex);

#include "WaterVolume.hlsli"
#include "WaterFoamCoverage.hlsli"
#include "WaterFoamAppearance.hlsli"
#include "WaterNormals.hlsli"
#include "WaterSubsurface.hlsli"

/// @brief Schlick 近似による Fresnel 係数を計算する
/// @param cosTheta  視線と法線のなす角の余弦（saturate 済み推奨）
/// @param f0        法線入射時の反射率（水面 ≈ 0.02）
float FresnelSchlick(float cosTheta, float f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}



// ===== 反射のにじみ =====
/// @brief 未解像のさざ波の実効ラフネス（空キューブマップのミップ選択に使う）
static const float kWaterReflectionMicroRoughness = 0.20f;

// ===== 反射の画素ならし（本体の合成で必ず通す）=====
/// @brief 反射テクスチャを引くタップの間隔（テクセル）
/// @warning 2 テクセルを超えると、ずれた像が離散的に重なって見える
static const float kWaterReflectionTapSpacingTexels = 1.0f;

/// @brief 反射テクスチャを 3×3 のテントでならして取得する（rgb=色 / a=信頼度）
/// @param screenUV スクリーンUV
/// @param grazing  かすめ具合 = 1 - cosθ（大きいほど縦の間隔を広げる）
/// @details 信頼度（a）はぼかさず中心タップの値を返す。
///          rgb は各タップ自身の信頼度で重み付けして平均する（非水面のタップは入れない）。
float4 SampleGlossyReflectionRGBA(float2 screenUV, float grazing)
{
    uint reflWidth = 1;
    uint reflHeight = 1;
    gReflectionTexture.GetDimensions(reflWidth, reflHeight);
    const float2 texel = 1.0f / float2(reflWidth, reflHeight);

    // かすめ角ほど縦の間隔を 1.5 倍まで広げる（双線形で行の間を補間する）
    const float2 radius = kWaterReflectionTapSpacingTexels * texel
        * float2(1.0f, 1.0f + 0.5f * saturate(grazing));

    const float2 kOffsets[9] = {
        float2( 0.0f,  0.0f),
        float2(-1.0f, -1.0f), float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f), float2( 1.0f,  1.0f),
        float2( 0.0f, -1.0f), float2( 0.0f,  1.0f),
        float2(-1.0f,  0.0f), float2( 1.0f,  0.0f)
    };
    const float kWeights[9] = { 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 2.0f };

    float3 colorSum = float3(0.0f, 0.0f, 0.0f);
    float weightSum = 0.0f;
    float centerConfidence = 0.0f;
    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        const float2 uv = saturate(screenUV + kOffsets[i] * radius);
        const float4 tap = gReflectionTexture.Sample(gLinearClamp, uv);
        const float tapConfidence = saturate((tap.a - kRTSuccessRangeMin) * 2.0f);
        if (i == 0)
        {
            centerConfidence = tapConfidence;
        }
        const float weight = kWeights[i] * tapConfidence;
        colorSum += tap.rgb * weight;
        weightSum += weight;
    }

    // 全タップが非水面なら色は無効。信頼度 0 で呼び出し側のフォールバックへ渡す。
    if (weightSum <= 1.0e-5f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return float4(colorSum / weightSum, kRTSuccessRangeMin + 0.5f * centerConfidence);
}

/// @brief 可視化モード 19 用の rgb だけのラッパー
float3 SampleGlossyReflection(float2 screenUV, float grazing)
{
    return SampleGlossyReflectionRGBA(screenUV, grazing).rgb;
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
float3 ComputeSunGlintSpecular(
    float3 normal, float3 viewDir, float foamCoverage, float mainLightVisibility, float unresolvedMeanSquareSlope)
{
    // 水の垂直入射反射率 F0 ≈ 0.02
    const float3 kWaterF0 = float3(0.02f, 0.02f, 0.02f);
    // グリッターの広がり。マテリアルのラフネスと連動させる
    // （小さいほど鋭く狭いきらめき、大きいほど広く柔らかい光の帯）。
    // 泡域は微細気泡の散乱でハイライトが大きく柔らかくなるためラフネスを引き上げる
    // （泡被覆による輝度の抑制は呼び出し側の (1-coverage) 倍が担当する）。
    // 遠方で法線から外した細かい波の傾きは、1 ピクセルの中の凹凸としてラフネスへ足す
    const float glintRoughness = max(AddSlopeVarianceToRoughness(
        lerp(gMaterial.roughness, kFoamGlintRoughness, saturate(foamCoverage)),
        unresolvedMeanSquareSlope), 0.04f);

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
        const float visibility = (i == 0) ? mainLightVisibility : 1.0f;
        totalGlint += brdf * gDirectionalLights[i].color.rgb * gDirectionalLights[i].intensity * ndotl
            * visibility;
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

    // 視線方向
    float3 toEye = normalize(gCamera.worldPosition - forwardInput.worldPosition);

    // アンリット
    if (gMaterial.enableLighting == 0)
    {
        output.color = float4(gMaterial.color.rgb, 1.0f);
        return output;
    }

    // 水面はテクスチャを使わず、マテリアル値のみで PBR パラメータを構成する
    float metallic = gMaterial.metallic;
    float roughness = max(gMaterial.roughness, 0.01f);
    float ao = 1.0f; // 水面は AO マップを持たないため遮蔽なし固定

    float3 albedo = gMaterial.color.rgb;

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(forwardInput, albedo, metallic, roughness, ao, toEye, float4(1.0f, 1.0f, 1.0f, 1.0f));
    output.color.a = 1.0f;

    // IBL
    output.color.rgb += ApplyIBL(forwardInput, albedo, metallic, roughness, ao, toEye);

    return output;
}

// デバッグ可視化（WaterDebugViewMode の全モード）は本体から分離してある。
// このファイルが宣言する資源・関数に依存するため、必ずここで include すること
// （依存の一覧は Water.Debug.hlsli の冒頭に明記）。
#include "Water.Debug.hlsli"

// ===== 水面の出力（SceneColor ＋ モーションベクター）=====
// ★水面もモーションベクターを書かなければならない★
// 水面は GBuffer より後のフォワードパスなので、書かないと TAA は水面ピクセルに対して
// 「水の背後にある地形」のモーションベクターで履歴を再投影してしまう。水面（y≈5.8m）と
// 海底（y=0）は視差が違うため、カメラが動いた瞬間だけ履歴が別の場所から引かれ、
// 泡のような高周波の模様が溶けたようにぼける（2026-08-08 実測: カメラ静止時
// meanLaplacian 14.4 に対し移動中 8.7 まで低下。TAA を切ると移動中でも 16.1）。
struct WaterPixelOutput
{
    float4 color : SV_TARGET0;
    float2 motionVector : SV_TARGET1; ///< NDC 差分（GBuffer.PS と同一規約）
};

/// @brief NDC 空間のモーションベクターを求める（GBuffer.PS.hlsl と同じ式）
/// @details TAA 側でジッタ差分を引く前提なので、ここではジッタ込みのクリップ座標から
///          そのまま差を取る（GBuffer と規約を揃えること）。
float2 ComputeWaterMotionVector(WaterPSInput input)
{
    const float2 ndcCurrent = input.clipPosCurrent.xy / max(input.clipPosCurrent.w, 1.0e-6f);
    const float2 ndcPrevious = input.clipPosPrev.xy / max(input.clipPosPrev.w, 1.0e-6f);
    return ndcCurrent - ndcPrevious;
}

WaterPixelOutput main(WaterPSInput input)
{
    WaterPixelOutput waterOutput;
    waterOutput.motionVector = ComputeWaterMotionVector(input);

    // ---- 1. 水面法線を 1 度だけ解決する ----
    // FFT 経路では 3 カスケード分のテクスチャサンプルを伴うため、
    // 以降の PBR・水柱厚さ・フレネル・グリッターで使い回す。
    float unresolvedMeanSquareSlope;
    const float3 surfaceNormal = ResolveSurfaceNormal(input, unresolvedMeanSquareSlope);

    // ---- 2. スクリーン UV を計算する ----
    uint sceneDepthWidth = 1;
    uint sceneDepthHeight = 1;
    gSceneDepth.GetDimensions(sceneDepthWidth, sceneDepthHeight);
    float2 screenUV = input.position.xy / float2(sceneDepthWidth, sceneDepthHeight);
    screenUV = saturate(screenUV);
    uint2 pixelCoord = min(uint2(input.position.xy), uint2(sceneDepthWidth - 1, sceneDepthHeight - 1));

    // ---- 2.5. メインライトの日向率（水より上の遮蔽物の影が水面に落ちているか）----
    // 太陽の鏡面反射（きらめき）は遮られたら残らないので日向率をそのまま掛ける。
    // 拡散の項（泡・水中の散乱・波頭の透過）は、DeferredLighting と水中コースティクスの
    // 影と同じく、影の中でも直接光を kShadowedSunFloor だけ残す。
    const float sunVisibility = (gSunVisibilityEnabled != 0)
        ? gWaterSunVisibility.Load(int3(pixelCoord, 0))
        : 1.0f;
    const float sunDiffuseVisibility = lerp(kShadowedSunFloor, 1.0f, sunVisibility);

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
        output.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    else
    {
        output = WaterForwardMain(input, surfaceNormal);
    }

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

    // フレネルは反射像・透過像と同じ面法線で評価する
    float cosTheta = saturate(dot(surfaceNormal, viewDir));
    float fresnel = FresnelSchlick(cosTheta, saturate(gFresnelBaseReflectance));
    float reflectanceWeight = saturate(fresnel * gFresnelReflectanceScale);

    // ---- 5.5. 泡マスク（dissolve 方式）----
    // foamMask は滑らかな「被覆率」の場（砕波泡と岸際泡の max）。表示形状は
    // 高周波パターンへのしきい値カット（ComputeFoamLace）でレース状に変換する:
    //   lace ＝ 水面の上の白い泡そのもの（縁が鋭いレース・筋・粒）
    //   haze ＝ レースの穴の間と縁の外側の水面下の気泡（白濁。パターンで粒状に変調し霧化を防ぐ）
    // 白波の泡のマスクは FFT の参照格子座標（x0）で評価し、模様は泡と一緒に風下へ流す。
    // 岸の泡は水の粒の静止位置で読み、模様もそこで評価する（寄せ・引きと一緒に動く）。
    // 2 つの泡は模様の動きが違うので、別々にレースを作って重ねる。
    const float whitecapFoam = ComputeFoamMask(input.baseWorldXZ);
    const ShoreFoamResult shoreFoam = ResolveShoreFoam(input.worldPosition, waterColumnResult.viewRayVerticalDepth);
    const float foamMask = max(whitecapFoam, shoreFoam.coverage);
    const float2 whitecapPatternXZ = ComputeWhitecapPatternXZ(input.baseWorldXZ);
    const float2 shorePatternXZ = input.baseWorldXZ + shoreFoam.patternShift;
    WaterFoamLayer whitecapLayer = (WaterFoamLayer)0;
    [branch]
    if (whitecapFoam > 0.0f)
    {
        whitecapLayer = EvaluateFoamLayer(whitecapFoam, whitecapPatternXZ);
    }
    WaterFoamLayer shoreLayer = (WaterFoamLayer)0;
    [branch]
    if (shoreFoam.coverage > 0.0f)
    {
        shoreLayer = EvaluateFoamLayer(shoreFoam.coverage, shorePatternXZ);
    }
    const WaterFoamLayer foamLayer = CombineFoamLayers(whitecapLayer, shoreLayer);
    // 水面の上の泡の割合。拡散層でフレネル反射を持たないので、反射・サングリッター・
    // 波頭の透過光をこの割合だけ遮る
    const float surfaceFoam = foamLayer.lace * saturate(gFoamOpacity);
    // 水面下の気泡の割合。水面より下にあるので、透過してくる光にだけ混ざる
    const float bubbleHaze = foamLayer.haze * saturate(gFoamOpacity) * kFoamHazeOpacity;
    reflectanceWeight *= (1.0f - surfaceFoam);

    // 泡の色（Lambert 白 × 太陽直達 + 天空光）。気泡の粒感として明度を高周波ノイズで揺らす
    float3 foamColor = float3(0.0f, 0.0f, 0.0f);
    if (foamMask > 0.0f)
    {
        const float shoreFoamShare = shoreFoam.coverage / max(whitecapFoam + shoreFoam.coverage, 1.0e-4f);
        const float foamGrain = lerp(FoamGrain(whitecapPatternXZ), FoamGrain(shorePatternXZ), shoreFoamShare);
        foamColor = ComputeFoamColor(surfaceNormal, sunDiffuseVisibility) * foamGrain;
    }

    float3 refractionColor = ResolveWaterTransmissionColor(pixelCoord, screenUV);
    float3 underwaterAmbient = ComputeUnderwaterAmbientLight(sunDiffuseVisibility);
    // refractionColor には既に太陽の下り光路の減衰が織り込まれている（上のコメント参照）。
    // ここで掛けるのは視線の上り光路 transmittance のみ。
    float3 transmissionColor = ComputeWaterVolumetricColor(
        refractionColor,
        transmittance,
        sigmaS,
        sigmaT,
        underwaterAmbient);
    // 水面下の気泡は、その奥から透過してくる光を遮り、自分が散乱した光に置き換える
    transmissionColor = lerp(transmissionColor, foamColor * kFoamHazeTint, bubbleHaze);
    // 水中の放射輝度は、水面から空気へ出るときに屈折で立体角が広がって 1/n² になる
    transmissionColor *= kWaterRadianceExitScale;

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
        // そのまま引く。alpha は「反射色の信頼度」を連続値で運ぶ:
        //   alpha ∈ (0.5, 1.0] … 成功。confidence = (alpha - 0.5) * 2
        //   alpha < 0.5        … 失敗（空へ抜けた／遮蔽）→ confidence 0
        // ★ここを 2 値（rtHit ? rt : sky）で切り替えてはいけない★
        // 反射シーン色と空環境マップは輝度が大きく違うため、切り替えの境界が
        // そのままピクセル単位のギザギザになる。かすめ角では再投影先が画面外へ
        // 出るピクセルが増え、成功/失敗が細かく入り混じるので特に目立つ
        // （水面すれすれの視点で出ていた黒いギザギザの正体。2026-08-09 修正）。
        // 点サンプルではなくグロッシーサンプルを通す（上の関数のコメント参照）。
        // かすめ角ほど半径が広がるので、まさに問題が出る領域で強く効く。
        const float4 rtReflection =
            SampleGlossyReflectionRGBA(screenUV, saturate(1.0f - cosTheta));
        const float rtConfidence = saturate((rtReflection.a - 0.5f) * 2.0f);

        // ★空は RT パス側が「実際にトレースしたレイの向き」で解決済み★
        // ここで受け取るのは 1 枚に解決された反射色なので、原則そのまま使う。
        //
        // 以前はここで空キューブを **完全な平面法線 float3(0,1,0)** で引き、
        // RT 反射と lerp していた。RT 側は波法線に沿って動く像、空側は波に
        // 一切追従しない像なので、成否が細かく入り混じるかすめ角では
        // 「幾何的に食い違う 2 枚が重なる」＝水面際で反射が二重に見える
        // 状態になっていた（2026-08-09 実測で確定・修正）。
        //
        // 以下のフォールバックは「RT パスへ空キューブが渡っていないフレーム」
        // だけの保険。そのときも **波法線** で引き、RT と面を食い違わせない。
        float3 fallbackReflectColor = reflectColor;
        if (gSkyEnvReflectionEnabled != 0)
        {
            const float kEnvMipCount = 5.0f;
            const float envMip = AddSlopeVarianceToRoughness(
                kWaterReflectionMicroRoughness, unresolvedMeanSquareSlope) * (kEnvMipCount - 1.0f);
            float3 envReflectDir = reflect(-viewDir, geomNormal);
            envReflectDir.y = max(abs(envReflectDir.y), 0.01f);
            fallbackReflectColor =
                gSkyEnvironmentMap.SampleLevel(gLinearClamp, envReflectDir, envMip).rgb;
        }

        reflectColor = lerp(fallbackReflectColor, rtReflection.rgb, rtConfidence);

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

    // 透過像（吸収・散乱を通した色）と反射像をフレネルで合成する
    float3 finalWaterComposite = lerp(transmissionColor, reflectColor, reflectanceWeight);

    // 水面の上の泡（レース）を重ねる（白ベタ禁止: gFoamOpacity < 1 で水面下の情報を残す）
    finalWaterComposite = lerp(finalWaterComposite, foamColor, surfaceFoam);

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
        debugContext.geomNormal = geomNormal;
        debugContext.viewDir = viewDir;
        debugContext.cosTheta = cosTheta;
        // ヤコビアン可視化は FFT テクスチャを引くので参照格子座標を渡す
        debugContext.worldXZ = input.baseWorldXZ;
        debugContext.foamMask = foamMask;

        waterOutput.color = float4(ResolveWaterDebugColor(debugContext), 1.0f);
        return waterOutput;
    }

    output.color.rgb = finalWaterComposite;

    // 太陽グリッター（きらめき）を加算で復元する（ComputeSunGlintSpecular 参照）。
    // 反射無効時は WaterForwardMain の PBR 出力（太陽スペキュラ入り）が reflectColor
    // 端点として生きているため、加算すると二重計上になる。反射有効時のみ。
    // 泡は拡散層で鏡面のきらめきを持たない: 被覆分の輝度抑制＋ラフネス引き上げの両方。
    if (gReflectionEnabled)
    {
        output.color.rgb +=
            ComputeSunGlintSpecular(
                geomNormal, viewDir, surfaceFoam, sunVisibility, unresolvedMeanSquareSlope)
            * (1.0f - surfaceFoam);
    }

    // ---- 波峰のサブサーフェス透過（逆光で波の背が緑に光る）----
    // 波を透過して視点へ出てくる光なので、水面で反射されずに「抜けてきた」分だけ、
    // つまり (1 - フレネル反射率) と、水から空気へ出る 1/n² を掛けて加算する。
    output.color.rgb += ComputeWaterSubsurfaceScattering(
        surfaceNormal, viewDir, input.waveHeight, sigmaS, sigmaT, surfaceFoam, sunDiffuseVisibility)
        * (1.0f - reflectanceWeight) * kWaterRadianceExitScale;

    // ---- 5. 空気遠近感（Aerial Perspective）----
    // 不透明パスへの合成（AerialPerspective.CS）は水面より前に終わっているため、
    // 水面自身の距離で同じ霞をここで適用する。屈折成分（背景）には背景自身の距離の
    // 霞が既に乗っているが、水面までの区間が重複適用されるのは半透明の慣例的な近似として許容する
    if (gAerialPerspectiveEnabled != 0)
    {
        output.color.rgb = ApplyAerialPerspective(
            output.color.rgb, input.worldPosition, screenUV, gLinearClamp);
    }

    // ---- 6. 高さフォグ ----
    // 水面は全画面合成パス（HeightFog.CS）より後に描かれるため、そこでは掛かっていない。
    // 不透明と同じ数式（Fog.hlsli）を水面自身の距離で適用する。
    // gFog は BaseModelRenderer がブレンドモードに応じて差す（水面は通常ブレンド＝内散乱あり）。
    output.color.rgb = ApplyFog(gFog, input.worldPosition, output.color.rgb);

    output.color.a = 1.0f;

    waterOutput.color = output.color;
    return waterOutput;
}
