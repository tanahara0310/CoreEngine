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
//   SURFACE_LIT    : MaterialComponent の Lighting によらずライトで照らす
//   SURFACE_UNLIT  : MaterialComponent の Lighting によらずライトで照らさない
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
    return gNormalMap.Sample(gSampler, IN.uv).rgb * 2.0f - 1.0f;
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
    float3 N = normalize(IN.normalWS);
    float3 T = normalize(IN.tangentWS);
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    return normalize(mul(normalTS, float3x3(T, B, N)));
}

/// @brief ワールドの向きを接空間（x = 接線・y = 従法線・z = 法線）へ移す
float3 SurfaceWorldToTangent(SurfaceInput IN, float3 directionWS)
{
    float3 N = normalize(IN.normalWS);
    float3 T = normalize(IN.tangentWS);
    T = normalize(T - dot(T, N) * N);
    float3 B = cross(N, T);
    return mul(float3x3(T, B, N), directionWS);
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
    if (SurfaceIsUnlit())
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
};

/// @brief スキン済みの頂点（GBufferSkinning.VS.hlsl の VertexShaderInput と同じ並び）
struct SurfaceSkinnedVertexAttributes
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
};

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
SurfaceVertexOutput SurfaceCallVertex(float3 positionOS, float3 normalOS, float3 tangentOS, float2 uv, float time,
    float4x4 world, float4x4 worldInverseTranspose)
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

    const SurfaceVertexOutput current = SurfaceCallVertex(input.position.xyz, input.normal, input.tangent,
        input.texcoord, gVertexAnim.time, mtx.World, mtx.WorldInversTranspose);
    const float4 position = float4(current.positionOS, input.position.w);
    return SurfaceTransformVertex(position, current.normalOS, input.tangent, input.texcoord, position, mtx);
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

// ===== 頂点を動かす入口（SURFACE_VERTEX のときだけ） =====
#ifdef SURFACE_VERTEX

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

    const SurfaceVertexOutput current = SurfaceCallVertex(animatedPos, input.normal, input.tangent, input.texcoord,
        gVertexAnim.time, mtx.World, mtx.WorldInversTranspose);
    const SurfaceVertexOutput previous = SurfaceCallVertex(prevAnimatedPos, input.normal, input.tangent,
        input.texcoord, gVertexAnim.prevTime, mtx.World, mtx.WorldInversTranspose);
    return SurfaceTransformVertex(float4(current.positionOS, input.position.w), current.normalOS, input.tangent,
        input.texcoord, float4(previous.positionOS, input.position.w), mtx);
}

/// @brief スキンのモデルの頂点シェーダ（GBuffer と前方描画。前のフレームは今の姿勢を前の時間で動かす）
VertexShaderOutput SurfaceSkinnedVertexMain(SurfaceSkinnedVertexAttributes input)
{
    const TransformationMatrix mtx = gTransformationMatrix;

    const SurfaceVertexOutput current = SurfaceCallVertex(input.position.xyz, input.normal, input.tangent,
        input.texcoord, gVertexAnim.time, mtx.World, mtx.WorldInversTranspose);
    const SurfaceVertexOutput previous = SurfaceCallVertex(input.position.xyz, input.normal, input.tangent,
        input.texcoord, gVertexAnim.prevTime, mtx.World, mtx.WorldInversTranspose);
    return SurfaceTransformVertex(float4(current.positionOS, input.position.w), current.normalOS, input.tangent,
        input.texcoord, float4(previous.positionOS, input.position.w), mtx);
}

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

    const SurfaceVertexOutput moved = SurfaceCallVertex(position, normal, float3(vertex.tx, vertex.ty, vertex.tz),
        float2(vertex.u, vertex.v), gVertexAnim.time, gDeform.world, gDeform.worldInverseTranspose);
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
