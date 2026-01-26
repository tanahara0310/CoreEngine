/// @brief シャドウマップ生成用の共通定義

/// @brief ライト空間での変換行列
/// @note TransformationMatrix構造体と同じレイアウト
struct LightTransform
{
    float4x4 WVP;                    // ワールド・ビュー・プロジェクション行列（通常描画用、シャドウでは使用しない）
    float4x4 World;                  // ワールド行列
    float4x4 WorldInverseTranspose;  // ワールド逆転置行列（通常描画用、シャドウでは使用しない）
    float4x4 LightViewProjection;    // ライトのビュープロジェクション行列
};
