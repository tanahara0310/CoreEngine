#include "GpuParticle.hlsli"

// 放出CS: フリーリストから死亡スロットを確保して gEmitCount 個の粒子を書き込む
// CPU版 ParticleEmitter::CreateParticle と同じ順序・数式で初期化する:
//   Main(サイズ/回転/色/寿命) → Shape(位置) → Velocity(方向)×Main(速さ) → Rotation(回転速度)

RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<uint32_t> gCounters : register(u1);   // [0]=freeTop [1]=alive [2]=draw
RWStructuredBuffer<uint32_t> gFreeList : register(u2);   // 死亡スロットのスタック

// ============================================================
// Shape: 放出位置の生成（ShapeModule::GeneratePosition 移植）
// ============================================================

// 平面指定つきの円周座標
float32_t3 CirclePlanePosition(float32_t cosA, float32_t sinA, float32_t r, uint32_t plane)
{
    if (plane == kCirclePlaneXY)
    {
        return float32_t3(cosA * r, sinA * r, 0.0f);
    }
    if (plane == kCirclePlaneYZ)
    {
        return float32_t3(0.0f, cosA * r, sinA * r);
    }
    return float32_t3(cosA * r, 0.0f, sinA * r); // XZ（既定）
}

float32_t3 GenerateShapeOffset(inout uint32_t state)
{
    switch (gShapeType)
    {
    case kShapeBox:
        return float32_t3(
            RandSigned(state) * gShapeScale.x,
            RandSigned(state) * gShapeScale.y,
            RandSigned(state) * gShapeScale.z);

    case kShapeSphere:
    {
        float32_t3 dir = float32_t3(RandSigned(state), RandSigned(state), RandSigned(state));
        float32_t len = length(dir);
        if (len > 0.0f) { dir /= len; }
        float32_t r = (gShapeEmitFromSurface != 0u)
            ? gShapeRadius
            : pow(Rand01(state), 1.0f / 3.0f) * gShapeRadius;
        return dir * r;
    }

    case kShapeCircle:
    {
        float32_t angle = RandRange(state, 0.0f, 2.0f * kGpuParticlePi);
        float32_t r = sqrt(Rand01(state)) * gShapeRadius;
        return CirclePlanePosition(cos(angle), sin(angle), r, gShapeCirclePlane);
    }

    case kShapeCone:
    {
        float32_t circleAngle = RandRange(state, 0.0f, 2.0f * kGpuParticlePi);
        float32_t height = Rand01(state) * gShapeHeight;
        float32_t coneRadius = height * tan(gShapeAngleRad);
        float32_t r = sqrt(Rand01(state)) * coneRadius;
        return float32_t3(cos(circleAngle) * r, height, sin(circleAngle) * r);
    }

    case kShapeHemisphere:
    {
        float32_t3 dir = float32_t3(RandSigned(state), abs(RandSigned(state)), RandSigned(state));
        float32_t len = length(dir);
        if (len > 0.0f) { dir /= len; }
        float32_t r = (gShapeEmitFromSurface != 0u)
            ? gShapeRadius
            : pow(Rand01(state), 1.0f / 3.0f) * gShapeRadius;
        return dir * r;
    }

    case kShapeRing:
    {
        float32_t angle = RandRange(state, 0.0f, 2.0f * kGpuParticlePi);
        float32_t r = gShapeInnerRadius + sqrt(Rand01(state)) * (gShapeRadius - gShapeInnerRadius);
        return CirclePlanePosition(cos(angle), sin(angle), r, gShapeCirclePlane);
    }

    case kShapeLine:
    {
        float32_t3 dir = gShapeEmissionDirection;
        float32_t len = length(dir);
        dir = (len > 0.0f) ? dir / len : float32_t3(0.0f, 1.0f, 0.0f);
        float32_t linePos = RandSigned(state) * gShapeScale.x;
        return dir * linePos;
    }

    case kShapeCylinder:
    {
        float32_t angle = RandRange(state, 0.0f, 2.0f * kGpuParticlePi);
        float32_t height = RandSigned(state) * (gShapeHeight * 0.5f);
        float32_t r;
        if (gShapeEmitFromSurface != 0u)
        {
            if (Rand01(state) < 0.8f)
            {
                r = gShapeRadius; // 側面
            }
            else
            {
                r = sqrt(Rand01(state)) * gShapeRadius; // 上下面
                height = (height > 0.0f) ? (gShapeHeight * 0.5f) : (-gShapeHeight * 0.5f);
            }
        }
        else
        {
            r = sqrt(Rand01(state)) * gShapeRadius;
        }
        return float32_t3(cos(angle) * r, height, sin(angle) * r);
    }

    case kShapeEdge:
    {
        float32_t3 dir = float32_t3(RandSigned(state), RandSigned(state), RandSigned(state));
        float32_t len = length(dir);
        if (len > 0.0f) { dir /= len; }
        return dir * gShapeRadius;
    }

    case kShapeCircleHalf:
    {
        bool top = Rand01(state) < 0.5f;
        float32_t minAngle, maxAngle;
        if (top)
        {
            minAngle = 5.0f * kGpuParticlePi / 6.0f;
            maxAngle = 7.0f * kGpuParticlePi / 6.0f;
        }
        else if (Rand01(state) < 0.5f)
        {
            minAngle = 11.0f * kGpuParticlePi / 6.0f;
            maxAngle = 2.0f * kGpuParticlePi;
        }
        else
        {
            minAngle = 0.0f;
            maxAngle = kGpuParticlePi / 6.0f;
        }
        float32_t angle = RandRange(state, minAngle, maxAngle);
        return CirclePlanePosition(cos(angle), sin(angle), gShapeRadius, gShapeCirclePlane);
    }

    case kShapePoint:
    default:
        return float32_t3(0.0f, 0.0f, 0.0f);
    }
}

// ============================================================
// Velocity: 初期速度方向（VelocityModule::ApplyInitialVelocity 移植）
// ============================================================

float32_t3 GenerateVelocityDirection(inout uint32_t state)
{
    if (gVelocityEnabled == 0u)
    {
        return float32_t3(0.0f, 1.0f, 0.0f); // モジュール無効時は上向き
    }

    float32_t3 dir = gVelocityDirection;
    if (gVelocityUseRandomDir != 0u)
    {
        // ランダム単位方向 + 揺らぎ幅
        dir = float32_t3(RandSigned(state), RandSigned(state), RandSigned(state));
        float32_t len = length(dir);
        if (len > 0.0f) { dir /= len; }
        dir += float32_t3(
            RandRange(state, -gVelocityRandomRange.x, gVelocityRandomRange.x),
            RandRange(state, -gVelocityRandomRange.y, gVelocityRandomRange.y),
            RandRange(state, -gVelocityRandomRange.z, gVelocityRandomRange.z));
    }

    float32_t len2 = length(dir);
    return (len2 > 0.0f) ? dir / len2 : float32_t3(0.0f, 1.0f, 0.0f);
}

// ============================================================
// Rotation: 初期回転速度（RotationModule::ApplyInitialRotation 移植）
// ============================================================

float32_t RotationDirectionFactor(inout uint32_t state)
{
    switch (gRotationDirectionMode)
    {
    case kRotDirClockwise:
        return 1.0f;
    case kRotDirCounterClockwise:
        return -1.0f;
    case kRotDirRandom:
    case kRotDirBoth:
    default:
        return (Rand01(state) < 0.5f) ? -1.0f : 1.0f;
    }
}

float32_t3 GenerateRotationSpeed(inout uint32_t state)
{
    if (gRotationEnabled == 0u)
    {
        return float32_t3(0.0f, 0.0f, 0.0f);
    }

    if (gRotationUse2D != 0u)
    {
        float32_t speed = ApplyRotRandomness(state, gRotation2DSpeed, gRotation2DSpeedRandomness);
        speed *= RotationDirectionFactor(state);
        return float32_t3(0.0f, 0.0f, speed);
    }

    return float32_t3(
        ApplyRotRandomness(state, gRotationSpeed3D.x, gRotationSpeedRandomness3D.x),
        ApplyRotRandomness(state, gRotationSpeed3D.y, gRotationSpeedRandomness3D.y),
        ApplyRotRandomness(state, gRotationSpeed3D.z, gRotationSpeedRandomness3D.z));
}

// ============================================================
// メイン
// ============================================================

[numthreads(kGpuParticleGroupSize, 1, 1)]
void main(uint32_t3 dispatchThreadID : SV_DispatchThreadID)
{
    uint32_t threadIndex = dispatchThreadID.x;
    if (threadIndex >= gEmitCount)
    {
        return;
    }

    // ===== 実効容量の厳密な制限（CPU版の maxParticles と同等） =====
    uint32_t prevAlive;
    InterlockedAdd(gCounters[kCounterAlive], 1u, prevAlive);
    if (prevAlive >= gEffectiveCapacity)
    {
        InterlockedAdd(gCounters[kCounterAlive], uint32_t(-1)); // 取り消し
        return;
    }

    // ===== フリーリストから死亡スロットをpop =====
    uint32_t prevTop;
    InterlockedAdd(gCounters[kCounterFreeTop], uint32_t(-1), prevTop);
    if (prevTop == 0u || prevTop > gBufferCapacity)
    {
        // 空（またはアンダーフローの巻き戻り値）: 書き戻してスキップ
        InterlockedAdd(gCounters[kCounterFreeTop], 1u);
        InterlockedAdd(gCounters[kCounterAlive], uint32_t(-1));
        return;
    }
    uint32_t slot = gFreeList[prevTop - 1u];

    uint32_t state = WangHash(slot * 747796405u + gFrameSeed * 2891336453u + threadIndex + 1u);

    GpuParticle particle;

    // ===== Main: 初期サイズ・回転・色・寿命（ランダム性つき） =====
    particle.initialScale = float32_t3(
        ApplyMainRandomness(state, gStartSize.x, gStartSizeRandomness),
        ApplyMainRandomness(state, gStartSize.y, gStartSizeRandomness),
        ApplyMainRandomness(state, gStartSize.z, gStartSizeRandomness));
    particle.rotation = float32_t3(
        ApplyMainRandomness(state, gStartRotation.x, gStartRotationRandomness),
        ApplyMainRandomness(state, gStartRotation.y, gStartRotationRandomness),
        ApplyMainRandomness(state, gStartRotation.z, gStartRotationRandomness));
    particle.initialColor = float32_t4(
        ApplyMainRandomness(state, gStartColor.x, gStartColorRandomness),
        ApplyMainRandomness(state, gStartColor.y, gStartColorRandomness),
        ApplyMainRandomness(state, gStartColor.z, gStartColorRandomness),
        ApplyMainRandomness(state, gStartColor.w, gStartColorRandomness));
    particle.lifeTime = ApplyMainRandomness(state, gStartLifetime, gStartLifetimeRandomness);
    particle.currentTime = 0.0f;

    // ===== Shape: 放出位置 =====
    float32_t3 position = gEmitterPosition;
    if (gShapeEnabled != 0u)
    {
        position += GenerateShapeOffset(state);
        if (gShapeRandomPositionRange > 0.0f)
        {
            position += float32_t3(
                RandRange(state, -gShapeRandomPositionRange, gShapeRandomPositionRange),
                RandRange(state, -gShapeRandomPositionRange, gShapeRandomPositionRange),
                RandRange(state, -gShapeRandomPositionRange, gShapeRandomPositionRange));
        }
    }
    particle.position = position;

    // ===== Velocity: 方向 × Main: 速さ =====
    float32_t3 direction = GenerateVelocityDirection(state);
    float32_t speed = ApplyMainRandomness(state, gStartSpeed, gStartSpeedRandomness);
    particle.velocity = direction * speed;

    // ===== Rotation: 回転速度 =====
    particle.rotationSpeed = GenerateRotationSpeed(state);

    particle.pad0 = 0.0f;
    particle.pad1 = 0.0f;
    particle.pad2 = 0.0f;

    gParticles[slot] = particle;
}
