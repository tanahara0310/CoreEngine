// Object3dVertex.hlsli
// Forward / GBuffer パス共通の頂点シェーダーロジック。
// このファイルをインクルードするだけで Object3d.VS.hlsl と同等の
// リソースバインディング・変換処理・main 関数が使用できる。
//
// 使用例（カスタム VS シェーダー）:
//   #include "Object3dVertex.hlsli"
//   // ← これだけで Object3d.VS.hlsl と完全同等の頂点シェーダーになる
//   // 独自の頂点変位などを追加する場合は main を再定義して差し替える

#include "Object3d.hlsli"
#include "VertexAnimation.hlsli"

// インスタンシング描画: 1 つの DrawIndexedInstanced で複数インスタンスを描画する
// Root SRV としてバインドされる StructuredBuffer<TransformationMatrix>
StructuredBuffer<TransformationMatrix> gInstanceData : register(t0, space1);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0; // タンジェント（接線）
    float4 animData : TEXCOORD1; // 頂点アニメーション用の値（VertexData::animData。VertexAnimation.hlsli）
};

/// @brief Forward / GBuffer パス共通の頂点変換処理ヘルパー（main から呼ぶ）
VertexShaderOutput VertexMain(VertexShaderInput input, uint instanceID : SV_InstanceID)
{
    TransformationMatrix mtx = gInstanceData[instanceID];

    // 頂点アニメーション（植物の揺れ・海草・魚の泳ぎ。マテリアルで種類を選んだモデルだけ動く）。
    // 前フレームの位置も同じ式で求めてモーションベクターに使う（揺れが TAA でぼけないように）
    float3 animatedPos = input.position.xyz;
    float3 prevAnimatedPos;
    ApplyVertexAnimation(input.animData, input.normal, mtx.World, mtx.WorldInversTranspose,
        animatedPos, prevAnimatedPos);
    const float4 position = float4(animatedPos, input.position.w);
    const float4 prevPosition = float4(prevAnimatedPos, input.position.w);

    VertexShaderOutput output;
    output.texcoord = input.texcoord;
    output.position = mul(position, mtx.WVP);

    // 法線をワールド空間に変換
    output.normal = normalize(mul(input.normal, (float3x3) mtx.WorldInversTranspose));

    // タンジェントをワールド空間に変換
    output.tangent = normalize(mul(input.tangent, (float3x3) mtx.World));

    // バイタンジェント（従接線）を計算（法線とタンジェントの外積）
    output.bitangent = normalize(cross(output.normal, output.tangent));

    // ワールド座標を計算
    float4 worldPos = mul(position, mtx.World);
    output.worldPosition = worldPos.xyz;

    // ライト空間座標を計算（シャドウマップ用）
    output.lightSpacePos = mul(worldPos, mtx.LightViewProjection);

    // モーションベクター用クリップ空間座標
    output.clipPosCurrent = output.position;
    output.clipPosPrev = mul(prevPosition, mtx.PrevWVP);

    return output;
}
