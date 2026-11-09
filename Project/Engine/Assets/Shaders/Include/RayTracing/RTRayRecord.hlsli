#ifndef RT_RAY_RECORD_HLSLI
#define RT_RAY_RECORD_HLSLI

// ============================================================
// 画素デバッガの記録
// RT_RAY_RECORD_HEAP_INDEX を定義した版（デバッグ窓が作る記録版）だけが、選んだ画素で撃ったレイと
// RT_DEBUG_PRINT の値を記録先へ書く。定義しない版では呼び出しごと消える。
// RTTrace.hlsli が取り込む（RTTraceHit を使う）。並びは C++ 側 RTDebugSystem の kDataRayRecord* と同じ。
//
// 効果のシェーダは入口で 1 回 RTRecordBeginPixel(トレース解像度の座標, フル解像度との比) を呼ぶ。
// 値を見たい所には RT_DEBUG_PRINT(値) を書く（float・int・uint・bool の 1〜4 成分）。
// ============================================================

#ifdef RT_RAY_RECORD_HEAP_INDEX

static const uint kRTRecordBase = 1024u;            // 記録の先頭（デバッグのバッファの中の位置）
static const uint kRTRecordHeaderTargetX = 0u;      // 選んだ画素（フル解像度）
static const uint kRTRecordHeaderTag = 8u;          // 今のディスパッチの番号
static const uint kRTRecordHeaderRayCount = 12u;    // 記録しようとしたレイの数
static const uint kRTRecordHeaderPrintCount = 16u;  // 記録しようとした値の数
static const uint kRTRecordRays = 64u;              // レイの記録の先頭（記録の先頭から）
static const uint kRTRecordRayStride = 80u;
static const uint kRTRecordMaxRays = 256u;
static const uint kRTRecordPrints = kRTRecordRays + kRTRecordRayStride * kRTRecordMaxRays;
static const uint kRTRecordPrintStride = 32u;
static const uint kRTRecordMaxPrints = 64u;

// 値の種類（C++ 側 RTDebugPrintType と同じ）
static const uint kRTRecordValueFloat = 0u;
static const uint kRTRecordValueUint = 1u;
static const uint kRTRecordValueInt = 2u;
static const uint kRTRecordValueBool = 3u;

static bool gRTRecordActive = false;  // この画素を記録するか
static uint gRTRecordTag = 0u;        // ディスパッチの番号
static uint gRTRecordCoord = 0u;      // トレース解像度の座標（x | y << 16）

/// @brief 画素の記録を始める（効果のシェーダの入口で 1 回呼ぶ）
/// @param traceCoord トレース解像度の座標
/// @param traceScale フル解像度との比（1 = フル解像度）
void RTRecordBeginPixel(uint2 traceCoord, uint traceScale)
{
    RWByteAddressBuffer records = ResourceDescriptorHeap[RT_RAY_RECORD_HEAP_INDEX];
    const uint2 target = records.Load2(kRTRecordBase + kRTRecordHeaderTargetX);
    gRTRecordActive = all(target / max(traceScale, 1u) == traceCoord);
    gRTRecordTag = records.Load(kRTRecordBase + kRTRecordHeaderTag);
    gRTRecordCoord = (traceCoord.x & 0xFFFFu) | (traceCoord.y << 16);
}

/// @brief 撃ったレイ 1 本を記録する
/// @param rayFlags   撃ったときのレイのフラグ
/// @param alphaTests アルファを調べた候補の数
/// @param cutouts    そのうち抜けた（通り抜けた）数
void RTRecordRay(RayDesc ray, uint rayFlags, uint instanceMask, RTTraceHit hit, uint alphaTests, uint cutouts)
{
    if (!gRTRecordActive)
    {
        return;
    }
    RWByteAddressBuffer records = ResourceDescriptorHeap[RT_RAY_RECORD_HEAP_INDEX];
    uint index;
    records.InterlockedAdd(kRTRecordBase + kRTRecordHeaderRayCount, 1u, index);
    if (index >= kRTRecordMaxRays)
    {
        return;
    }
    const uint offset = kRTRecordBase + kRTRecordRays + index * kRTRecordRayStride;
    const uint flags = (hit.hit ? 1u : 0u) | (hit.frontFace ? 2u : 0u);
    records.Store4(offset, uint4(gRTRecordTag, flags, rayFlags, instanceMask));
    records.Store4(offset + 16u, uint4(asuint(ray.Origin), asuint(ray.TMin)));
    records.Store4(offset + 32u, uint4(asuint(ray.Direction), asuint(ray.TMax)));
    records.Store4(offset + 48u, uint4(asuint(hit.hitT), hit.primitive.instanceIndex,
        hit.primitive.geometryIndex, hit.primitive.primitiveIndex));
    records.Store4(offset + 64u, uint4(alphaTests, cutouts, gRTRecordCoord, 0u));
}

/// @brief 値 1 つを記録する（RT_DEBUG_PRINT から呼ぶ）
/// @param lineNumber 書いた行
/// @param type       値の種類（kRTRecordValue*）
/// @param components 成分の数（1〜4）
void RTRecordPrintBits(uint lineNumber, uint type, uint components, uint4 bits)
{
    if (!gRTRecordActive)
    {
        return;
    }
    RWByteAddressBuffer records = ResourceDescriptorHeap[RT_RAY_RECORD_HEAP_INDEX];
    uint index;
    records.InterlockedAdd(kRTRecordBase + kRTRecordHeaderPrintCount, 1u, index);
    if (index >= kRTRecordMaxPrints)
    {
        return;
    }
    const uint offset = kRTRecordBase + kRTRecordPrints + index * kRTRecordPrintStride;
    records.Store4(offset, uint4(gRTRecordTag, lineNumber, type | (components << 8), gRTRecordCoord));
    records.Store4(offset + 16u, bits);
}

void RTRecordPrint(uint lineNumber, float value) { RTRecordPrintBits(lineNumber, kRTRecordValueFloat, 1u, uint4(asuint(value), 0u, 0u, 0u)); }
void RTRecordPrint(uint lineNumber, float2 value) { RTRecordPrintBits(lineNumber, kRTRecordValueFloat, 2u, uint4(asuint(value), 0u, 0u)); }
void RTRecordPrint(uint lineNumber, float3 value) { RTRecordPrintBits(lineNumber, kRTRecordValueFloat, 3u, uint4(asuint(value), 0u)); }
void RTRecordPrint(uint lineNumber, float4 value) { RTRecordPrintBits(lineNumber, kRTRecordValueFloat, 4u, asuint(value)); }
void RTRecordPrint(uint lineNumber, uint value) { RTRecordPrintBits(lineNumber, kRTRecordValueUint, 1u, uint4(value, 0u, 0u, 0u)); }
void RTRecordPrint(uint lineNumber, uint2 value) { RTRecordPrintBits(lineNumber, kRTRecordValueUint, 2u, uint4(value, 0u, 0u)); }
void RTRecordPrint(uint lineNumber, uint3 value) { RTRecordPrintBits(lineNumber, kRTRecordValueUint, 3u, uint4(value, 0u)); }
void RTRecordPrint(uint lineNumber, uint4 value) { RTRecordPrintBits(lineNumber, kRTRecordValueUint, 4u, value); }
void RTRecordPrint(uint lineNumber, int value) { RTRecordPrintBits(lineNumber, kRTRecordValueInt, 1u, uint4(asuint(value), 0u, 0u, 0u)); }
void RTRecordPrint(uint lineNumber, int2 value) { RTRecordPrintBits(lineNumber, kRTRecordValueInt, 2u, uint4(asuint(value), 0u, 0u)); }
void RTRecordPrint(uint lineNumber, int3 value) { RTRecordPrintBits(lineNumber, kRTRecordValueInt, 3u, uint4(asuint(value), 0u)); }
void RTRecordPrint(uint lineNumber, int4 value) { RTRecordPrintBits(lineNumber, kRTRecordValueInt, 4u, asuint(value)); }
void RTRecordPrint(uint lineNumber, bool value) { RTRecordPrintBits(lineNumber, kRTRecordValueBool, 1u, uint4(value ? 1u : 0u, 0u, 0u, 0u)); }

/// @brief 選んだ画素でこの行の値を記録する
#define RT_DEBUG_PRINT(value) RTRecordPrint(__LINE__, value)

#else

#define RTRecordBeginPixel(traceCoord, traceScale)
#define RT_DEBUG_PRINT(value)

#endif // RT_RAY_RECORD_HEAP_INDEX

#endif // RT_RAY_RECORD_HLSLI
