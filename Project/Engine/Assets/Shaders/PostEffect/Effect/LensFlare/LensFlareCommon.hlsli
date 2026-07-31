// LensFlareCommon.hlsli - レンズフレア共通定義
// C++ 側 LensFlare::LensFlareConstants と 112 バイトレイアウトを一致させること。

#ifndef LENS_FLARE_COMMON_HLSLI
#define LENS_FLARE_COMMON_HLSLI
#include "ShaderMath.hlsli"  // TWO_PI
#include "ColorSpace.hlsli"  // Luminance
#include "Hash.hlsli"        // HashSine11

cbuffer LensFlareParams : register(b0)
{
    float gThreshold;          // 輝度抽出の閾値（HDR 空間。太陽は非常に明るいので数倍以上）
    float gSoftKnee;           // ソフトニー [0,1]
    float gGhostDispersal;     // ゴースト間隔（画面中心対称ベクトルのスケール）
    float gGhostIntensity;     // ゴースト強度

    float gHaloWidth;          // ハロー半径（アスペクト補正済み UV 単位）
    float gHaloIntensity;      // ハロー強度
    float gChromaDistortion;   // 色収差強度（テクセル単位の係数）
    float gStarburstIntensity; // スターバースト変調の強さ [0,1]

    float gIntensity;          // 全体強度
    uint gGhostCount;          // ゴースト数
    uint gFlareWidth;          // 1/4 解像度フレアバッファの幅
    uint gFlareHeight;         // 1/4 解像度フレアバッファの高さ

    uint gScreenWidth;         // フル解像度の幅
    uint gScreenHeight;        // フル解像度の高さ
    float gMaxBrightness;      // 抽出輝度のクランプ（ファイアフライ防止）
    float gBlurSigma;          // ガウスブラーの σ（フレアバッファのテクセル単位）

    float4 gTint;              // 全体ティント（rgb 使用）

    float gApertureBladeCount;  // 絞り羽根の枚数（多角形ゴーストの角数。6=正六角形）
    float gApertureRotationRad; // 絞りの回転角 [rad]
    float gGhostRadius;         // ゴースト半径（アスペクト補正済み UV 単位）
    float gGhostPolygonMix;     // 多角形ゴーストの混在比率 [0,1]（0=円形、1=多角形）

    // ===== 太陽限定化（CPU 側で毎フレーム設定） =====
    float2 gSunUv;              // 太陽のスクリーン UV（画角外でも投影値が入る）
    float gSunValid;            // 1=太陽が前方（投影有効）/ 0=無効（フレアを出さない）
    float gSunMaskRadius;       // 輝度抽出を許可する太陽周辺の半径（アスペクト補正済み UV 単位）
};

// アスペクト比補正: UV 空間の差分ベクトルを x が物理的に等方になる空間へ変換する
float2 LensFlareToAspect(float2 uvDelta, float aspect)
{
    uvDelta.x *= aspect;
    return uvDelta;
}

// 正 N 角形の中心からの距離比（1.0 で辺、1.0 未満で内側）。絞り羽根形状の近似。
// d はアスペクト補正済み空間での中心からの相対座標。
float LensFlarePolygonDistanceRatio(float2 d, float radius, float sides, float rotation)
{
    float angle = atan2(d.y, d.x) - rotation;
    float seg = TWO_PI / max(sides, 3.0f);
    float a = fmod(angle, seg);
    if (a < 0.0f) { a += seg; }
    a -= seg * 0.5f;
    float edgeRadius = radius * cos(seg * 0.5f) / max(cos(a), 1e-3f);
    return length(d) / max(edgeRadius, 1e-5f);
}

// 円形の中心からの距離比（ボケた光斑。絞り面共役から外れたゴーストの形）
float LensFlareCircleDistanceRatio(float2 d, float radius)
{
    return length(d) / max(radius, 1e-5f);
}

#endif // LENS_FLARE_COMMON_HLSLI
