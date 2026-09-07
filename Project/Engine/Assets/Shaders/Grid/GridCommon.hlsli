#ifndef GRID_COMMON_HLSLI
#define GRID_COMMON_HLSLI

// エディタ用の無限グリッド（床）で VS / PS が共有する定義。
// C++ 側の GridRenderer::GridConstants と 1 対 1 で対応するので、
// 片方だけ変えると中身がずれる。必ず両方を直すこと。
//
// 行列が「カメラ相対」なのが重要な点。絶対ワールド座標のままレイと床平面の交点を
// 求めると、カメラが原点から離れたときに float32 の有効桁が足りず、交点とその画面
// スペース微分がピクセル単位でばらつく（＝格子も軸ラインも一面のノイズになる）。
// カメラを原点に置いた座標系で解くことで、手前ほど小さい値になり精度が保たれる。
cbuffer GridParams : register(b0)
{
    matrix viewProjection;      // カメラ相対ワールド → クリップ
    matrix invViewProjection;   // クリップ → カメラ相対ワールド

    float3 cameraPosition; float planeY;          // カメラのワールド位置 / 床平面の高さ [m]
    float3 gridColor;      float baseSpacing;     // 格子の色 / 最細の格子間隔 [m]
    float3 xAxisColor;     float brightness;      // X 軸の色 / 全体の濃さ
    float3 zAxisColor;     float minPixelsPerCell; // Z 軸の色 / 1 マスの最小ピクセル数

    float maxLevel;        // baseSpacing から何段まで粗くしてよいか（超えたら消す）
    float lineWidthPixels; // 線の太さ [px]
    float axisAlpha;       // 軸ラインの不透明度
    float depthBias;       // 床メッシュとの Z ファイティング回避用の NDC 深度バイアス
};

struct GridVSOutput
{
    float4 position  : SV_POSITION;
    float3 nearPoint : TEXCOORD0;   // カメラレイのニア平面側（カメラ相対ワールド）
    float3 farPoint  : TEXCOORD1;   // 同じくファー平面側（カメラ相対ワールド）
};

#endif // GRID_COMMON_HLSLI
