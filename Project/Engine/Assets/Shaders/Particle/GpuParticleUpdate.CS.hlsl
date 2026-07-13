#include "GpuParticle.hlsli"

// 更新CS: 全スロットを更新し、生存粒子のみ描画用インスタンスバッファへコンパクション書き込みする
// CPU版 ParticleUpdater::UpdateSingleParticle と同じ順序で適用する:
//   Force → 位置積分 → Color → Size → Rotation → Noise
// 今フレーム死亡したスロットはフリーリストへ push し aliveCount を減算する。
// gReset != 0 で全スロット死亡化 + フリーリスト再構築 + カウンタ初期化を行う
// （注意: 現在は未使用。初回初期化はレンダラーの CopyBufferRegion で行う。
//   CBは1面のみでCPUが毎フレーム上書きするため、一度きりのフラグは
//   フレームパイプライン中にGPUが読む前に潰される競合がある。
//   将来「全消去」機能を作る場合は複数フレーム連続でフラグを立てる等の対策が必要）

RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<GpuParticleInstance> gInstancing : register(u1);
RWStructuredBuffer<uint32_t> gCounters : register(u2);   // [0]=freeTop [1]=alive [2]=draw
RWStructuredBuffer<uint32_t> gFreeList : register(u3);   // 死亡スロットのスタック

// SizeModule::ApplyCurve 移植
float32_t ApplySizeCurve(float32_t t)
{
    switch (gSizeCurve)
    {
    case kSizeCurveEaseIn:
        return t * t;
    case kSizeCurveEaseOut:
        return 1.0f - (1.0f - t) * (1.0f - t);
    case kSizeCurveEaseInOut:
        return (t < 0.5f) ? (2.0f * t * t) : (1.0f - 2.0f * (1.0f - t) * (1.0f - t));
    case kSizeCurveConstant:
        return 0.0f;
    case kSizeCurveLinear:
    default:
        return t;
    }
}

// 角度を -π〜π に正規化（RotationModule::NormalizeAngle 相当）
float32_t NormalizeAngle(float32_t angle)
{
    angle = fmod(angle + kGpuParticlePi, 2.0f * kGpuParticlePi);
    if (angle < 0.0f)
    {
        angle += 2.0f * kGpuParticlePi;
    }
    return angle - kGpuParticlePi;
}

[numthreads(kGpuParticleGroupSize, 1, 1)]
void main(uint32_t3 dispatchThreadID : SV_DispatchThreadID)
{
    uint32_t index = dispatchThreadID.x;
    if (index >= gBufferCapacity)
    {
        return;
    }

    if (gReset != 0u)
    {
        // 全スロット死亡化 + フリーリスト再構築（全インデックスを積む）+ カウンタ初期化
        if (index == 0u)
        {
            gCounters[kCounterFreeTop] = gBufferCapacity;
            gCounters[kCounterAlive] = 0u;
            gCounters[kCounterDraw] = 0u;
        }
        gFreeList[index] = index;
        GpuParticle dead = (GpuParticle) 0;
        gParticles[index] = dead;
        return;
    }

    GpuParticle particle = gParticles[index];

    bool wasAlive = (particle.lifeTime > 0.0f) && (particle.currentTime < particle.lifeTime);
    if (!wasAlive)
    {
        // 死亡済みスロット（フリーリスト登録済み）は何もしない
        return;
    }

    bool alive = wasAlive;
    float32_t3 scale = particle.initialScale;
    float32_t4 color = particle.initialColor;

    if (alive)
    {
        particle.currentTime += gDeltaTime;
        float32_t lifeRatio = saturate(particle.currentTime / max(particle.lifeTime, 1e-5f));

        // ===== Force（ForceModule::ApplyForces 移植） =====
        if (gForceEnabled != 0u)
        {
            particle.velocity += gGravity * gGravityModifier * gDeltaTime;
            particle.velocity += gWind * gDeltaTime;
            if (gDrag > 0.0f)
            {
                float32_t dragFactor = max(0.0f, 1.0f - gDrag * gDeltaTime);
                particle.velocity *= dragFactor;
            }
            if (gUseAccelerationField != 0u)
            {
                bool inside = all(particle.position >= gFieldMin) && all(particle.position <= gFieldMax);
                if (inside)
                {
                    particle.velocity += gFieldAcceleration * gDeltaTime;
                }
            }
        }

        // ===== 位置の積分 =====
        particle.position += particle.velocity * gDeltaTime;

        // ===== Color（ColorModule::UpdateColor 移植: initialColor → endColor） =====
        if (gColorEnabled != 0u)
        {
            color = lerp(particle.initialColor, gEndColor, lifeRatio);
        }

        // ===== Size（SizeModule::UpdateSize 移植） =====
        if (gSizeEnabled != 0u)
        {
            float32_t curveValue = ApplySizeCurve(lifeRatio);
            if (gSizeUse3D != 0u)
            {
                scale = clamp(lerp(particle.initialScale, gEndSize3D, curveValue),
                              float32_t3(gSizeMin, gSizeMin, gSizeMin),
                              float32_t3(gSizeMax, gSizeMax, gSizeMax));
            }
            else
            {
                float32_t s = particle.initialScale.x
                    + (gEndSize - particle.initialScale.x) * curveValue;
                s = clamp(s, gSizeMin, gSizeMax);
                scale = float32_t3(s, s, s);
            }
        }

        // ===== Rotation（RotationModule::UpdateRotation 移植） =====
        if (gRotationEnabled != 0u)
        {
            float32_t3 rotSpeed = particle.rotationSpeed;
            if (gRotationOverLifetime != 0u)
            {
                float32_t mul = gRotStartMul + (gRotEndMul - gRotStartMul) * lifeRatio;
                rotSpeed *= mul;
            }
            particle.rotation += rotSpeed * gDeltaTime;
            particle.rotation = float32_t3(
                NormalizeAngle(particle.rotation.x),
                NormalizeAngle(particle.rotation.y),
                NormalizeAngle(particle.rotation.z));
        }

        // ===== Noise（NoiseModule::ApplyNoise 移植） =====
        if (gNoiseEnabled != 0u)
        {
            float32_t dampingFactor = (gNoiseDamping != 0u) ? (1.0f - lifeRatio) : 1.0f;
            float32_t timeOffset = particle.currentTime * gNoiseScrollSpeed;
            float32_t3 noiseCoord = float32_t3(
                (particle.position.x + timeOffset) * gNoiseFrequency,
                (particle.position.y + timeOffset) * gNoiseFrequency,
                (particle.position.z + particle.currentTime) * gNoiseFrequency);

            float32_t3 noiseOffset = float32_t3(
                PerlinNoise3D(noiseCoord),
                PerlinNoise3D(noiseCoord + float32_t3(100.0f, 0.0f, 0.0f)),
                PerlinNoise3D(noiseCoord + float32_t3(0.0f, 100.0f, 0.0f)));
            noiseOffset *= gNoisePositionAmount * gNoiseStrength * dampingFactor;

            particle.position += noiseOffset * gDeltaTime;
        }

        gParticles[index] = particle;
        alive = particle.currentTime < particle.lifeTime;
    }

    if (!alive)
    {
        // 今フレーム死亡: フリーリストへ push + aliveCount 減算
        // lifeTime=0 にして以後のフレームで再pushされないようにする
        particle.lifeTime = 0.0f;
        gParticles[index] = particle;

        uint32_t pushIndex;
        InterlockedAdd(gCounters[kCounterFreeTop], 1u, pushIndex);
        gFreeList[pushIndex] = index;
        InterlockedAdd(gCounters[kCounterAlive], uint32_t(-1));
        return;
    }

    // 生存粒子のみインスタンスバッファへコンパクション書き込み（ExecuteIndirect 用）
    GpuParticleInstance instance = (GpuParticleInstance) 0;
    {
        // CPU版 CalculateWorldMatrix と同じ:
        // スケール×回転（MakeAffine と同一の回転順序）を原点で作り、
        // ビルボード行列を掛けてから平行移動を設定する（行ベクトル規約 -Zpr）
        float32_t cx = cos(particle.rotation.x);
        float32_t sx = sin(particle.rotation.x);
        float32_t cy = cos(particle.rotation.y);
        float32_t sy = sin(particle.rotation.y);
        float32_t cz = cos(particle.rotation.z);
        float32_t sz = sin(particle.rotation.z);

        float32_t4x4 scaleRotate = float32_t4x4(
            scale.x * (cy * cz),                 scale.x * (cy * sz),                 scale.x * (-sy),      0.0f,
            scale.y * (sx * sy * cz - cx * sz),  scale.y * (sx * sy * sz + cx * cz),  scale.y * (sx * cy),  0.0f,
            scale.z * (cx * sy * cz + sx * sz),  scale.z * (cx * sy * sz - sx * cz),  scale.z * (cx * cy),  0.0f,
            0.0f,                                0.0f,                                0.0f,                 1.0f);

        float32_t4x4 world = mul(scaleRotate, gBillboardMatrix);
        world[3] = float32_t4(particle.position, 1.0f);

        instance.World = world;
        instance.WVP = mul(world, gViewProjection);
        instance.Color = color;
    }

    uint32_t writeIndex;
    InterlockedAdd(gCounters[kCounterDraw], 1u, writeIndex);
    gInstancing[writeIndex] = instance;
}
