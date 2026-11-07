#ifndef RT_INSTANCE_MASK_HLSLI
#define RT_INSTANCE_MASK_HLSLI

// ============================================================
// TLAS のインスタンスの印（TraceRay の InstanceInclusionMask に渡す）
// C++ 側 Graphics/RayTracing/RTInstanceMask.h の RTInstanceMask と 1:1
// ============================================================

// 影を落とす物（光が届くかを調べるレイが当たる）
static const uint kRTInstanceMaskShadowCaster = 0x01;
// 反射・屈折に映る物（当たった面の色を求めるレイが当たる）
static const uint kRTInstanceMaskReflected = 0x02;
// 止まっている形の決まった不透明な物
static const uint kRTInstanceMaskStaticSolid = 0x04;
// 動いている形の決まった不透明な物
static const uint kRTInstanceMaskMovingSolid = 0x08;

#endif // RT_INSTANCE_MASK_HLSLI
