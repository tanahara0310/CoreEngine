// ModelSurface.hlsli
// 3D モデルの材質を Surface() で決め、GBuffer か前方描画へ書く。
// 使う側は Surface() を 1 つ書く。OUT には何もしないときに描かれる値（テクスチャ × gMaterial）が入っている。
// 入口は GBufferMain（不透明）・GBufferMovingMain（スキンの不透明）・ForwardMain（半透明など）と、
// シェーダーグラフの Main Preview の SurfacePreviewVertexMain・SurfacePreviewMain。
// MODEL_SURFACE_CUSTOM_ENTRY_POINTS を定義してから include すると、入口を置かない。
//
// include の前に定義すると変わるもの:
//   SURFACE_VERTEX : SurfaceVertex() を 1 つ書くと頂点を動かせる。頂点シェーダの入口
//                    SurfaceStaticVertexMain・SurfaceSkinnedVertexMain と、レイトレの影の形を作る
//                    SurfaceDeformMain（cs_6_0）が増える。SurfaceVertex() は今と前のフレームの時間で 2 回呼ばれる。
//                    スキンのモデルの頂点シェーダは b0 を行列に使うので、SurfaceVertex() から gMaterial は読めない。
//   SURFACE_OBJECT_SPACE : Surface() の IN に物体の空間の位置・法線・接線と、物体の空間とワールドの間の行列が入る。
//                          頂点シェーダの入口 SurfaceStaticVertexMain・SurfaceSkinnedVertexMain が増える。
//   SURFACE_VERTEX_COLOR : Surface() と SurfaceVertex() の IN に頂点の色が入る（色の無いモデルは白）。
//                          頂点シェーダの入口 SurfaceStaticVertexMain・SurfaceSkinnedVertexMain が増える。
//   SURFACE_LIT    : MaterialComponent の Lighting によらずライトで照らす
//   SURFACE_UNLIT  : MaterialComponent の Lighting によらずライトで照らさない
#ifndef MODEL_SURFACE_HLSLI
#define MODEL_SURFACE_HLSLI

#include "Object3dForward.hlsli"
#include "VertexAnimation.hlsli"

#if defined(SURFACE_VERTEX) || defined(SURFACE_OBJECT_SPACE) || defined(SURFACE_VERTEX_COLOR)
#define SURFACE_OWN_VERTEX_SHADER
#endif

// ===== サンプラー（s0 の gSampler は ObjectMaterial.hlsli・異方性・繰り返し） =====
SamplerState gPointSampler : register(s1); ///< 点・繰り返し
SamplerState gClampSampler : register(s2); ///< 線形・端で止める

// ===== 前方描画の空の環境光（DeferredLighting と同じ定数・SH・キューブマップ） =====
ConstantBuffer<SkyAmbientParams> gSkyAmbient : register(b0, space5);
StructuredBuffer<float4> gSkyIrradianceSH : register(t0, space5); ///< 放射照度畳み込み済みの SH9 係数
TextureCube<float4> gSkySpecularMap : register(t1, space5); ///< プリフィルタ済みの空のキューブマップ

/// @brief Surface() に渡す画素ごとの値
struct SurfaceInput
{
    float2 uv; ///< gMaterial.uvTransform を掛けた UV
    float2 uvRaw; ///< 頂点の UV
    float3 positionWS; ///< ワールドの位置
    float3 normalWS; ///< 補間した頂点の法線（ワールド・正規化していない）
    float3 tangentWS; ///< 補間した接線（ワールド・正規化していない）
    float3 bitangentWS; ///< 補間した従法線（ワールド・正規化していない。法線マップの緑と同じ画像の上の向き）
    float bitangentSign; ///< cross(法線, 接線) に掛けると画像の上を向く符号（+1 か -1）
    float2 screenPosition; ///< 画面の位置（px）
    float screenDepth; ///< この面の深度の値（0〜1）
#ifdef SURFACE_OBJECT_SPACE
    float3 positionOS; ///< 物体の空間の位置（頂点の関数の後）
    float3 normalOS; ///< 補間した物体の空間の法線（正規化していない）
    float3 tangentOS; ///< 補間した物体の空間の接線（正規化していない）
    float4x4 objectToWorld; ///< 物体の空間 → ワールド（行ベクトル。mul(float4(p, 1), objectToWorld)）
    float4x4 worldToObject; ///< ワールド → 物体の空間
#endif
#ifdef SURFACE_VERTEX_COLOR
    float4 vertexColor; ///< 頂点の色（無いモデルは白）
#endif
};

/// @brief Surface() が決める材質
struct SurfaceOutput
{
    float3 baseColor; ///< ベースカラー（リニア）
    float alpha; ///< gMaterial のアルファカット・ディザに使う
    float3 normalWS; ///< ワールドの法線（正規化済み）
    float metallic;
    float roughness;
    float occlusion; ///< 1 = 遮られていない
    float3 emission; ///< 自己発光（リニア。1 を超えてよい）
};

/// @brief SurfaceVertex() に渡す頂点ごとの値
struct SurfaceVertexInput
{
    float3 positionOS; ///< 物体の空間の位置（植物の揺れなどの頂点アニメーションの後）
    float3 normalOS; ///< 物体の空間の法線
    float3 tangentOS; ///< 物体の空間の接線
    float2 uv; ///< 頂点の UV（gMaterial.uvTransform は掛けない）
    float time; ///< 秒（240 秒で 0 に戻る。今か前のフレームの時間）
    float4x4 objectToWorld; ///< 物体の空間 → ワールド（行ベクトル。mul(float4(p, 1), objectToWorld)）
    float4x4 worldToObject; ///< ワールド → 物体の空間
#ifdef SURFACE_VERTEX_COLOR
    float4 vertexColor; ///< 頂点の色（無いモデルは白）
#endif
};

/// @brief SurfaceVertex() が決める頂点（初めは入力の値）
struct SurfaceVertexOutput
{
    float3 positionOS; ///< 物体の空間の位置
    float3 normalOS; ///< 物体の空間の法線
};

#ifdef SURFACE_VERTEX
/// @brief 使う側が書く頂点の関数
void SurfaceVertex(SurfaceVertexInput IN, inout SurfaceVertexOutput OUT);
#endif

/// @brief GBuffer の 4 枚
struct GBufferOutput
{
    float4 albedoAO : SV_TARGET0; ///< rgb=アルベド, a=AO
    float4 normalRoughness : SV_TARGET1; ///< rgb=ワールド法線(エンコード済み), a=ラフネス（0=アンリット）
    float4 emissiveMetallic : SV_TARGET2; ///< rgb=エミッシブ, a=メタリック
    float4 motionVector : SV_TARGET3; ///< rg=モーションベクター（NDC空間の2Dオフセット）, b=水面フラグ（0）, a=動く物（1）
};

/// @brief 使う側が書く材質の関数
void Surface(SurfaceInput IN, inout SurfaceOutput OUT);

/// @brief 頂点シェーダーの出力から Surface() に渡す値を作る
SurfaceInput MakeSurfaceInput(VertexShaderOutput input)
{
    SurfaceInput IN;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    IN.uv = transformedUV.xy;
    IN.uvRaw = input.texcoord;
    IN.positionWS = input.worldPosition;
    IN.normalWS = input.normal;
    IN.tangentWS = input.tangent;
    IN.bitangentSign = NormalMapBitangentSign(input.normal, input.tangent,
        ddx(input.worldPosition), ddy(input.worldPosition), ddx(IN.uv), ddy(IN.uv));
    IN.bitangentWS = input.bitangent * IN.bitangentSign;
    IN.screenPosition = input.position.xy;
    IN.screenDepth = input.position.z;
#ifdef SURFACE_OBJECT_SPACE
    IN.positionOS = input.objectPosition;
    IN.normalOS = input.objectNormal;
    IN.tangentOS = input.objectTangent;
    IN.objectToWorld = transpose(float4x4(input.objectToWorld0, input.objectToWorld1, input.objectToWorld2,
        float4(0.0f, 0.0f, 0.0f, 1.0f)));
    IN.worldToObject = transpose(float4x4(input.worldToObject0, input.worldToObject1, input.worldToObject2,
        float4(0.0f, 0.0f, 0.0f, 1.0f)));
#endif
#ifdef SURFACE_VERTEX_COLOR
    IN.vertexColor = input.vertexColor;
#endif
    return IN;
}

/// @brief モデルのテクスチャと gMaterial から材質を作る
SurfaceOutput DefaultSurface(SurfaceInput IN)
{
    SurfaceOutput OUT;
    float4 textureColor = gTexture.Sample(gSampler, IN.uv);
    OUT.baseColor = gMaterial.color.rgb * textureColor.rgb;
    OUT.alpha = gMaterial.color.a * textureColor.a;
    OUT.normalWS = SampleNormalMapWorld(IN.normalWS, IN.tangentWS, IN.bitangentSign, IN.uv);

    float metallic = 0.0f;
    float roughness = 1.0f;
    float occlusion = 1.0f;
    GetPBRParameters(IN.uv, metallic, roughness, occlusion);
    OUT.metallic = metallic;
    OUT.roughness = roughness;
    OUT.occlusion = occlusion;
    OUT.emission = GetEmissive(IN.uv);
    return OUT;
}

/// @brief ライトで照らさないか（SURFACE_LIT・SURFACE_UNLIT が無ければ gMaterial.enableLighting で決める）
bool SurfaceIsUnlit()
{
#if defined(SURFACE_UNLIT)
    return true;
#elif defined(SURFACE_LIT)
    return false;
#else
    return gMaterial.enableLighting == 0;
#endif
}

/// @brief モデルの法線マップの向き（接空間・-1〜1。法線マップが無ければ (0, 0, 1)）
float3 SurfaceModelNormalTangent(SurfaceInput IN)
{
    if (gMaterial.useNormalMap == 0)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return UnpackNormalMap(gNormalMap.Sample(gSampler, IN.uv));
}

/// @brief 秒（240 秒で 0 に戻る）
float SurfaceTime()
{
    return gVertexAnim.time;
}

/// @brief 前のフレームからの秒
float SurfaceDeltaTime()
{
    return gVertexAnim.time - gVertexAnim.prevTime;
}

/// @brief 物の表面からカメラへの向き（正規化済み）
float3 SurfaceViewDirection(SurfaceInput IN)
{
    return normalize(gCamera.worldPosition - IN.positionWS);
}

/// @brief 接空間の法線（法線マップの値を -1〜1 にしたもの）をワールドの向きにする
float3 SurfaceTangentToWorld(SurfaceInput IN, float3 normalTS)
{
    return normalize(mul(normalTS, NormalMapTangentFrame(IN.normalWS, IN.tangentWS, IN.bitangentSign)));
}

/// @brief ワールドの向きを接空間（x = 接線・y = 従法線（画像の上）・z = 法線）へ移す
float3 SurfaceWorldToTangent(SurfaceInput IN, float3 directionWS)
{
    return mul(NormalMapTangentFrame(IN.normalWS, IN.tangentWS, IN.bitangentSign), directionWS);
}

/// @brief 頂点の関数の接空間の軸（頂点の法線と接線をワールドへ移して作る。従法線は UV を鏡映していない面の画像の上）
float3x3 SurfaceVertexTangentFrame(SurfaceVertexInput IN)
{
    const float3 N = mul((float3x3)IN.worldToObject, IN.normalOS);
    const float3 T = mul(IN.tangentOS, (float3x3)IN.objectToWorld);
    return NormalMapTangentFrame(N, T, -1.0f);
}

/// @brief 頂点の関数で、接空間の向きをワールドの向きにする（正規化済み）
float3 SurfaceVertexTangentToWorld(SurfaceVertexInput IN, float3 directionTS)
{
    return normalize(mul(directionTS, SurfaceVertexTangentFrame(IN)));
}

/// @brief 頂点の関数で、ワールドの向きを接空間へ移す
float3 SurfaceVertexWorldToTangent(SurfaceVertexInput IN, float3 directionWS)
{
    return mul(SurfaceVertexTangentFrame(IN), directionWS);
}

/// @brief Surface() の結果を GBuffer へ書く
/// @param movingObject 1 = 動く物（スキンメッシュ）。motionVector.a に書く
GBufferOutput SurfaceGBuffer(VertexShaderOutput input, float movingObject)
{
    GBufferOutput output;

    SurfaceInput IN = MakeSurfaceInput(input);
    SurfaceOutput OUT = DefaultSurface(IN);
    Surface(IN, OUT);

    // アルファカット（ディザリング or 通常テスト）— 前方描画と共通判定
    if (ShouldDiscardByAlpha(OUT.alpha, input.position.xy))
    {
        discard;
    }

    float3 albedo = saturate(OUT.baseColor);

    // NDC 空間での現フレームと前フレームの位置の差
    float2 ndcCurrent = input.clipPosCurrent.xy / input.clipPosCurrent.w;
    float2 ndcPrev = input.clipPosPrev.xy / input.clipPosPrev.w;
    output.motionVector = float4(ndcCurrent - ndcPrev, 0.0f, movingObject);

    // アンリット: roughness=0 の印と、emissiveMetallic.rgb の色を DeferredLighting がそのまま出す。
    // 前方描画と同じく 1 を超える色はそのまま書く
    if (SurfaceIsUnlit())
    {
        output.albedoAO = float4(0.0f, 0.0f, 0.0f, 1.0f);
        output.normalRoughness = float4(0.5f, 0.5f, 1.0f, 0.0f);
        output.emissiveMetallic = float4(max(OUT.baseColor, 0.0f), 0.0f);
        return output;
    }

    float3 encodedNormal = OUT.normalWS * 0.5f + 0.5f;
    float metallic = saturate(OUT.metallic);
    float roughness = saturate(max(OUT.roughness, 0.01f));
    float ao = saturate(OUT.occlusion);

    output.albedoAO = float4(albedo, ao);
    output.normalRoughness = float4(encodedNormal, roughness);
    output.emissiveMetallic = float4(max(OUT.emission, 0.0f), metallic);

    return output;
}

/// @brief 前方描画の環境光（空の環境光があれば DeferredLighting と同じ式、無ければ半球ランバート）
/// @details 材質の値は GBuffer に書くときと同じ範囲に収めて使う
float3 SurfaceForwardAmbient(VertexShaderOutput input, SurfaceOutput OUT, float3 toEye)
{
    float3 albedo = saturate(OUT.baseColor);
    float metallic = saturate(OUT.metallic);
    float roughness = saturate(max(OUT.roughness, 0.01f));
    float ao = saturate(OUT.occlusion);
    if (gSkyAmbient.enabled != 0)
    {
        float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), albedo, metallic);
        return EvaluateSkyAmbient(gSkyIrradianceSH, gSkySpecularMap, gClampSampler,
            gSkyAmbient.scale, gSkyAmbient.specularEnabled, OUT.normalWS, toEye, albedo, metallic, roughness, ao, F0);
    }
    return CalculateFallbackAmbient(input, albedo, metallic, ao);
}

/// @brief Surface() の結果をライトで照らして色を出す
PixelShaderOutput SurfaceForward(VertexShaderOutput input)
{
    PixelShaderOutput output;

    SurfaceInput IN = MakeSurfaceInput(input);
    SurfaceOutput OUT = DefaultSurface(IN);
    Surface(IN, OUT);

    input.normal = OUT.normalWS;
    float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

    // アルファカット（ディザリング or 通常テスト）— GBuffer と共通判定
    if (ShouldDiscardByAlpha(OUT.alpha, input.position.xy))
    {
        discard;
    }

    // アンリット
    if (SurfaceIsUnlit())
    {
        output.color = float4(OUT.baseColor, OUT.alpha);
        output.color.rgb = ApplyFog(gFog, input.worldPosition, output.color.rgb);
        return output;
    }

    float roughness = max(OUT.roughness, 0.01f);

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(input, OUT.baseColor, OUT.metallic, roughness, OUT.occlusion, toEye,
        float4(OUT.baseColor, OUT.alpha));
    output.color.rgb += SurfaceForwardAmbient(input, OUT, toEye);
    output.color.a = OUT.alpha;

    // エミッシブ（自己発光: ライティングの影響を受けず加算）
    output.color.rgb += OUT.emission;

    // フォグ（全画面合成パスと同じ数式）。不透明フォワードでは gFog が「何もしない」バリアント
    output.color.rgb = ApplyFog(gFog, input.worldPosition, output.color.rgb);

    // ディザリング時はアルファを 1.0 に固定
    if (gMaterial.enableDithering != 0)
        output.color.a = 1.0f;

    return output;
}

/// @brief 静的なモデルの頂点（Object3dVertex.hlsli の VertexShaderInput と同じ並び）
struct SurfaceStaticVertexAttributes
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
    float4 animData : TEXCOORD1;
#ifdef SURFACE_VERTEX_COLOR
    uint vertexID : SV_VertexID;
#endif
};

/// @brief スキン済みの頂点（GBufferSkinning.VS.hlsl の VertexShaderInput と同じ並び）
struct SurfaceSkinnedVertexAttributes
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
    uint vertexID : SV_VertexID;
};

#ifdef SURFACE_VERTEX_COLOR
/// @brief 頂点の色（1 頂点 4 バイトの RGBA8・頂点の番号で引く。色の無いモデルには白 1 つだけの配列が差される）
StructuredBuffer<uint> gVertexColors : register(t1, space1);

/// @brief 頂点の色を読む（配列の外は白）
float4 SurfaceLoadVertexColor(uint vertexID)
{
    uint count;
    uint stride;
    gVertexColors.GetDimensions(count, stride);
    if (vertexID >= count)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f);
    }
    const uint packed = gVertexColors[vertexID];
    return float4(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu, packed >> 24) / 255.0f;
}
#endif

/// @brief 物体の空間の値と頂点の色を、ピクセルシェーダへ渡す値に書く（SURFACE_OBJECT_SPACE・SURFACE_VERTEX_COLOR のとき）
void SurfaceWriteVertexExtras(inout VertexShaderOutput output, float3 positionOS, float3 normalOS, float3 tangentOS,
    float4 vertexColor, TransformationMatrix mtx)
{
#ifdef SURFACE_OBJECT_SPACE
    output.objectPosition = positionOS;
    output.objectNormal = normalOS;
    output.objectTangent = tangentOS;
    const float4x4 objectToWorldT = transpose(mtx.World);
    output.objectToWorld0 = objectToWorldT[0];
    output.objectToWorld1 = objectToWorldT[1];
    output.objectToWorld2 = objectToWorldT[2];
    output.worldToObject0 = mtx.WorldInversTranspose[0];
    output.worldToObject1 = mtx.WorldInversTranspose[1];
    output.worldToObject2 = mtx.WorldInversTranspose[2];
#endif
#ifdef SURFACE_VERTEX_COLOR
    output.vertexColor = vertexColor;
#endif
}

/// @brief 物体の空間の頂点を、ピクセルシェーダへ渡す値にする
/// @param prevPosition 前のフレームの物体の空間の位置（モーションベクター用）
VertexShaderOutput SurfaceTransformVertex(float4 position, float3 normalOS, float3 tangentOS, float2 uv,
    float4 prevPosition, TransformationMatrix mtx)
{
    VertexShaderOutput output;
    output.texcoord = uv;
    output.position = mul(position, mtx.WVP);
    output.normal = normalize(mul(normalOS, (float3x3) mtx.WorldInversTranspose));
    output.tangent = normalize(mul(tangentOS, (float3x3) mtx.World));
    output.bitangent = normalize(cross(output.normal, output.tangent));

    float4 worldPos = mul(position, mtx.World);
    output.worldPosition = worldPos.xyz;
    output.lightSpacePos = mul(worldPos, mtx.LightViewProjection);

    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(prevPosition, mtx.PrevWVP);
    return output;
}

/// @brief SurfaceVertex() を 1 回呼ぶ（SURFACE_VERTEX が無ければ入力のまま返す）
/// @param world ワールド行列
/// @param worldInverseTranspose ワールド行列の逆行列の転置
/// @param vertexColor 頂点の色（SURFACE_VERTEX_COLOR のときだけ渡す）
SurfaceVertexOutput SurfaceCallVertex(float3 positionOS, float3 normalOS, float3 tangentOS, float2 uv, float time,
    float4x4 world, float4x4 worldInverseTranspose, float4 vertexColor)
{
    SurfaceVertexOutput OUT;
    OUT.positionOS = positionOS;
    OUT.normalOS = normalOS;
#ifdef SURFACE_VERTEX
    SurfaceVertexInput IN;
    IN.positionOS = positionOS;
    IN.normalOS = normalOS;
    IN.tangentOS = tangentOS;
    IN.uv = uv;
    IN.time = time;
    IN.objectToWorld = world;
    IN.worldToObject = transpose(worldInverseTranspose);
#ifdef SURFACE_VERTEX_COLOR
    IN.vertexColor = vertexColor;
#endif
    SurfaceVertex(IN, OUT);
#endif
    return OUT;
}

#ifndef MODEL_SURFACE_CUSTOM_ENTRY_POINTS
GBufferOutput GBufferMain(VertexShaderOutput input)
{
    return SurfaceGBuffer(input, 0.0f);
}

GBufferOutput GBufferMovingMain(VertexShaderOutput input)
{
    return SurfaceGBuffer(input, 1.0f);
}

PixelShaderOutput ForwardMain(VertexShaderOutput input)
{
    return SurfaceForward(input);
}

// ===== シェーダーグラフの Main Preview（シーンのライトを使わず、決まったライト 1 つと環境光で照らす） =====

/// @brief プレビューの定数（C++ 側 ShaderGraphPreview の SurfacePreviewConstants と一致させる）
struct SurfacePreviewConstants
{
    float4x4 world;
    float4x4 worldInverseTranspose;
    float4x4 viewProjection;
    float3 lightDirection; ///< 光の進む向き（正規化済み）
    float ambient; ///< 環境光の強さ
    float3 lightColor;
    float padding;
};
ConstantBuffer<SurfacePreviewConstants> gSurfacePreview : register(b0, space3);

VertexShaderOutput SurfacePreviewVertexMain(SurfaceStaticVertexAttributes input)
{
    TransformationMatrix mtx;
    mtx.WVP = mul(gSurfacePreview.world, gSurfacePreview.viewProjection);
    mtx.World = gSurfacePreview.world;
    mtx.WorldInversTranspose = gSurfacePreview.worldInverseTranspose;
    mtx.LightViewProjection = mtx.WVP;
    mtx.PrevWVP = mtx.WVP;

    const float4 white = float4(1.0f, 1.0f, 1.0f, 1.0f);
    const SurfaceVertexOutput current = SurfaceCallVertex(input.position.xyz, input.normal, input.tangent,
        input.texcoord, gVertexAnim.time, mtx.World, mtx.WorldInversTranspose, white);
    const float4 position = float4(current.positionOS, input.position.w);
    VertexShaderOutput output = SurfaceTransformVertex(position, current.normalOS, input.tangent, input.texcoord,
        position, mtx);
    SurfaceWriteVertexExtras(output, current.positionOS, current.normalOS, input.tangent, white, mtx);
    return output;
}

float4 SurfacePreviewMain(VertexShaderOutput input) : SV_TARGET
{
    SurfaceInput IN = MakeSurfaceInput(input);
    SurfaceOutput OUT = DefaultSurface(IN);
    Surface(IN, OUT);

    if (ShouldDiscardByAlpha(OUT.alpha, input.position.xy))
    {
        discard;
    }

    float3 color = OUT.baseColor;
    if (!SurfaceIsUnlit())
    {
        const float3 N = OUT.normalWS;
        const float3 V = SurfaceViewDirection(IN);
        const float3 L = -gSurfacePreview.lightDirection;
        const float NdotL = saturate(dot(N, L));
        const float roughness = max(OUT.roughness, 0.04f);
        const float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0), OUT.baseColor, OUT.metallic);
        const float3 specular = CookTorranceBRDF(N, V, L, roughness, F0);
        const float3 diffuse = OUT.baseColor * (1.0f - OUT.metallic);
        color = (diffuse + specular) * gSurfacePreview.lightColor * NdotL
            + OUT.baseColor * gSurfacePreview.ambient * OUT.occlusion + OUT.emission;
    }

    // 画面へそのまま出すので、0〜1 に収めて sRGB にする
    color = pow(saturate(color), 1.0f / 2.2f);
    return float4(color, 1.0f);
}

// ===== 自分の頂点シェーダの入口（SURFACE_VERTEX・SURFACE_OBJECT_SPACE・SURFACE_VERTEX_COLOR のどれかのとき） =====
#ifdef SURFACE_OWN_VERTEX_SHADER

#include "SkinnedMotion.hlsli"

StructuredBuffer<TransformationMatrix> gInstanceData : register(t0, space1);
ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

/// @brief 静的なモデルの頂点シェーダ（GBuffer と前方描画）
VertexShaderOutput SurfaceStaticVertexMain(SurfaceStaticVertexAttributes input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    float3 animatedPos = input.position.xyz;
    float3 prevAnimatedPos;
    ApplyVertexAnimation(input.animData, input.normal, mtx.World, mtx.WorldInversTranspose,
        animatedPos, prevAnimatedPos);

#ifdef SURFACE_VERTEX_COLOR
    const float4 vertexColor = SurfaceLoadVertexColor(input.vertexID);
#else
    const float4 vertexColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
#endif
    const SurfaceVertexOutput current = SurfaceCallVertex(animatedPos, input.normal, input.tangent, input.texcoord,
        gVertexAnim.time, mtx.World, mtx.WorldInversTranspose, vertexColor);
    const SurfaceVertexOutput previous = SurfaceCallVertex(prevAnimatedPos, input.normal, input.tangent,
        input.texcoord, gVertexAnim.prevTime, mtx.World, mtx.WorldInversTranspose, vertexColor);
    VertexShaderOutput output = SurfaceTransformVertex(float4(current.positionOS, input.position.w), current.normalOS,
        input.tangent, input.texcoord, float4(previous.positionOS, input.position.w), mtx);
    SurfaceWriteVertexExtras(output, current.positionOS, current.normalOS, input.tangent, vertexColor, mtx);
    return output;
}

/// @brief スキンのモデルの頂点シェーダ（GBuffer と前方描画。前のフレームは前の姿勢の位置を前の時間で動かす）
VertexShaderOutput SurfaceSkinnedVertexMain(SurfaceSkinnedVertexAttributes input)
{
    const TransformationMatrix mtx = gTransformationMatrix;

#ifdef SURFACE_VERTEX_COLOR
    const float4 vertexColor = SurfaceLoadVertexColor(input.vertexID);
#else
    const float4 vertexColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
#endif
    const float3 prevSkinnedPos = LoadPrevSkinnedPosition(input.vertexID).xyz;
    const SurfaceVertexOutput current = SurfaceCallVertex(input.position.xyz, input.normal, input.tangent,
        input.texcoord, gVertexAnim.time, mtx.World, mtx.WorldInversTranspose, vertexColor);
    const SurfaceVertexOutput previous = SurfaceCallVertex(prevSkinnedPos, input.normal, input.tangent,
        input.texcoord, gVertexAnim.prevTime, mtx.World, mtx.WorldInversTranspose, vertexColor);
    VertexShaderOutput output = SurfaceTransformVertex(float4(current.positionOS, input.position.w), current.normalOS,
        input.tangent, input.texcoord, float4(previous.positionOS, input.position.w), mtx);
    SurfaceWriteVertexExtras(output, current.positionOS, current.normalOS, input.tangent, vertexColor, mtx);
    return output;
}

#endif // SURFACE_OWN_VERTEX_SHADER

// ===== レイトレの影の形を作る入口（SURFACE_VERTEX のときだけ） =====
#ifdef SURFACE_VERTEX

/// @brief レイトレの影の形を作る頂点（C++ 側 VertexData の 64 バイト。VertexAnimationDeform.CS.hlsl と同じ並び）
struct SurfaceDeformVertex
{
    float px, py, pz, pw;
    float u, v;
    float nx, ny, nz;
    float tx, ty, tz;
    float a0, a1, a2, a3;
};

/// @brief ディスパッチごとの値（C++ 側 VertexAnimationDeformer::DeformConstants と一致させる）
struct SurfaceDeformConstants
{
    float4x4 world;
    float4x4 worldInverseTranspose;
    uint indexStart; ///< このサブメッシュのインデックスの開始位置（LOD0）
    uint indexCount;
    uint vertexCount; ///< 範囲外の添字は捨てる
    uint applyVertexAnimation; ///< 1 = 植物の揺れなどを先に掛ける（静的なモデル）
};

StructuredBuffer<SurfaceDeformVertex> gSourceVertices : register(t0, space2);
StructuredBuffer<uint> gIndices : register(t1, space2);
RWStructuredBuffer<SurfaceDeformVertex> gOutputVertices : register(u0, space2);
ConstantBuffer<SurfaceDeformConstants> gDeform : register(b0, space2);

/// @brief 今のフレームの時間で動かした頂点を書く（サブメッシュごとに、インデックスが指す頂点を書く）
[numthreads(64, 1, 1)]
void SurfaceDeformMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gDeform.indexCount)
    {
        return;
    }
    const uint vertexIndex = gIndices[gDeform.indexStart + dispatchThreadID.x];
    if (vertexIndex >= gDeform.vertexCount)
    {
        return;
    }

    SurfaceDeformVertex vertex = gSourceVertices[vertexIndex];
    float3 position = float3(vertex.px, vertex.py, vertex.pz);
    const float3 normal = float3(vertex.nx, vertex.ny, vertex.nz);
    if (gDeform.applyVertexAnimation != 0)
    {
        float3 prevPosition;
        ApplyVertexAnimation(float4(vertex.a0, vertex.a1, vertex.a2, vertex.a3), normal,
            gDeform.world, gDeform.worldInverseTranspose, position, prevPosition);
    }

#ifdef SURFACE_VERTEX_COLOR
    const float4 vertexColor = SurfaceLoadVertexColor(vertexIndex);
#else
    const float4 vertexColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
#endif
    const SurfaceVertexOutput moved = SurfaceCallVertex(position, normal, float3(vertex.tx, vertex.ty, vertex.tz),
        float2(vertex.u, vertex.v), gVertexAnim.time, gDeform.world, gDeform.worldInverseTranspose, vertexColor);
    const float3 movedNormal = normalize(moved.normalOS);
    vertex.px = moved.positionOS.x;
    vertex.py = moved.positionOS.y;
    vertex.pz = moved.positionOS.z;
    vertex.nx = movedNormal.x;
    vertex.ny = movedNormal.y;
    vertex.nz = movedNormal.z;
    gOutputVertices[vertexIndex] = vertex;
}

#endif // SURFACE_VERTEX
#endif // MODEL_SURFACE_CUSTOM_ENTRY_POINTS

#endif // MODEL_SURFACE_HLSLI
