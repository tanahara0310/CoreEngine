struct VertexShaderOutput
{
    float4 position : SV_POSITION; // クリップ空間位置
    float2 texcoord : TEXCOORD0; // テクスチャ座標
    float3 normal : NORMAL0; // 法線（ワールド空間）
    float3 worldPosition : POSITION0; // ワールド空間位置
    float4 lightSpacePos : POSITION1; // ライト空間位置（シャドウマッピング用）
    float3 tangent : TANGENT0; // 接線（ワールド空間）
    float3 bitangent : BINORMAL0; // 従法線（ワールド空間）
    float4 clipPosCurrent : POSITION2; // 現フレームのクリップ空間位置（モーションベクター用）
    float4 clipPosPrev : POSITION3; // 前フレームのクリップ空間位置（モーションベクター用）
#ifdef SURFACE_OBJECT_SPACE
    float3 objectPosition : OBJECTPOSITION0; // 物体の空間の位置
    float3 objectNormal : OBJECTNORMAL0; // 物体の空間の法線
    float3 objectTangent : OBJECTTANGENT0; // 物体の空間の接線
    nointerpolation float4 objectToWorld0 : OBJECTTOWORLD0; // 物体の空間 → ワールドの行列の 1〜3 列目
    nointerpolation float4 objectToWorld1 : OBJECTTOWORLD1;
    nointerpolation float4 objectToWorld2 : OBJECTTOWORLD2;
    nointerpolation float4 worldToObject0 : WORLDTOOBJECT0; // ワールド → 物体の空間の行列の 1〜3 列目
    nointerpolation float4 worldToObject1 : WORLDTOOBJECT1;
    nointerpolation float4 worldToObject2 : WORLDTOOBJECT2;
#endif
#ifdef SURFACE_VERTEX_COLOR
    float4 vertexColor : COLOR0; // 頂点の色（無いモデルは白）
#endif
};

struct TransformationMatrix
{
    float4x4 WVP; // ワールドビュー射影行列
    float4x4 World; // ワールド行列
    float4x4 WorldInversTranspose; // ワールド行列の逆行列の転置（法線変換用）
    float4x4 LightViewProjection; // ライトのビュー射影行列（シャドウマッピング用）
    float4x4 PrevWVP; // 前フレームのワールドビュー射影行列（モーションベクター用）
};
