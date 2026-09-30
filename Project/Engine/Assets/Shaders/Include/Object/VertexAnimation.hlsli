// VertexAnimation.hlsli
// 頂点アニメーション（植物の揺れ・海草の寄せ返し・魚の泳ぎ）。Object3dVertex.hlsli の VertexMain から使う。
//
// 頂点ごとの値: VertexData::animData（glTF の TEXCOORD_1.xy, TEXCOORD_2.xy）
//   植物・海草: (R 葉先の震えの振幅 m, G 葉ごとの位相 0..1, B 葉・枝のしなりの振幅 m, A 株全体の曲げの振幅 m)
//   魚        : (t 吻端 0 → 尾の先 1, 体の横揺れの振幅 m, 胸びれの振幅 m, 胸びれの左右 +1 左 / -1 右)
//   持たないモデルは 0 なので、種類を指定しても動かない。
// マテリアルごと: gMaterial.vertexAnimation（種類）/ vertexAnimStrength（振幅の倍率）/ vertexAnimSpeed（速さの倍率）
// フレーム共通  : gVertexAnim（時間・風。ルート定数。C++ 側 VertexAnimationParams）
//
// 振幅は「強さ 1」での値（モデル作成時の想定はやや強い海風）。オブジェクト空間（Y-up、原点 = 根元）で
// 動かすので、拡大した個体は揺れも同じ比率で大きくなる。法線は変えない（揺れは小さいので見た目の差は小さい）。
// 数式は Tools/OkinawaBeach/README.md の HLSL 例、Blender 上の確認用ジオメトリノード（okinawa/motion.py）と同じ。
#ifndef VERTEX_ANIMATION_HLSLI
#define VERTEX_ANIMATION_HLSLI

#include "ObjectMaterialConstants.hlsli"

// 種類（C++ 側 VertexAnimationType と一致させる）
static const int kVertexAnimNone = 0;
static const int kVertexAnimPlant = 1;
static const int kVertexAnimSeagrass = 2;
static const int kVertexAnimFish = 3;

// time は CPU 側でこの秒数ごとに折り返す。すべての周波数をこの逆数の整数倍に丸めるので、
// 折り返しの瞬間も動きが途切れない（C++ 側 VertexAnimationParams::kWrapSeconds と一致させる）
static const float kVertexAnimWrapSeconds = 240.0f;
static const float kVertexAnimTau = 6.28318531f;

/// @brief フレーム共通の定数（ルート定数。C++ 側 VertexAnimationParams と一致させる）
struct VertexAnimationParams
{
    float time; ///< 秒（kVertexAnimWrapSeconds で折り返す）
    float prevTime; ///< 前フレームの time（モーションベクター用）
    float windStrength; ///< 風の強さ（1 = モデル作成時の想定。海の FFT の風速から決まる）
    float waterStrength; ///< 水の寄せ返しの強さ（海草）
    float2 windDir; ///< 風下の向き（ワールド XZ、正規化済み）
    float2 waterDir; ///< 寄せ返しの向き（ワールド XZ、正規化済み）
};
// b0 = gMaterial, b1〜b3, b7 = フォワードのシーン定数, b4〜b6 = 水面・大気が使うので空いている b9 を使う
ConstantBuffer<VertexAnimationParams> gVertexAnim : register(b9);

/// @brief 周波数 [Hz] を折り返し周期の逆数の整数倍に丸める
float VertexAnimQuantizeHz(float hz)
{
    return round(hz * kVertexAnimWrapSeconds) / kVertexAnimWrapSeconds;
}

/// @brief 植物・海草の揺れ（Crysis の植物アニメーション（GPU Gems 3 16 章）を簡略化）
/// @param pos      オブジェクト空間の位置（Y-up、原点 = 根元）
/// @param nrm      オブジェクト空間の法線（両面化した裏の面は逆向き）
/// @param anim     (R 震え, G 位相, B しなり, A 全体の曲げ)
/// @param dirOS    風下（寄せ返し）の向き（オブジェクト空間、単位ベクトル）
/// @param strength 強さ（風の強さ × マテリアルの倍率）
/// @param t        時間 [s]
/// @param objPhase 個体ごとの位相 [rad]
/// @param baseHz   基本の周波数 [Hz]（各揺れはこの整数倍）
/// @param branchUp しなりの上下成分の割合（木 0.8 / 海草 0）
/// @param bias     風下へ押される量（木 0.6 / 海草 0.25）
/// @param freq     (しなり 1, しなり 2, 震え) の周波数の倍数
float3 VertexAnimWind(float3 pos, float3 nrm, float4 anim, float3 dirOS, float strength, float t,
    float objPhase, float baseHz, float branchUp, float bias, float3 freq)
{
    const float w0 = kVertexAnimTau * VertexAnimQuantizeHz(baseHz);
    const float3 up = float3(0.0f, 1.0f, 0.0f);
    const float3 side = cross(up, dirOS);

    // 1) 株全体の曲げ。根元からの距離を保って幹が伸びないようにする
    const float gust = strength * (0.55f + 0.30f * sin(w0 * t + objPhase)
        + 0.15f * sin(3.0f * w0 * t + 1.3f * objPhase));
    const float sway = strength * 0.25f * sin(2.0f * w0 * t + 2.0f * objPhase);
    float3 p = pos + (dirOS * gust + side * sway) * anim.w;
    const float len = length(pos);
    const float newLen = length(p);
    p = (newLen > 1e-5f) ? p * (len / newLen) : p;

    // 2) 葉・枝のしなり（葉ごとの位相 G で少しずつずらす）
    const float phB = anim.y * kVertexAnimTau + objPhase;
    const float wb = 0.65f * sin(freq.x * w0 * t + phB) + 0.35f * sin(freq.y * w0 * t + 1.7f * phB);
    p += (up * (wb * branchUp) + dirOS * (strength * bias + 0.4f * wb)) * (anim.z * strength);

    // 3) 葉先の震え。両面化した裏の面（法線が逆）と同じ向きに動かして裂けないようにする
    const float3 n = nrm * (nrm.y >= 0.0f ? 1.0f : -1.0f);
    const float wf = sin(freq.z * w0 * t + anim.y * 5.0f * kVertexAnimTau
        + dot(pos, float3(1.3f, 2.1f, 1.7f)) * 3.0f);
    p += n * (anim.x * strength * wf);
    return p;
}

/// @brief 魚の泳ぎ（オブジェクト空間: 頭 +X、背 +Y、体の左 +Z）
/// @param anim  (t 吻端 0 → 尾 1, 横揺れの振幅, 胸びれの振幅, 胸びれの左右 ±1)。振幅は倍率を掛け済み
/// @param t     時間 [s]
/// @param phase 個体ごとの位相 0..1
/// @param speed 速さの倍率
float3 VertexAnimSwim(float3 pos, float4 anim, float t, float phase, float speed)
{
    // スズメダイの仲間の目安（トゲチョウチョウウオは速さの倍率 0.65 程度にする）
    const float bodyHz = VertexAnimQuantizeHz(2.2f * speed);
    const float finHz = VertexAnimQuantizeHz(4.5f * speed);
    const float wavelength = 0.95f; // 体長に対する体の波の波長

    // 頭から尾へ進む波（尾ほど振幅が大きい値が頂点に入っている）
    const float body = anim.y * sin(kVertexAnimTau * (anim.x / wavelength - bodyHz * t + phase));
    // 胸びれ: 外側へ開閉しながら前後に少し漕ぐ
    const float flap = kVertexAnimTau * (finHz * t + 1.7f * phase);
    pos.z += body + anim.w * anim.z * sin(flap);
    pos.x -= 0.5f * anim.z * cos(flap);
    return pos;
}

/// @brief 頂点アニメーションを適用した今フレームと前フレームの位置（オブジェクト空間）を求める
/// @param anim     VertexData::animData
/// @param normal   オブジェクト空間の法線
/// @param world    ワールド行列（個体ごとの位相を位置から作る）
/// @param worldInverseTranspose ワールド行列の逆行列の転置（風向きをオブジェクト空間へ移す）
/// @param pos      [in] 元の位置 / [out] 今フレームの位置
/// @param prevPos  [out] 前フレームの位置（モーションベクター用）
void ApplyVertexAnimation(float4 anim, float3 normal, float4x4 world, float4x4 worldInverseTranspose,
    inout float3 pos, out float3 prevPos)
{
    prevPos = pos;
    const int mode = gMaterial.vertexAnimation;
    if (mode == kVertexAnimNone)
    {
        return;
    }

    const float strength = gMaterial.vertexAnimStrength;
    const float speed = gMaterial.vertexAnimSpeed;
    // 個体ごとの位相: 同じモデルを並べても揃って動かないよう、ワールド位置から作る
    const float phase = frac(dot(world[3].xyz, float3(0.371f, 0.137f, 0.613f)));
    const float3 rest = pos;

    if (mode == kVertexAnimFish)
    {
        const float4 scaled = float4(anim.x, anim.y * strength, anim.z * strength, anim.w);
        pos = VertexAnimSwim(rest, scaled, gVertexAnim.time, phase, speed);
        prevPos = VertexAnimSwim(rest, scaled, gVertexAnim.prevTime, phase, speed);
        return;
    }

    const bool water = (mode == kVertexAnimSeagrass);
    const float2 dirXZ = water ? gVertexAnim.waterDir : gVertexAnim.windDir;
    // ワールド → オブジェクト。WorldInverse = transpose(WorldInverseTranspose) なので、
    // 行ベクトルの v * WorldInverse は mul(WorldInverseTranspose, v) と書ける
    float3 dirOS = mul((float3x3) worldInverseTranspose, float3(dirXZ.x, 0.0f, dirXZ.y));
    const float dirLen = length(dirOS);
    dirOS = (dirLen > 1e-5f) ? dirOS / dirLen : float3(1.0f, 0.0f, 0.0f);

    const float s = (water ? gVertexAnim.waterStrength : gVertexAnim.windStrength) * strength;
    // 木は 4 秒、海草は 6 秒を基本の周期にする（うねりの寄せ返しはゆっくり）
    const float baseHz = (water ? (1.0f / 6.0f) : (1.0f / 4.0f)) * speed;
    const float branchUp = water ? 0.0f : 0.8f;
    const float bias = water ? 0.25f : 0.6f;
    const float3 freq = water ? float3(1.0f, 3.0f, 10.0f) : float3(4.0f, 9.0f, 28.0f);
    const float objPhase = phase * kVertexAnimTau;

    pos = VertexAnimWind(rest, normal, anim, dirOS, s, gVertexAnim.time, objPhase, baseHz, branchUp, bias, freq);
    prevPos = VertexAnimWind(rest, normal, anim, dirOS, s, gVertexAnim.prevTime, objPhase, baseHz, branchUp, bias,
        freq);
}

#endif // VERTEX_ANIMATION_HLSLI
