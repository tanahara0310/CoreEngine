#include "Object3d.hlsli"
#include "../Include/Object/ObjectMaterial.hlsli"

// ハイブリッドレンダリングの GBuffer 書き込み専用シェーダー。
// 不透明モデル（BlendMode::None）の全てのメッシュがこのパスを通る。
// ライティングは DeferredLighting.PS.hlsl で行う。
// マテリアル定義とサンプリングヘルパーは ObjectMaterial.hlsli と共有する。

struct GBufferOutput
{
    float4 albedoAO : SV_TARGET0; ///< rgb=アルベド, a=AO
    float4 normalRoughness : SV_TARGET1; ///< rgb=ワールド法線(エンコード済み), a=ラフネス（符号=IBL有効/無効、0=アンリット）
    float4 emissiveMetallic : SV_TARGET2; ///< rgb=エミッシブ, a=メタリック
    float2 motionVector : SV_TARGET3; ///< モーションベクター（NDC空間の2Dオフセット）
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

    // WorldPosition ターゲット削除に伴い、IBL 有効/無効フラグを roughness の符号ビットへ
    // 無劣化で埋め込む（roughness は上で 0.01 未満に丸め済みなので 0.0 との衝突は無い。
    // 0.0 自体はアンリットセンチネルとして別途予約済み）。
    // DeferredLighting.PS.hlsl はワールド座標を深度から復元し、背景判定は深度のクリア値で行う。
    float encodedRoughness = (gMaterial.iblIntensity > 0.0f) ? roughness : -roughness;

    output.albedoAO = float4(albedo, ao);
    output.normalRoughness = float4(encodedNormal, encodedRoughness);
    output.emissiveMetallic = float4(emissive, metallic);

    // ===== モーションベクター計算 =====
    // NDC空間（-1〜1）での現フレームと前フレームの位置差分を格納する。
    // RTShadow.hlsl がこの値を使って前フレームのピクセル座標を逆算し正確にリプロジェクションする。
    float2 ndcCurrent = input.clipPosCurrent.xy / input.clipPosCurrent.w;
    float2 ndcPrev = input.clipPosPrev.xy / input.clipPosPrev.w;
    output.motionVector = ndcCurrent - ndcPrev;

    return output;
}
