#include "ShaderMath.hlsli" // PI / TWO_PI

// GPUパーティクル共通定義（Emit / Update CSで共有）
// Phase 2: CPU版モジュール（Shape/Velocity/Force/Color/Size/Rotation/Noise）の
// パラメータを GpuParticleParams に詰め、CPU版と同じ数式で適用する。
// C++側 GpuParticleParams / GpuParticleData と完全に一致させること。

// パーティクルの状態（particleBuffer_ の要素、96byte）
struct GpuParticle
{
    float32_t3 position;
    float32_t lifeTime;
    float32_t3 velocity;
    float32_t currentTime;
    float32_t3 rotation;        // 現在の回転（ラジアン、オイラー角）
    float32_t pad0;
    float32_t3 rotationSpeed;   // 回転速度（ラジアン/秒）
    float32_t pad1;
    float32_t3 initialScale;    // 初期サイズ（SizeModule の補間起点）
    float32_t pad2;
    float32_t4 initialColor;    // 初期色（ColorModule のグラデーション起点）
};

// 描画用インスタンスデータ（Particle.hlsli の ParticleForGPU と同一レイアウト）
struct GpuParticleInstance
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4 Color;
};

// 形状タイプ（C++ ShapeModule::ShapeType と一致）
static const uint32_t kShapePoint = 0;
static const uint32_t kShapeBox = 1;
static const uint32_t kShapeSphere = 2;
static const uint32_t kShapeCircle = 3;
static const uint32_t kShapeCone = 4;
static const uint32_t kShapeHemisphere = 5;
static const uint32_t kShapeRing = 6;
static const uint32_t kShapeLine = 7;
static const uint32_t kShapeCylinder = 8;
static const uint32_t kShapeEdge = 9;
static const uint32_t kShapeCircleHalf = 10;

// 円の平面（C++ ShapeModule::CirclePlane と一致）
static const uint32_t kCirclePlaneXZ = 0;
static const uint32_t kCirclePlaneXY = 1;
static const uint32_t kCirclePlaneYZ = 2;

// サイズカーブ（C++ SizeModule::SizeData::SizeCurve と一致）
static const uint32_t kSizeCurveLinear = 0;
static const uint32_t kSizeCurveEaseIn = 1;
static const uint32_t kSizeCurveEaseOut = 2;
static const uint32_t kSizeCurveEaseInOut = 3;
static const uint32_t kSizeCurveConstant = 4;

// 回転方向（C++ RotationModule::RotationData::RotationDirection と一致）
static const uint32_t kRotDirClockwise = 0;
static const uint32_t kRotDirCounterClockwise = 1;
static const uint32_t kRotDirRandom = 2;
static const uint32_t kRotDirBoth = 3;

// カウンタバッファ（uint×4）のインデックス
static const uint32_t kCounterFreeTop = 0;  // フリーリストのスタック深さ（永続）
static const uint32_t kCounterAlive = 1;    // 生存数（永続。Emit+1 / 死亡-1）
static const uint32_t kCounterDraw = 2;     // 今フレームの描画インスタンス数（毎フレーム0クリア）

// C++側 GpuParticleParams と一致させること（576バイト）
cbuffer GpuParticleParams : register(b0)
{
    float32_t4x4 gBillboardMatrix;      // ビルボード回転（平行移動なし）
    float32_t4x4 gViewProjection;

    // ===== フレーム情報 =====
    float32_t3 gEmitterPosition;
    float32_t gDeltaTime;
    uint32_t gEmitCount;                // 今フレームの放出数
    uint32_t gBufferCapacity;           // パーティクルバッファの物理容量（Update の走査範囲・フリーリスト容量）
    uint32_t gEffectiveCapacity;        // 生存数の上限（MainModule.maxParticles を反映）
    uint32_t gFrameSeed;
    uint32_t gReset;                    // 1=全スロット初期化フレーム
    float32_t gGravityModifier;         // MainModule.gravityModifier
    uint32_t gPad0;
    uint32_t gPad1;

    // ===== Main（初期値と各ランダム性） =====
    float32_t4 gStartColor;
    float32_t3 gStartSize;
    float32_t gStartSizeRandomness;
    float32_t3 gStartRotation;          // ラジアン（CPU側で度→ラジアン変換済み）
    float32_t gStartRotationRandomness;
    float32_t gStartLifetime;
    float32_t gStartLifetimeRandomness;
    float32_t gStartSpeed;
    float32_t gStartSpeedRandomness;
    float32_t gStartColorRandomness;
    uint32_t gShapeEnabled;
    uint32_t gVelocityEnabled;
    uint32_t gVelocityUseRandomDir;

    // ===== Shape =====
    float32_t3 gShapeScale;
    uint32_t gShapeType;
    float32_t gShapeRadius;
    float32_t gShapeInnerRadius;
    float32_t gShapeHeight;
    float32_t gShapeAngleRad;           // ラジアン（CPU側で度→ラジアン変換済み）
    float32_t3 gShapeEmissionDirection;
    float32_t gShapeRandomPositionRange;
    uint32_t gShapeCirclePlane;
    uint32_t gShapeEmitFromSurface;
    uint32_t gPad2;
    uint32_t gPad3;

    // ===== Velocity =====
    float32_t3 gVelocityDirection;      // 固定方向（useRandomDir=0 のとき使用、非正規化可）
    uint32_t gPad4;
    float32_t3 gVelocityRandomRange;    // ランダム方向の揺らぎ幅
    uint32_t gPad5;

    // ===== Force =====
    float32_t3 gGravity;
    float32_t gDrag;
    float32_t3 gWind;
    uint32_t gForceEnabled;
    float32_t3 gFieldAcceleration;
    uint32_t gUseAccelerationField;
    float32_t3 gFieldMin;
    uint32_t gPad6;
    float32_t3 gFieldMax;
    uint32_t gPad7;

    // ===== Color =====
    float32_t4 gEndColor;
    uint32_t gColorEnabled;             // ColorModule enabled && useGradient
    uint32_t gSizeEnabled;              // SizeModule enabled && sizeOverLifetime
    uint32_t gSizeUse3D;
    uint32_t gSizeCurve;

    // ===== Size =====
    float32_t3 gEndSize3D;
    float32_t gEndSize;
    float32_t gSizeMin;
    float32_t gSizeMax;
    uint32_t gRotationEnabled;
    uint32_t gRotationUse2D;

    // ===== Rotation =====
    float32_t3 gRotationSpeed3D;
    float32_t gRotation2DSpeed;
    float32_t3 gRotationSpeedRandomness3D;
    float32_t gRotation2DSpeedRandomness;
    uint32_t gRotationDirectionMode;
    uint32_t gRotationOverLifetime;
    float32_t gRotStartMul;
    float32_t gRotEndMul;

    // ===== Noise =====
    float32_t3 gNoisePositionAmount;
    uint32_t gNoiseEnabled;
    float32_t gNoiseStrength;
    float32_t gNoiseFrequency;
    float32_t gNoiseScrollSpeed;
    uint32_t gNoiseDamping;
};

static const uint32_t kGpuParticleGroupSize = 64;

// ============================================================
// 乱数
// ============================================================

// Wang Hash（シード撹拌用）
uint32_t WangHash(uint32_t seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

// xorshift による [0,1) 乱数（state を更新しながら使う）
float32_t Rand01(inout uint32_t state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return float32_t(state) * (1.0f / 4294967296.0f);
}

// [-1,1] 乱数（RandomGenerator::GetFloatSigned 相当）
float32_t RandSigned(inout uint32_t state)
{
    return Rand01(state) * 2.0f - 1.0f;
}

// [min,max] 乱数
float32_t RandRange(inout uint32_t state, float32_t minV, float32_t maxV)
{
    return minV + Rand01(state) * (maxV - minV);
}

// MainModule::ApplyRandomness 相当（base ± base*randomness、負値クランプ）
float32_t ApplyMainRandomness(inout uint32_t state, float32_t base, float32_t randomness)
{
    if (randomness <= 0.0f)
    {
        return base;
    }
    float32_t range = base * randomness;
    float32_t result = base + RandRange(state, -range, range);
    return max(result, 0.0f);
}

// RotationModule::ApplyRandomness 相当（負値クランプなし・係数方式でなく加算方式）
float32_t ApplyRotRandomness(inout uint32_t state, float32_t base, float32_t randomness)
{
    if (randomness <= 0.0f)
    {
        return base;
    }
    return base + RandRange(state, -randomness, randomness);
}

// ============================================================
// パーリンノイズ（ハッシュ勾配方式。CPU版は順列テーブルだが見た目同等）
// ============================================================

float32_t PerlinFade(float32_t t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Ken Perlin の grad 関数（ハッシュから12方向の勾配を選択）
float32_t PerlinGrad(uint32_t hash, float32_t x, float32_t y, float32_t z)
{
    uint32_t h = hash & 15u;
    float32_t u = (h < 8u) ? x : y;
    float32_t v = (h < 4u) ? y : ((h == 12u || h == 14u) ? x : z);
    return (((h & 1u) == 0u) ? u : -u) + (((h & 2u) == 0u) ? v : -v);
}

uint32_t PerlinLatticeHash(int32_t3 cell)
{
    // 格子点座標のハッシュ（順列テーブルの代替）
    uint32_t h = uint32_t(cell.x) * 73856093u ^ uint32_t(cell.y) * 19349663u ^ uint32_t(cell.z) * 83492791u;
    return WangHash(h);
}

float32_t PerlinNoise3D(float32_t3 p)
{
    int32_t3 pi = int32_t3(floor(p));
    float32_t3 pf = p - floor(p);
    float32_t3 f = float32_t3(PerlinFade(pf.x), PerlinFade(pf.y), PerlinFade(pf.z));

    float32_t n000 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(0, 0, 0)), pf.x,        pf.y,        pf.z);
    float32_t n100 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(1, 0, 0)), pf.x - 1.0f, pf.y,        pf.z);
    float32_t n010 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(0, 1, 0)), pf.x,        pf.y - 1.0f, pf.z);
    float32_t n110 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(1, 1, 0)), pf.x - 1.0f, pf.y - 1.0f, pf.z);
    float32_t n001 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(0, 0, 1)), pf.x,        pf.y,        pf.z - 1.0f);
    float32_t n101 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(1, 0, 1)), pf.x - 1.0f, pf.y,        pf.z - 1.0f);
    float32_t n011 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(0, 1, 1)), pf.x,        pf.y - 1.0f, pf.z - 1.0f);
    float32_t n111 = PerlinGrad(PerlinLatticeHash(pi + int32_t3(1, 1, 1)), pf.x - 1.0f, pf.y - 1.0f, pf.z - 1.0f);

    float32_t nx00 = lerp(n000, n100, f.x);
    float32_t nx10 = lerp(n010, n110, f.x);
    float32_t nx01 = lerp(n001, n101, f.x);
    float32_t nx11 = lerp(n011, n111, f.x);
    float32_t nxy0 = lerp(nx00, nx10, f.y);
    float32_t nxy1 = lerp(nx01, nx11, f.y);
    return lerp(nxy0, nxy1, f.z);
}
