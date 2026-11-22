// ============================================================
// 水面のピクセルシェーダーが読む資源と入力
// ------------------------------------------------------------
// Water.PS.hlsl と、その工程ごとの hlsli（法線・水柱・体積色・泡・反射・波頭の透過・デバッグ表示）が
// それぞれ自分で include する。資源は WaterShaderResourceBinder が差す。
// ============================================================
#ifndef WATER_SURFACE_RESOURCES_INCLUDED
#define WATER_SURFACE_RESOURCES_INCLUDED

// カメラ・材質・平行光源・サンプラ・フォワード PBR・フォグ
#include "Object3dForward.hlsli"
// 大気散乱の空気遠近感（b6 / t22 / t23）
#include "AtmosphereApply.hlsli"
// FFT のカスケード定数・回転写像・波群エンベロープ
#include "../Common/FFTOceanCascade.hlsli"
#include "../Common/WaterVisibleSlope.hlsli"
// RT 屈折のアルファの書き方（RTWaterRefraction.hlsl と共有）
#include "../Common/WaterRefractionEncoding.hlsli"
#include "ColorSpace.hlsli" // Luminance

// ===== RT 反射（RTWaterReflectionPass の出力）=====
// 水面の画素ごとの反射色。alpha >= 0.5 が成功・< 0.5 がミス（空環境マップへ落とす）。
Texture2D<float4> gReflectionTexture : register(t14);
SamplerState gLinearClamp : register(s2);

// ===== シーンの深度（水柱の厚さを求める）=====
Texture2D<float> gSceneDepth : register(t15);

// ===== シーンの色（水面越しに見える背景）=====
Texture2D<float4> gSceneColor : register(t16);

// ===== RT 屈折（RTWaterRefractionPass の出力）=====
Texture2D<float4> gRTWaterRefractionColor : register(t17);

// ===== FFT の法線（FFTOceanFinalize.CS の出力。カスケードは配列のスライス）=====
Texture2DArray<float4> gFFTOceanNormal : register(t19);

// ===== FFT のヤコビアン（FFTOceanFinalize.CS の出力）=====
// 各スライス = (Jxx, Jzz, Jxy, detJ_cascade)。合成 detJ は ComputeFFTCombinedDetJ で求める。
Texture2DArray<float4> gFFTOceanJacobian : register(t20);

// ===== FFT の蓄積泡（FFTOceanFoamAccumulate.CS の出力。前のフレームで書き終わった側。
//       x = 白波の水面の泡の被覆率・y = 水の中の気泡の量）=====
Texture2DArray<float2> gFFTOceanFoam : register(t21);

// ===== 空の放射照度の SH9 係数（SkyIrradianceSH.CS の出力）=====
// Σ c_i・Y_i(N) が「法線 N に対する空からの放射照度 / π」（DeferredLighting と同じ）。
StructuredBuffer<float4> gWaterSkyIrradianceSH : register(t24);

// ===== プリフィルタ済みの空のキューブマップ（空＋雲）=====
// rgb は空（SkyAtmosphere.PS）と同じ輝度。α は雲の透過率。
TextureCube<float4> gSkyEnvironmentMap : register(t25);

// ===== 水面の日向率（RTWaterReflectionPass の 2 枚目の出力）=====
// 0 = 水より上の遮蔽物の影 / 1 = 日向。gSunVisibilityEnabled が 1 のフレームだけ読む。
Texture2D<float> gWaterSunVisibility : register(t26);

// ===== 海底の高さ（RTWaterSeabedPass の出力。カメラの周りの範囲のワールド Y）=====
// gSeabedEnabled が 1 のフレームだけ読む。
Texture2D<float> gWaterSeabedHeight : register(t27);

// ===== 岸の泡（WaterShoreFoamPass の出力。(被覆率, 寄せ・引きのずれ x, z, 0)）=====
// gShoreFoamEnabled が 1 のフレームだけ読む。
Texture2D<float4> gWaterShoreFoam : register(t28);

// ===== 水の波紋（WaterRipplePropagate.CS の出力。段ごとの (高さ, ∂h/∂x, ∂h/∂z, 0)）=====
// gRippleEnabled が 1 のフレームだけ読む。
Texture2DArray<float4> gWaterRipple : register(t30);

// ===== 物がかき回した所の泡（WaterObjectFoamAdvance.CS の出力）=====
// 泡は (水面の泡 F, 水の中の気泡 B, 模様を動かす速さ vx, vz [m/s])、流れは (ux, uz) [m/s]。
// gObjectFoamEnabled が 1 のフレームだけ読む。
Texture2D<float4> gWaterObjectFoam : register(t31);
Texture2D<float2> gWaterObjectFoamFlow : register(t32);

// ===== 泡の模様の画像（Tools/Water/generate_foam_textures.py が作る。ミップも画像の中）=====
// R = 泡になる順（どのミップも [0,1] に一様）・G = 泡の明るさ。網目は 1 枚 4m 四方、気泡の粒は 1 枚 0.5m 四方。
// gFoamPatternMode が 1 のフレームだけ読む。
Texture2D<float2> gWaterFoamNet : register(t33);
Texture2D<float2> gWaterFoamGrain : register(t34);

struct WaterPSInput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    // 変位前の参照格子座標 x0（描画点は x = x0 + D(x0)）。
    // FFT の変位・法線・ヤコビアン・蓄積泡はすべて x0 の関数なので、カスケードはこの値で引く。
    float2 baseWorldXZ : TEXCOORD1;
    // 静止水面からの波の高さ [m]（WaterSubsurface.hlsli）
    float waveHeight : TEXCOORD2;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float4 clipPosCurrent : POSITION2;
    float4 clipPosPrev : POSITION3;
    // 1 = 岸の細かい格子の画素（WaterShoreMesh.hlsli）
    nointerpolation float shoreMeshVertex : TEXCOORD3;
};

// フレーム定数（b5。頂点シェーダーと共有）
#include "../Common/WaterFrameConstants.hlsli"
#include "../Common/WaterShoreMesh.hlsli"

/// @brief 画素の RT 屈折の結果（RT 屈折を読まない水面は、どの画素もヒットなしのアルファ 0）
float4 SampleRTWaterRefraction(uint2 pixelCoord)
{
    if (gRTRefractionEnabled == 0)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return gRTWaterRefractionColor.Load(int3(pixelCoord, 0));
}

#endif // WATER_SURFACE_RESOURCES_INCLUDED
