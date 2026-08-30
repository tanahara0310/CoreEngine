/// @file CloudMarchBindings.hlsli
/// @brief 雲をレイマーチする CS が共通で使うリソース宣言
/// @details CloudRayMarch.CS / CloudCubemapCapture.CS が同じ register 割り当てで使う。
///          各 CS 固有のリソース（深度・出力先）はそれぞれの CS 側で宣言する。
/// @note 大気 LUT 専用のサンプラーは端をクランプする。既定の WRAP で LUT を引くと、
///       太陽が地平線を跨いだ瞬間に Transmittance LUT の U が反対端へ巻き込み、
///       雲が真っ白に光ってから一気に真っ黒へ落ちる。
///       （RootSignature 側で "gLUTSampler" は LinearClamp 固定）

#ifndef CLOUD_MARCH_BINDINGS_HLSLI
#define CLOUD_MARCH_BINDINGS_HLSLI

#include "CloudCommon.hlsli"
#include "../../Atmosphere/Common/AtmosphereCommon.hlsli"

ConstantBuffer<CloudConstants> gCloud : register(b0);
ConstantBuffer<AtmosphereConstants> gAtmosphere : register(b1);

Texture3D<float4> gBaseShapeNoise : register(t0);
Texture3D<float4> gDetailNoise : register(t1);
Texture2D<float4> gWeatherMap : register(t2);
Texture2D<float4> gTransmittanceLUT : register(t4);
Texture2D<float4> gSkyViewLUT : register(t5);
Texture2D<float4> gCloudPaintMap : register(t8);

SamplerState gSamplerLinearWrap : register(s0);
SamplerState gLUTSampler : register(s1);

#endif // CLOUD_MARCH_BINDINGS_HLSLI
