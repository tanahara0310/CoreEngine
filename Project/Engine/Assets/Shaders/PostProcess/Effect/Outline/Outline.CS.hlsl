// Outline.CS.hlsl - Depth Based Outline コンピュートシェーダー

Texture2D<float4> gTexture : register(t0);   // カラーテクスチャ
Texture2D<float>  gDepth   : register(t1);   // 深度テクスチャ（NDC空間）
RWTexture2D<float4> gOutput : register(u0);  // 出力テクスチャ

cbuffer OutlineParams : register(b0)
{
    float4 outlineColor;    // アウトラインの色 (RGBA)
    float  depthThreshold;  // 深度エッジ検出の閾値
    float  depthStrength;   // 深度エッジの強度乗数
    float  outlineWidth;    // アウトラインの太さ（サンプリングオフセット）(1.0 ~ 4.0)
    float  nearPlane;       // カメラのニアクリップ距離
    float  farPlane;        // カメラのファークリップ距離
    float3 pad;
};

cbuffer ScreenParams : register(b1)
{
    uint   screenWidth;
    uint   screenHeight;
    float2 screenPad;
};

static const uint kGroupSize = 8;

/// @brief NDC深度値をビュー空間の線形距離に変換する
/// @details 遠距離でも近距離でも同じ感度でエッジ検出できるようにするための正規化
float ToLinearDepth(float ndcDepth)
{
    // DirectX の深度は [0, 1] 範囲の非線形値
    // 線形ビュー空間距離 = near * far / (far - ndcDepth * (far - near))
    return (nearPlane * farPlane) / (farPlane - ndcDepth * (farPlane - nearPlane));
}

/// @brief 指定座標の線形深度をサンプリング（範囲外はクランプ）
float SampleLinearDepth(int2 coord, int2 size)
{
    coord = clamp(coord, int2(0, 0), size - int2(1, 1));
    float rawDepth = gDepth.Load(int3(coord, 0));
    return ToLinearDepth(rawDepth);
}

/// @brief Sobelフィルタで線形深度の勾配を計算し、エッジ強度を返す
float ComputeDepthEdge(int2 center, int2 screenSize, float offset)
{
    int o = (int)ceil(offset);

    // Sobelカーネル (3x3)
    // X方向: [-1, 0, 1; -2, 0, 2; -1, 0, 1]
    // Y方向: [-1,-2,-1;  0, 0, 0;  1, 2, 1]
    float tl = SampleLinearDepth(center + int2(-o, -o), screenSize);
    float tc = SampleLinearDepth(center + int2( 0, -o), screenSize);
    float tr = SampleLinearDepth(center + int2( o, -o), screenSize);
    float ml = SampleLinearDepth(center + int2(-o,  0), screenSize);
    float mr = SampleLinearDepth(center + int2( o,  0), screenSize);
    float bl = SampleLinearDepth(center + int2(-o,  o), screenSize);
    float bc = SampleLinearDepth(center + int2( 0,  o), screenSize);
    float br = SampleLinearDepth(center + int2( o,  o), screenSize);

    float sobelX = (-tl + tr - 2.0f * ml + 2.0f * mr - bl + br);
    float sobelY = (-tl - 2.0f * tc - tr + bl + 2.0f * bc + br);

    return sqrt(sobelX * sobelX + sobelY * sobelY);
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    int2 coord      = int2(dispatchId.xy);
    int2 screenSize = int2(screenWidth, screenHeight);

    // 元のカラーをサンプリング
    float4 color = gTexture.Load(int3(coord, 0));

    // 線形深度のSobelフィルタでエッジを検出
    float edge = ComputeDepthEdge(coord, screenSize, outlineWidth);

    // 閾値処理と強度スケーリング
    edge = saturate((edge - depthThreshold) * depthStrength);

    // アウトラインカラーとブレンド（エッジが強いほどアウトラインカラーになる）
    float4 result = lerp(color, outlineColor * outlineColor.a + color * (1.0f - outlineColor.a), edge);
    result.a = color.a;

    gOutput[coord] = result;
}
