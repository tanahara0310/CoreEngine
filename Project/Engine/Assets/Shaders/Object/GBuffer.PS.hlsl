#include "Object3d.hlsli"
#include "../Include/Object/ObjectMaterial.hlsli"

// ハイブリッドレンダリングの GBuffer 書き込み専用シェーダー。
// 不透明モデル（BlendMode::None）の全てのメッシュがこのパスを通る。
// ライティングは DeferredLighting.PS.hlsl で行う。
// マテリアル定義とサンプリングヘルパーは ObjectMaterial.hlsli と共有する。

struct GBufferOutput
{
    float4 albedoAO : SV_TARGET0; ///< rgb=アルベド, a=AO
    float4 normalRoughness : SV_TARGET1; ///< rgb=ワールド法線(エンコード済み), a=ラフネス
    float4 emissiveMetallic : SV_TARGET2; ///< rgb=エミッシブ, a=メタリック
    float4 worldPosition : SV_TARGET3; ///< rgb=ワールド座標, a=ピクセルフラグ
    float2 motionVector : SV_TARGET4; ///< モーションベクター（NDC空間の2Dオフセット）
};

GBufferOutput main(VertexShaderOutput input)
{
    GBufferOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float2 uv = transformedUV.xy;
    float4 textureColor = gTexture.Sample(gSampler, uv);
    float finalAlpha = gMaterial.color.a * textureColor.a;

    // アルファカット（ディザリング or 通常テスト）— Forward パスと共通判定
    if (ShouldDiscardByAlpha(finalAlpha, input.position.xy))
    {
        discard;
    }

    float3 albedo = saturate((gMaterial.color * textureColor).rgb);

    // ===== アンリットマテリアル処理 =====
    // enableLighting=0 の場合、DeferredLighting パスに roughness=0 のセンチネル値を書き込む。
    // emissiveMetallic.rgb にアンリットカラーを格納し、DeferredLighting 側で検出して PBR をスキップする。
    if (gMaterial.enableLighting == 0)
    {
        output.albedoAO = float4(0.0f, 0.0f, 0.0f, 1.0f);
        output.normalRoughness = float4(0.5f, 0.5f, 1.0f, 0.0f); // roughness=0 = アンリットセンチネル
        output.emissiveMetallic = float4(albedo, 0.0f); // rgb にアンリットカラーを格納
        output.worldPosition = float4(input.worldPosition, 1.0f);
        output.motionVector = float2(0.0f, 0.0f);
        return output;
    }

    // ===== PBR パス（常にここに到達） =====
    float3 worldNormal = GetNormalFromMap(input.normal, input.tangent, input.bitangent, uv);
    float3 encodedNormal = worldNormal * 0.5f + 0.5f;

    float metallic = 0.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    GetPBRParameters(uv, metallic, roughness, ao);

    metallic = saturate(metallic);
    roughness = saturate(max(roughness, 0.01f));
    ao = saturate(ao);

    // エミッシブ（DeferredLighting が最終カラーに加算する）
    float3 emissive = GetEmissive(uv);

    // worldPosition.a pixelFlag:
    // 0 = 背景（クリア値）
    // 2 = PBR（IBL 無効）
    // 3 = PBR + IBL
    // 4 = Lambert
    // 5 = Half-Lambert
    float pixelFlag;
    if (gMaterial.shadingMode == 1)
        pixelFlag = 3.0f; // PBR_IBL
    else if (gMaterial.shadingMode == 2)
        pixelFlag = 4.0f; // Lambert
    else if (gMaterial.shadingMode == 3)
        pixelFlag = 5.0f; // HalfLambert
    else
        pixelFlag = 2.0f; // PBR (default)

    output.albedoAO = float4(albedo, ao);
    output.normalRoughness = float4(encodedNormal, roughness);
    output.emissiveMetallic = float4(emissive, metallic);
    output.worldPosition = float4(input.worldPosition, pixelFlag);

    // ===== モーションベクター計算 =====
    // NDC空間（-1〜1）での現フレームと前フレームの位置差分を格納する。
    // RTShadow.hlsl がこの値を使って前フレームのピクセル座標を逆算し正確にリプロジェクションする。
    float2 ndcCurrent = input.clipPosCurrent.xy / input.clipPosCurrent.w;
    float2 ndcPrev = input.clipPosPrev.xy / input.clipPosPrev.w;
    output.motionVector = ndcCurrent - ndcPrev;

    return output;
}
