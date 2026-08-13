// MotionBlurCommon.hlsli - モーションブラー3パス（TileMax/NeighborMax/Gather）の共有定義
//
// 速度の単位系はここで一元化する。
//   G-Buffer の MotionVector は NDC 差分（current - prev、ジッタ込み）。
//   ブラー計算は全てピクセル単位で行うため、必ず NdcVelocityToPixels を通すこと。
//   シャッター倍率とクランプも全経路で同一関数を使わないと、
//   タイルの最大速度と画素の実速度の基準がずれてブラー境界が破綻する。

/// @brief これ未満の速度[px]はブラー対象にしない
/// @details TAA のサブピクセルジッタ（1px未満）を拾って静止画面が常時ぼけるのを防ぐ。
///          ジッタ差分を厳密に引く方式ではなくカットオフで吸収する（TAA無効時も同じ式で済む）
static const float kMinVelocityPixels = 0.5f;

/// @brief NDC 差分の速度をピクセル差分へ変換する
/// @details NDC は画面全幅で 2.0、Y は画面座標と逆向き
float2 NdcVelocityToPixels(float2 ndcDelta, float2 screenSize)
{
    return ndcDelta * float2(0.5f, -0.5f) * screenSize;
}

/// @brief シャッター開角度によるスケールと最大距離クランプ
/// @param velocityPixels 1フレームの移動量[px]
/// @param shutterFraction シャッター開角度/360（180度=0.5）
/// @param maxBlurPixels ブラーの最大到達距離[px]
float2 ScaleAndClampVelocity(float2 velocityPixels, float shutterFraction, float maxBlurPixels)
{
    float2 v = velocityPixels * shutterFraction;
    float len = length(v);
    if (len > maxBlurPixels)
    {
        v *= maxBlurPixels / len;
    }
    return v;
}
