// ============================================================
// DXR 水面屈折シェーダー
// フラットな水面平面を近似し、屈折レイの最初のヒット位置を SceneColor に再投影して
// 水面屈折用のカラー出力を生成する。
// ============================================================

RWTexture2D<float4> gRefractionOutput : register(u0);
RaytracingAccelerationStructure gScene : register(t0);
Texture2D<float4> gWorldPosition : register(t1);
Texture2D<float4> gSceneColor : register(t2);

struct WaveParam
{
    float2 direction;
    float amplitude;
    float wavelength;
    float speed;
    float steepness;
    float phaseOffset;
    float padding;
};

cbuffer WaterSurfaceData : register(b1)
{
    float gSurfaceWaterHeight;
    uint gSurfaceActiveWaveCount;
    float gSurfaceTime;
    float gSurfacePadding;
    WaveParam gSurfaceWaves[16];
};

cbuffer WaterRefractionConstants : register(b0)
{
    float4x4 gViewProjection;
    float3 gCameraPosition;
    float gWaterHeight;
    float gSurfaceBias;
    float gMaxRayDistance;
    float gRefractionEta;
    float gAbsorptionCoeff;
    float gScreenWidth;
    float gScreenHeight;
    float gMaxRefractionOffsetPixels;
    float gPadding;
};

struct RefractionPayload
{
    float hitT;
    float hitFlag;
};

static const float kRTReasonNoWorldPosition = 1.0f / 255.0f;
static const float kRTReasonNearZeroSceneDistance = 2.0f / 255.0f;
static const float kRTReasonParallelToWater = 3.0f / 255.0f;
static const float kRTReasonInvalidPlaneIntersection = 4.0f / 255.0f;
static const float kRTReasonInvalidRefractionVector = 5.0f / 255.0f;
static const float kRTReasonTraceMiss = 6.0f / 255.0f;
static const float kRTReasonInvalidClip = 7.0f / 255.0f;
static const float kRTReasonSuccess = 8.0f / 255.0f;

float4 MakeFallbackOutput(float3 fallbackColor, float reasonCode)
{
    return float4(fallbackColor, reasonCode);
}

float3 EvaluateWaterOffset(float2 worldXZ)
{
    float3 totalOffset = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < 16; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        WaveParam wave = gSurfaceWaves[waveIndex];
        float k = 2.0f * 3.14159265f / max(wave.wavelength, 1.0e-4f);
        float omega = wave.speed * k;
        float kA = max(k * wave.amplitude, 1.0e-4f);
        float safeSteepness = min(wave.steepness, 0.95f / kA);
        float phase = k * dot(wave.direction, worldXZ) + omega * gSurfaceTime + wave.phaseOffset;
        float sinP = sin(phase);
        float cosP = cos(phase);

        totalOffset.x += safeSteepness * wave.amplitude * wave.direction.x * cosP;
        totalOffset.y += wave.amplitude * sinP;
        totalOffset.z += safeSteepness * wave.amplitude * wave.direction.y * cosP;
    }

    return totalOffset;
}

float3 EvaluateWaterNormal(float2 worldXZ)
{
    float3 dPdX = float3(1.0f, 0.0f, 0.0f);
    float3 dPdZ = float3(0.0f, 0.0f, 1.0f);

    [unroll]
    for (uint waveIndex = 0; waveIndex < 16; ++waveIndex)
    {
        if (waveIndex >= gSurfaceActiveWaveCount)
        {
            break;
        }

        WaveParam wave = gSurfaceWaves[waveIndex];
        float k = 2.0f * 3.14159265f / max(wave.wavelength, 1.0e-4f);
        float omega = wave.speed * k;
        float kA = max(k * wave.amplitude, 1.0e-4f);
        float safeSteepness = min(wave.steepness, 0.95f / kA);
        float phase = k * dot(wave.direction, worldXZ) + omega * gSurfaceTime + wave.phaseOffset;
        float sinP = sin(phase);
        float cosP = cos(phase);
        float common = safeSteepness * wave.amplitude * k * sinP;
        float heightSlope = wave.amplitude * k * cosP;

        dPdX += float3(
            -common * wave.direction.x * wave.direction.x,
             heightSlope * wave.direction.x,
            -common * wave.direction.x * wave.direction.y);

        dPdZ += float3(
            -common * wave.direction.x * wave.direction.y,
             heightSlope * wave.direction.y,
            -common * wave.direction.y * wave.direction.y);
    }

    float3 normal = normalize(cross(dPdZ, dPdX));
    return normal.y < 0.0f ? -normal : normal;
}

[shader("raygeneration")]
void RTWaterRefractionRayGen()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    float4 fallbackSample = gSceneColor.Load(int3(launchIndex, 0));
    float4 worldPosSample = gWorldPosition.Load(int3(launchIndex, 0));

    if (worldPosSample.a < 0.5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonNoWorldPosition);
        return;
    }

    float3 cameraToScene = worldPosSample.xyz - gCameraPosition;
    float sceneDistance = length(cameraToScene);
    if (sceneDistance <= 1.0e-4f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonNearZeroSceneDistance);
        return;
    }

    float3 primaryDir = cameraToScene / sceneDistance;
    float denom = primaryDir.y;
    if (abs(denom) <= 1.0e-5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonParallelToWater);
        return;
    }

    float tSurface = (gSurfaceWaterHeight - gCameraPosition.y) / denom;
    if (tSurface <= 0.0f || tSurface >= sceneDistance)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidPlaneIntersection);
        return;
    }

    float3 waterPos = gCameraPosition + primaryDir * tSurface;
    float safeDenom = denom;
    if (abs(safeDenom) < 1.0e-4f)
    {
        safeDenom = (safeDenom < 0.0f) ? -1.0e-4f : 1.0e-4f;
    }
    [unroll]
    for (int iteration = 0; iteration < 3; ++iteration)
    {
        float3 waveOffset = EvaluateWaterOffset(waterPos.xz);
        float surfaceY = gSurfaceWaterHeight + waveOffset.y;
        float deltaY = waterPos.y - surfaceY;
        waterPos -= primaryDir * (deltaY / safeDenom);
    }

    float3 waterNormal = EvaluateWaterNormal(waterPos.xz);
    float3 refractedDir = refract(primaryDir, waterNormal, gRefractionEta);
    if (dot(refractedDir, refractedDir) <= 1.0e-6f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidRefractionVector);
        return;
    }

    RayDesc ray;
    ray.Origin = waterPos + waterNormal * gSurfaceBias;
    ray.Direction = normalize(refractedDir);
    ray.TMin = 0.001f;
    ray.TMax = gMaxRayDistance;

    RefractionPayload payload;
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;

    TraceRay(
        gScene,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xFF,
        0,
        1,
        0,
        ray,
        payload);

    if (payload.hitFlag < 0.5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonTraceMiss);
        return;
    }

    float3 hitWorldPos = ray.Origin + ray.Direction * payload.hitT;
    float4 clip = mul(float4(hitWorldPos, 1.0f), gViewProjection);
    if (clip.w <= 1.0e-5f)
    {
        gRefractionOutput[launchIndex] = MakeFallbackOutput(fallbackSample.rgb, kRTReasonInvalidClip);
        return;
    }

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    float2 screenUV = (float2(launchIndex) + 0.5f) / float2(gScreenWidth, gScreenHeight);
    float2 refractedUV = screenUV;
    if (all(uv >= 0.0f.xx) && all(uv <= 1.0f.xx))
    {
        float2 pixelSize = 1.0f / float2(gScreenWidth, gScreenHeight);
        float2 maxOffset = pixelSize * max(gMaxRefractionOffsetPixels, 0.0f);
        float2 uvOffset = clamp(uv - screenUV, -maxOffset, maxOffset);
        refractedUV = saturate(screenUV + uvOffset);
    }

    uint2 sampleCoord = uint2(refractedUV * float2(gScreenWidth, gScreenHeight));
    sampleCoord = min(sampleCoord, uint2(gScreenWidth - 1.0f, gScreenHeight - 1.0f));
    float3 refractedColor = gSceneColor.Load(int3(sampleCoord, 0)).rgb;

    gRefractionOutput[launchIndex] = float4(refractedColor, kRTReasonSuccess);
}

[shader("miss")]
void RTWaterRefractionMiss(inout RefractionPayload payload)
{
    payload.hitT = 0.0f;
    payload.hitFlag = 0.0f;
}

[shader("closesthit")]
void RTWaterRefractionClosestHit(
    inout RefractionPayload payload,
    in BuiltInTriangleIntersectionAttributes attr)
{
    payload.hitT = RayTCurrent();
    payload.hitFlag = 1.0f;
}
