#include "SpriteMaterial.hlsli"

// ゴールのワープホール。patternMode で 10 通りの描き方を切り替える（加算で重ねる）
struct Params
{
    float4 coreColor;     // 中心の暗い色
    float4 innerColor;    // 内側の光の色
    float4 outerColor;    // 外周の光の色
    float4 effectParam0;  // 描き方ごとの値 0
    float4 effectParam1;  // 描き方ごとの値 1
    float  time;          // 経過時間
    float  pulseStrength; // 脈打ちの強さ
    float  swirlStrength; // 渦の強さ
    float  ringWidth;     // 外周の輪の幅
    float  glowIntensity; // 光の強さ
    float  noiseStrength; // ノイズの揺らぎ
    float  suction;       // 中心へ吸い込む感じ
    float  patternMode;   // 描き方の番号
};
ConstantBuffer<Params> gMaterial : register(b0);

// ─── 共通ユーティリティ ───────────────────────────────────────────────────────

float2 Rot(float2 v, float a)
{
    float s = sin(a), c = cos(a);
    return float2(v.x * c - v.y * s, v.x * s + v.y * c);
}

float Hash21(float2 p)
{
    p = frac(p * float2(127.13f, 311.71f));
    p += dot(p, p + 19.19f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 uv)
{
    float2 i = floor(uv);
    float2 f = frac(uv);
    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(
        lerp(Hash21(i), Hash21(i + float2(1.0f, 0.0f)), u.x),
        lerp(Hash21(i + float2(0.0f, 1.0f)), Hash21(i + float2(1.0f, 1.0f)), u.x),
        u.y);
}

float FBM(float2 uv, float t)
{
    float v = 0.0f;
    float a = 0.5f;
    float2 drift = float2(0.13f, -0.07f);
    for (int k = 0; k < 4; ++k)
    {
        v += a * ValueNoise(uv + t * drift);
        uv = uv * 2.1f;
        drift *= 1.4f;
        a *= 0.5f;
    }
    return v;
}

// ─── A: アニメ渦巻きポータル ───────────────────────────────────────────────
// 同心リング＋数式螺旋ラインで様式化されたポータル
float4 PatternA(float2 uv, float r, float angle, float t)
{
    float pulse = sin(t * 3.0f) * 0.5f + 0.5f;
    float swirlOffset = gMaterial.swirlStrength * 0.5f * (1.0f - r);
    float a = angle + t * 1.4f + swirlOffset;

    // 同心リング（セル的なバンディング）
    float rings = frac(r * 5.0f - t * 0.6f);
    float ringBand = smoothstep(0.0f, 0.28f, rings) * smoothstep(0.55f, 0.28f, rings);

    // 螺旋腕（数式ベース: sin波で作るシャープな縞）
    float spiral = sin(a * 3.0f - r * 14.0f + t * 2.5f) * 0.5f + 0.5f;
    spiral = smoothstep(0.52f, 0.72f, spiral);
    float spiralMask = smoothstep(0.0f, 0.25f, r) * smoothstep(1.0f, 0.55f, r);

    // 中心の輝きコア
    float core = exp(-r * r * 18.0f);

    // 外縁グロウリング
    float rim = exp(-abs(r - 0.88f) * 14.0f);

    float energy = ringBand * 0.55f
                 + spiral * spiralMask * 0.45f
                 + core * 1.2f
                 + rim * 0.6f;
    energy *= (1.0f + pulse * gMaterial.pulseStrength * 0.5f);

    float outerFade = smoothstep(1.0f, 0.7f, r);
    energy *= outerFade;

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb,
                      smoothstep(0.0f, 0.8f, r));
    col += gMaterial.outerColor.rgb * rim * 0.5f;
    col *= energy * gMaterial.glowIntensity;

    return float4(col, saturate(energy));
}

// ─── E: ネオンライトニング ────────────────────────────────────────────────
// 放電ノイズ＋外周アークでスピード感のあるネオン演出
float4 PatternE(float2 uv, float r, float angle, float t)
{
    float boltCount = max(2.0f, gMaterial.effectParam0.x);
    float boltWidth = max(0.01f, gMaterial.effectParam0.y);
    float flickerSpeed = gMaterial.effectParam0.z;
    float arcRadius = gMaterial.effectParam0.w;

    float seed = floor(t * flickerSpeed);
    float cell = floor((angle / 6.28318f) * boltCount);
    float local = frac((angle / 6.28318f) * boltCount) - 0.5f;
    float jitter = (Hash21(float2(cell, seed)) - 0.5f) * gMaterial.noiseStrength * 2.4f;
    float bolt = exp(-abs(local + jitter) / boltWidth) * smoothstep(0.12f, 0.35f, r) * smoothstep(1.0f, 0.28f, r);

    float arcNoise = ValueNoise(float2(angle * 3.0f, seed * 0.17f));
    float arc = exp(-abs(r - arcRadius) * (28.0f / max(gMaterial.ringWidth, 0.02f)))
              * smoothstep(0.35f, 0.85f, arcNoise);
    float core = exp(-r * r * 18.0f);
    float rim = exp(-abs(r - 0.92f) * 18.0f);
    float pulse = sin(t * 8.0f) * 0.5f + 0.5f;

    float energy = bolt * 0.9f + arc * 0.75f + core * 1.1f + rim * 0.45f;
    energy *= 1.0f + pulse * gMaterial.pulseStrength;
    energy *= smoothstep(1.0f, 0.72f, r);

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb, saturate(r * 1.2f));
    col += float3(1.0f, 1.0f, 1.0f) * bolt * 0.35f;
    col *= energy * gMaterial.glowIntensity;
    return float4(col, saturate(energy));
}

// ─── F: レーダー / スキャンライン ─────────────────────────────────────────
// 回転スイープ＋同心グリッドでサイバーなネオンゲート
float4 PatternF(float2 uv, float r, float angle, float t)
{
    float sweepSpeed = gMaterial.effectParam0.x;
    float sweepWidth = max(0.02f, gMaterial.effectParam0.y);
    float gridCount = max(2.0f, gMaterial.effectParam0.z);
    float trailPower = max(0.5f, gMaterial.effectParam0.w);

    float sweepAngle = t * sweepSpeed;
    float d = abs(atan2(sin(angle - sweepAngle), cos(angle - sweepAngle)));
    float sweep = exp(-d / sweepWidth) * smoothstep(0.05f, 0.22f, r) * smoothstep(1.0f, 0.35f, r);
    float trail = pow(saturate(1.0f - d / 3.14159f), trailPower) * 0.22f;

    float rings = frac(r * gridCount - t * 0.35f);
    float ringLine = exp(-abs(rings - 0.5f) * 18.0f) * smoothstep(1.0f, 0.5f, r);
    float radial = exp(-abs(frac((angle / 6.28318f) * gridCount) - 0.5f) * 20.0f) * 0.25f;
    float core = exp(-r * r * 15.0f);
    float rim = exp(-abs(r - 0.9f) * 16.0f);

    float energy = sweep * 1.0f + trail + ringLine * 0.45f + radial * 0.3f + core + rim * 0.45f;
    energy *= smoothstep(1.0f, 0.7f, r);

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb, saturate(r));
    col += gMaterial.outerColor.rgb * sweep * 0.6f;
    col *= energy * gMaterial.glowIntensity;
    return float4(col, saturate(energy));
}

// ─── G: ダイヤモンドグリッド ─────────────────────────────────────────────
// 菱形グリッドが中心に吸い込まれるデジタル系ポータル
float4 PatternG(float2 uv, float r, float angle, float t)
{
    float gridScale = max(2.0f, gMaterial.effectParam0.x);
    float flowSpeed = gMaterial.effectParam0.y;
    float lineWidth = max(0.01f, gMaterial.effectParam0.z);
    float nodeGlow = gMaterial.effectParam0.w;

    float2 p = Rot(uv, 0.785398f + t * 0.12f);
    float suction = t * flowSpeed + gMaterial.suction * (1.0f - r) * 2.0f;
    float2 gp = p * gridScale + suction;
    float2 cell = abs(frac(gp) - 0.5f);
    float gridLine = exp(-min(cell.x, cell.y) / lineWidth);
    float node = exp(-length(cell - 0.5f) * 10.0f) * nodeGlow;
    float rim = exp(-abs(r - 0.9f) * 15.0f);
    float core = exp(-r * r * 14.0f);

    float energy = gridLine * 0.55f + node * 0.25f + rim * 0.55f + core * 1.05f;
    energy *= smoothstep(0.0f, 0.14f, r) * smoothstep(1.0f, 0.68f, r);

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb, smoothstep(0.1f, 0.85f, r));
    col *= energy * gMaterial.glowIntensity;
    return float4(col, saturate(energy));
}

// ─── H: オーロラ波 ────────────────────────────────────────────────────────
// ネオンの波帯がゆっくり干渉する柔らかいポータル
float4 PatternH(float2 uv, float r, float angle, float t)
{
    float waveCount = max(1.0f, gMaterial.effectParam0.x);
    float waveSpeed = gMaterial.effectParam0.y;
    float waveWidth = max(0.05f, gMaterial.effectParam0.z);
    float twist = gMaterial.effectParam0.w;

    float2 p = Rot(uv, sin(t * 0.4f) * 0.35f);
    float w1 = sin((p.y * waveCount + p.x * twist) * 8.0f + t * waveSpeed);
    float w2 = sin((p.y * (waveCount * 0.65f) - p.x * twist) * 10.0f - t * waveSpeed * 0.7f);
    float bands = exp(-abs(w1 + w2) / waveWidth);
    bands *= smoothstep(0.05f, 0.25f, r) * smoothstep(1.0f, 0.55f, r);

    float ripple = exp(-abs(frac(r * 4.5f - t * 0.45f) - 0.5f) * 9.0f) * 0.25f;
    float rim = exp(-abs(r - 0.9f) * 13.0f);
    float core = exp(-r * r * 9.0f);
    float pulse = sin(t * 2.0f) * 0.5f + 0.5f;

    float energy = bands * 0.75f + ripple + rim * 0.5f + core * 0.85f;
    energy *= 1.0f + pulse * gMaterial.pulseStrength * 0.45f;
    energy *= smoothstep(1.0f, 0.68f, r);

    float aurora = sin(angle * 2.0f + t * 0.5f) * 0.5f + 0.5f;
    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb, aurora);
    col *= energy * gMaterial.glowIntensity;
    return float4(col, saturate(energy));
}

// ─── B: 星型ポータル ───────────────────────────────────────────────────────
// 中心から放射する星芒＋回転する六角形パターン
float4 PatternB(float2 uv, float r, float angle, float t)
{
    float pulse = sin(t * 2.8f) * 0.5f + 0.5f;

    // 星芒（放射状の光の筋）: 角度の cos^N で鋭い突起を作る
    float rays = 0.0f;
    for (int i = 0; i < 6; ++i)
    {
        float a = angle - (float(i) / 6.0f) * 6.28318f - t * 0.7f;
        float beam = pow(saturate(cos(a * 3.0f)), 12.0f);
        rays += beam;
    }
    rays *= smoothstep(0.0f, 0.15f, r) * smoothstep(1.0f, 0.3f, r);

    // 六角形リング（角度で6分割したセル境界）
    float hexAngle = frac((angle + t * 0.3f) / 1.0472f) - 0.5f; // 1.0472 = pi/3
    float hexLine = exp(-abs(hexAngle) * 18.0f);
    float hexRing1 = exp(-abs(r - 0.55f) * 16.0f);
    float hexRing2 = exp(-abs(r - 0.82f) * 12.0f);
    float hexPat = hexLine * (hexRing1 + hexRing2 * 0.6f);

    // 中心コア
    float core = exp(-r * r * 20.0f);

    float energy = rays * 0.65f
                 + hexPat * 0.55f
                 + core * 1.4f;
    energy *= (1.0f + pulse * gMaterial.pulseStrength * 0.6f);
    energy *= smoothstep(1.0f, 0.75f, r);

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb,
                      saturate(r * 1.4f));
    col += gMaterial.outerColor.rgb * hexPat * 0.4f;
    col *= energy * gMaterial.glowIntensity;

    return float4(col, saturate(energy));
}

// ─── C: 魔法陣型ポータル ───────────────────────────────────────────────────
// 多角形リング＋幾何学回転パターン
float4 PatternC(float2 uv, float r, float angle, float t)
{
    float pulse = sin(t * 2.2f) * 0.5f + 0.5f;

    // 3重の回転リング（それぞれ異なる速度・細さ）
    float ring1 = exp(-abs(r - 0.35f) * 22.0f);
    float ring2 = exp(-abs(r - 0.62f) * 18.0f);
    float ring3 = exp(-abs(r - 0.88f) * 14.0f);

    // 角度分割（n角形のエッジ感）内側は8角形、外側は12角形
    float segInner = cos(round(angle / (6.28318f / 8.0f)) * (6.28318f / 8.0f) - angle);
    float segOuter = cos(round(angle / (6.28318f / 12.0f)) * (6.28318f / 12.0f) - angle);
    float polyInner = exp(-abs(segInner - 0.97f) * 60.0f);
    float polyOuter = exp(-abs(segOuter - 0.985f) * 80.0f);

    // 角度方向に流れるルーン風のノイズ縞
    float2 runeUv = float2(angle / 6.28318f + t * 0.08f, r * 3.0f);
    float rune = smoothstep(0.38f, 0.78f, ValueNoise(runeUv * float2(8.0f, 1.5f)));
    float runeMask = ring2 * 1.8f + ring1 * 1.2f;

    // 中心コア
    float core = exp(-r * r * 16.0f);

    // 内側多角形は t でゆっくり回転（向きを反転させてアニメ）
    float2 uvRot = Rot(uv, t * 0.4f);
    float aRot = atan2(uvRot.y, uvRot.x);
    float segRot = cos(round(aRot / (6.28318f / 8.0f)) * (6.28318f / 8.0f) - aRot);
    float polyRot = exp(-abs(segRot - 0.97f) * 60.0f);

    float energy = ring1 * polyRot * 0.9f
                 + ring2 * polyInner * 0.7f
                 + ring3 * polyOuter * 0.5f
                 + rune * runeMask * 0.4f
                 + core * 1.3f;
    energy *= (1.0f + pulse * gMaterial.pulseStrength * 0.55f);
    energy *= smoothstep(1.0f, 0.72f, r);

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb,
                      smoothstep(0.0f, 0.75f, r));
    col += gMaterial.outerColor.rgb * (ring3 * polyOuter) * 0.35f;
    col *= energy * gMaterial.glowIntensity;

    return float4(col, saturate(energy));
}

// ─── D: 波紋型ポータル ─────────────────────────────────────────────────────
// 同心円波紋が収束＋楕円リングの穏やかなポータル
float4 PatternD(float2 uv, float r, float angle, float t)
{
    float pulse = sin(t * 2.5f) * 0.5f + 0.5f;

    // 内向きに流れる波紋
    float ripple = frac(r * 6.0f + t * 1.2f);
    float rippleBand = smoothstep(0.0f, 0.22f, ripple) * smoothstep(0.48f, 0.22f, ripple);
    rippleBand *= smoothstep(0.0f, 0.1f, r) * smoothstep(1.0f, 0.5f, r);

    // ゆっくり回転する楕円リング（2本）
    float2 ell1 = Rot(uv, t * 0.25f);
    float2 ell2 = Rot(uv, -t * 0.18f + 1.5708f);
    float ellR1 = length(ell1 * float2(1.0f, 0.6f)) * 2.0f;
    float ellR2 = length(ell2 * float2(0.6f, 1.0f)) * 2.0f;
    float ellRing1 = exp(-abs(ellR1 - 0.72f) * 14.0f);
    float ellRing2 = exp(-abs(ellR2 - 0.78f) * 12.0f);

    // 外縁の輝くリム
    float rim = exp(-abs(r - 0.90f) * 13.0f);

    // 中心コア（柔らかいグロウ）
    float core = exp(-r * r * 10.0f);

    float energy = rippleBand * 0.6f
                 + ellRing1 * 0.55f
                 + ellRing2 * 0.45f
                 + rim * 0.5f
                 + core * 1.0f;
    energy *= (1.0f + pulse * gMaterial.pulseStrength * 0.45f);
    energy *= smoothstep(1.0f, 0.68f, r);

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb,
                      smoothstep(0.0f, 0.85f, r));
    col += gMaterial.outerColor.rgb * rim * 0.35f;
    col *= energy * gMaterial.glowIntensity;

    return float4(col, saturate(energy));
}

// ─── I: ワープホール白円 ──────────────────────────────────────────────────
// 白い円を基調としつつ、中心に近づくほど空間が歪んで見えるワープホール表現
//   effectParam0.x : 渦歪み強度
//   effectParam0.y : 渦回転速度
//   effectParam0.z : 吸い込み波の速度
//   effectParam0.w : 波の本数
float4 PatternI(float2 uv, float r, float angle, float t)
{
    float warpStrength = gMaterial.effectParam0.x;
    float swirlSpeed   = gMaterial.effectParam0.y;
    float flowSpeed    = gMaterial.effectParam0.z;
    float ringCount    = max(1.0f, gMaterial.effectParam0.w);

    // ── UV歪み: 中心に近いほど強く空間を捻じ曲げる ──────────────────────────
    // r が小さいほど warpAngle が大きくなるため中心が大きく歪む
    float warpAngle = warpStrength * (1.0f / (r * 2.5f + 0.4f)) - t * swirlSpeed;
    float2 warpedUV = Rot(uv, warpAngle);
    float  warpedR  = length(warpedUV) * 2.0f;

    // ── 吸い込み波紋（歪んだ UV 上で内側に流れる同心リング） ─────────────────
    float wave      = frac(warpedR * ringCount + t * flowSpeed);
    float waveBand  = smoothstep(0.0f, 0.25f, wave) * smoothstep(0.55f, 0.25f, wave);
    // 円の外縁に近づくほど波を目立たせ、中心では薄くする（奥に吸い込まれる感）
    float waveMask  = smoothstep(0.0f, 0.18f, r) * smoothstep(0.78f, 0.35f, r);
    waveBand *= waveMask;

    // ── 白い塗りつぶし円（内部は波紋で白い面に陰影がつく） ──────────────────
    const float kInnerRadius = 0.72f;
    float innerCircle = smoothstep(kInnerRadius + 0.04f, kInnerRadius - 0.04f, r);
    // 波紋を明暗として白円に焼き込む（暗い筋が中心へ流れていく）
    float surfaceShade = 1.0f - waveBand * 0.55f;
    float3 innerCol = float3(surfaceShade, surfaceShade, surfaceShade) * innerCircle;

    // ── 事象の地平線: 中心をわずかに暗くして穴感を演出 ──────────────────────
    float horizon     = smoothstep(0.18f, 0.04f, r) * 0.7f;
    innerCol         *= (1.0f - horizon);

    // ── 外周リング（脈動） ───────────────────────────────────────────────────
    float pulse      = sin(t * 3.0f) * 0.5f + 0.5f;
    float ringRadius = 0.87f + pulse * gMaterial.pulseStrength * 0.15f;
    float ringW      = max(gMaterial.ringWidth, 0.02f);
    float outerRing  = exp(-abs(r - ringRadius) / ringW);
    outerRing       *= smoothstep(1.0f, 0.80f, r);

    // ── 合成 ─────────────────────────────────────────────────────────────────
    float3 col   = innerCol;
    col         += float3(1.0f, 1.0f, 1.0f) * outerRing * gMaterial.glowIntensity * 0.7f;

    float alpha = saturate(innerCircle + outerRing * 0.5f);

    return float4(col, alpha);
}

// ─── J: ワープホール歪み ──────────────────────────────────────────────────
// UV座標を中心への引力で渦巻き歪みさせ、空間が吸い込まれるワープホール表現
//   effectParam0.x : 歪み強度
//   effectParam0.y : 渦回転速度
//   effectParam0.z : 降着円盤半径
//   effectParam0.w : シマー速度
float4 PatternJ(float2 uv, float r, float angle, float t)
{
    float warpStrength = gMaterial.effectParam0.x;
    float swirlSpeed   = gMaterial.effectParam0.y;
    float diskRadius   = gMaterial.effectParam0.z;
    float shimmerSpeed = gMaterial.effectParam0.w;

    // ── UV歪み: 中心への引力で角度をシフトして空間を捻じ曲げる ───────────────
    float warpAngle = warpStrength * (1.0f / (r + 0.3f)) - t * swirlSpeed;
    float2 warpedUV = Rot(uv, warpAngle);
    float warpedR   = length(warpedUV) * 2.0f;
    float warpedA   = atan2(warpedUV.y, warpedUV.x);

    // ── 降着円盤（明るいリング） ─────────────────────────────────────────────
    float diskRing = exp(-abs(warpedR - diskRadius) * 10.0f / max(gMaterial.ringWidth, 0.02f));
    // 角度方向の輝度変調（ホットスポット）
    float shimmer = sin(warpedA * 3.0f - t * shimmerSpeed) * 0.3f + 0.7f;
    diskRing *= shimmer;

    // ── 内側に向かう流れ縞（物質が吸い込まれていく表現） ─────────────────────
    float flow = frac(warpedR * 5.0f + t * 1.5f);
    float flowBand = smoothstep(0.0f, 0.2f, flow) * smoothstep(0.45f, 0.2f, flow);
    flowBand *= smoothstep(0.0f, 0.1f, r) * smoothstep(diskRadius + 0.15f, 0.05f, r);

    // ── 事象の地平線（中心を黒く塗りつぶす） ───────────────────────────────
    float horizon     = smoothstep(0.22f, 0.10f, r);
    float horizonMask = 1.0f - horizon;

    // ── 外縁グロウリング ─────────────────────────────────────────────────────
    float rim = exp(-abs(r - 0.88f) * 12.0f);

    // ── 脈動と合成 ──────────────────────────────────────────────────────────
    float pulse  = sin(t * 3.0f) * 0.5f + 0.5f;
    float energy = (diskRing * 1.2f + flowBand * 0.5f + rim * 0.5f) * horizonMask;
    energy *= (1.0f + pulse * gMaterial.pulseStrength * 0.4f);
    energy *= smoothstep(1.0f, 0.7f, r);

    float3 col = lerp(gMaterial.innerColor.rgb, gMaterial.outerColor.rgb,
                      smoothstep(0.0f, 0.9f, warpedR));
    col += gMaterial.outerColor.rgb * rim * 0.4f;
    col *= energy * gMaterial.glowIntensity;
    // 事象の地平線の中心を完全に黒く
    col *= horizonMask;

    return float4(col, saturate(energy));
}

float4 main(SpriteVertexOutput input) : SV_TARGET0
{
    float2 uv = input.texcoord - 0.5f;
    float r = length(uv) * 2.0f;
    float angle = atan2(uv.y, uv.x);
    float t = gMaterial.time;

    if (r > 1.0f)
    {
        discard;
    }

    int mode = (int) round(gMaterial.patternMode);
    if (mode == 0)
    {
        return PatternA(uv, r, angle, t);
    }
    else if (mode == 1)
    {
        return PatternB(uv, r, angle, t);
    }
    else if (mode == 2)
    {
        return PatternC(uv, r, angle, t);
    }
    else if (mode == 3)
    {
        return PatternD(uv, r, angle, t);
    }
    else if (mode == 4)
    {
        return PatternE(uv, r, angle, t);
    }
    else if (mode == 5)
    {
        return PatternF(uv, r, angle, t);
    }
    else if (mode == 6)
    {
        return PatternG(uv, r, angle, t);
    }
    else if (mode == 7)
    {
        return PatternH(uv, r, angle, t);
    }
    else if (mode == 8)
    {
        return PatternI(uv, r, angle, t);
    }
    return PatternJ(uv, r, angle, t);
}
