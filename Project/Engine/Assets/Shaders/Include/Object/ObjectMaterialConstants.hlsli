// ObjectMaterialConstants.hlsli
// モデル描画のマテリアル定数（gMaterial）だけの定義。
// テクスチャ・サンプラーやサンプリング関数は ObjectMaterial.hlsli にある。
// 頂点シェーダー（頂点アニメーション）は定数だけを使うので、こちらだけをインクルードする
// （独自に gSampler などを宣言している頂点シェーダーと名前がぶつからないようにするため）。
// CPU 側 MaterialConstants (Graphics/Material/MaterialConstants.h) とメモリレイアウトを一致させること。
#ifndef OBJECT_MATERIAL_CONSTANTS_HLSLI
#define OBJECT_MATERIAL_CONSTANTS_HLSLI

// ===== マテリアル =====
// glTF 準拠の「ファクター × テクスチャ」乗算方式。
// テクスチャが無いマテリアルには白1x1がバインドされるため、ファクター値がそのまま最終値になる。
struct Material
{
    float4 color; // ベースカラーファクター
    float4x4 uvTransform;

    // ===== PBR Factors =====
    float metallic; // 金属性ファクター（MRテクスチャの B チャネルと乗算）
    float roughness; // 粗さファクター（MRテクスチャの G チャネルと乗算）
    float occlusionStrength; // AOマップ適用強度 (0=無効, 1=フル適用)
    int useNormalMap;

    float3 emissiveFactor; // エミッシブファクター（エミッシブテクスチャと乗算）
    int enableLighting;

    // ===== Alpha =====
    int enableDithering;
    float ditheringScale;
    float alphaCutoff; // discard 判定に使用するアルファしきい値

    // ===== 頂点アニメーション（VertexAnimation.hlsli） =====
    int vertexAnimation; // 0=なし, 1=植物（風）, 2=海草（水の寄せ返し）, 3=魚（泳ぎ）
    float vertexAnimStrength; // 振幅の倍率（1 = モデル作成時の想定）
    float vertexAnimSpeed; // 速さの倍率（1 = 既定の周期）
    float2 vertexAnimPadding;
};

ConstantBuffer<Material> gMaterial : register(b0);

#endif // OBJECT_MATERIAL_CONSTANTS_HLSLI
