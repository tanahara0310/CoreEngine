// ModelSurface.hlsli
// 3D モデルの材質を Surface() で決め、GBuffer か前方描画へ書く。
// 使う側は Surface() を 1 つ書く。OUT には何もしないときに描かれる値（テクスチャ × gMaterial）が入っている。
// 入口は GBufferMain（不透明）・GBufferMovingMain（スキンの不透明）・ForwardMain（半透明など）。
// MODEL_SURFACE_CUSTOM_ENTRY_POINTS を定義してから include すると、入口を置かない。
#ifndef MODEL_SURFACE_HLSLI
#define MODEL_SURFACE_HLSLI

#include "Object3dForward.hlsli"
#include "VertexAnimation.hlsli"

// ===== サンプラー（s0 の gSampler は ObjectMaterial.hlsli・異方性・繰り返し） =====
SamplerState gPointSampler : register(s1); ///< 点・繰り返し
SamplerState gClampSampler : register(s2); ///< 線形・端で止める

/// @brief Surface() に渡す画素ごとの値
struct SurfaceInput
{
    float2 uv; ///< gMaterial.uvTransform を掛けた UV
    float2 uvRaw; ///< 頂点の UV
    float3 positionWS; ///< ワールドの位置
    float3 normalWS; ///< 補間した頂点の法線（ワールド・正規化していない）
    float3 tangentWS; ///< 補間した接線（ワールド・正規化していない）
    float3 bitangentWS; ///< 補間した従法線（ワールド・正規化していない）
    float2 screenPosition; ///< 画面の位置（px）
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
    float3 emission; ///< 自己発光（GBuffer では 0〜1 に収まる）
};

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
    IN.bitangentWS = input.bitangent;
    IN.screenPosition = input.position.xy;
    return IN;
}

/// @brief モデルのテクスチャと gMaterial から材質を作る
SurfaceOutput DefaultSurface(SurfaceInput IN)
{
    SurfaceOutput OUT;
    float4 textureColor = gTexture.Sample(gSampler, IN.uv);
    OUT.baseColor = gMaterial.color.rgb * textureColor.rgb;
    OUT.alpha = gMaterial.color.a * textureColor.a;
    OUT.normalWS = GetNormalFromMap(IN.normalWS, IN.tangentWS, IN.bitangentWS, IN.uv);

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

/// @brief 秒（240 秒で 0 に戻る）
float SurfaceTime()
{
    return gVertexAnim.time;
}

/// @brief 物の表面からカメラへの向き（正規化済み）
float3 SurfaceViewDirection(SurfaceInput IN)
{
    return normalize(gCamera.worldPosition - IN.positionWS);
}

/// @brief 接空間の法線（法線マップの値を -1〜1 にしたもの）をワールドの向きにする
float3 SurfaceTangentToWorld(SurfaceInput IN, float3 normalTS)
{
    float3 N = normalize(IN.normalWS);
    float3 T = normalize(IN.tangentWS);
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    return normalize(mul(normalTS, float3x3(T, B, N)));
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

    // アンリット: roughness=0 の印と、emissiveMetallic.rgb の色を DeferredLighting がそのまま出す
    if (gMaterial.enableLighting == 0)
    {
        output.albedoAO = float4(0.0f, 0.0f, 0.0f, 1.0f);
        output.normalRoughness = float4(0.5f, 0.5f, 1.0f, 0.0f);
        output.emissiveMetallic = float4(albedo, 0.0f);
        return output;
    }

    float3 encodedNormal = OUT.normalWS * 0.5f + 0.5f;
    float metallic = saturate(OUT.metallic);
    float roughness = saturate(max(OUT.roughness, 0.01f));
    float ao = saturate(OUT.occlusion);

    output.albedoAO = float4(albedo, ao);
    output.normalRoughness = float4(encodedNormal, roughness);
    output.emissiveMetallic = float4(OUT.emission, metallic);

    return output;
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
    if (gMaterial.enableLighting == 0)
    {
        output.color = float4(OUT.baseColor, OUT.alpha);
        output.color.rgb = ApplyFog(gFog, input.worldPosition, output.color.rgb);
        return output;
    }

    float roughness = max(OUT.roughness, 0.01f);

    // PBR ライティング
    output.color.rgb = CalculateAllLighting(input, OUT.baseColor, OUT.metallic, roughness, OUT.occlusion, toEye,
        float4(OUT.baseColor, OUT.alpha));
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
#endif

#endif // MODEL_SURFACE_HLSLI
