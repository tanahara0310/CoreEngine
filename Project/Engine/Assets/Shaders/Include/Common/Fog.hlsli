#pragma once

/// @file Fog.hlsli
/// @brief 高さフォグの数式（エンジン内で唯一の実体）
/// @details 全画面合成パス（HeightFog.CS.hlsl）も、前方描画（半透明・水面・パーティクル）も
///          このファイルの関数を呼ぶこと。数式が 2 箇所に分かれると不透明と半透明で
///          フォグ量が食い違い、境目が線として見える。
///
/// 密度モデル: rho(y) = density * exp(-heightFalloff * (y - heightRef))
///   heightFalloff = 0 : 高さ非依存 ＝ 古典的な指数距離フォグ
///   heightFalloff > 0 : 高いほど薄い ＝ 地表に溜まる霧
/// レイに沿った光学的深さは解析的に積分できるので、レイマーチは不要。
///
/// @note 大気散乱には依存しない。空色ブレンド用の Sky-View LUT は
///       呼び出し側がサンプルして FogInscatteringColor へ値で渡す
///       （このファイルが AtmosphereCommon.hlsli を引くと、大気を持たない
///        前方シェーダーがフォグを適用できなくなる）。

/// @brief フォグの共有パラメータ（C++ 側 FogConstants と 1 対 1）
/// @note メンバを増減したら FogManager.h の kFogConstantsFields も直すこと。
///       食い違いは CB_BIND_HLSL のリフレクション照合がシェーダーロード時に検出する。
struct FogParameters
{
    float4x4 invViewProj;     ///< View*Projection の逆行列（行ベクトル規約: p' = p * M）
    float3   cameraWorldPos;  ///< カメラのワールド座標 [m]
    float    density;         ///< heightRef における消散係数 [1/m]
    float3   fogColor;        ///< フォグ色（リニア HDR）
    float    heightFalloff;   ///< 高さ方向の減衰率 [1/m]。0 で高さ非依存
    float    heightRef;       ///< density を与える基準高度 [m]
    float    startDistance;   ///< フォグが効き始めるカメラからの距離 [m]
    float    maxOpacity;      ///< フォグの最大濃度 [0,1]。1 未満なら遠景が消えきらない
    float    skyDistance;     ///< 背景ピクセルのレイ長 [m]
    uint     applyToSky;      ///< 背景（深度 far）にもフォグを掛けるか
    float3   sunDirection;    ///< 太陽光の進行方向（太陽→地表、正規化済み）
    float    sunExponent;     ///< 内散乱ローブの鋭さ。大きいほど太陽の周りだけ光る
    float3   sunTint;         ///< 太陽方向でのフォグの色味（基準色への倍率）
    float    sunGain;         ///< 太陽方向でのフォグの明るさ倍率。1 で無効
    float    skyColorBlend;   ///< フォグ色を空の色へ寄せる量 [0,1]。大気が無ければ 0
    float    cameraRadiusKm;  ///< Sky-View LUT サンプル用: 惑星中心からのカメラ距離 [km]
    float    planetRadiusKm;  ///< Sky-View LUT サンプル用: 惑星半径 [km]
};

/// @brief 光学的深さの上限。exp(-32) = 1.3e-14 で、これ以上は完全に不透明と区別できない
static const float kFogMaxOpticalDepth = 32.0f;

/// @brief exp の指数の上限。積 a * exp(e) が float の範囲を超えないよう抑える
/// @details exp(40) = 2.4e17。密度側と合わせても 3.4e38 に届かない
static const float kFogMaxExponent = 40.0f;

/// @brief レイ origin + t*dir の区間 [t0, t1] における光学的深さを解析積分する
/// @param p      フォグパラメータ
/// @param origin レイ始点（通常はカメラ位置）
/// @param dir    正規化済みレイ方向
/// @param t0     積分開始距離 [m]
/// @param t1     積分終了距離 [m]
/// @return 光学的深さ tau（透過率は exp(-tau)）
/// @details 密度は高さの指数関数なので、区間積分は 2 つの指数の差で閉じた形に書ける:
///          tau = density * (exp(-falloff*(y0-href)) - exp(-falloff*(y1-href))) / (falloff*dir.y)。
///          falloff*dir.y -> 0 の極限は始点密度 × 区間長。
float FogOpticalDepth(FogParameters p, float3 origin, float3 dir, float t0, float t1)
{
    const float s = max(t1 - t0, 0.0f);
    if (s <= 0.0f || p.density <= 0.0f)
    {
        return 0.0f;
    }

    const float y0 = origin.y + dir.y * t0;      // 区間の始点高度
    const float y1 = y0 + dir.y * s;             // 区間の終点高度
    const float k = p.heightFalloff * dir.y;     // 高さ減衰 × 視線の傾き

    // ほぼ水平なレイ、または heightFalloff = 0 は一様密度として積分する
    // （このとき下の差分は 0/0 になり、桁落ちで精度も出ない）
    if (abs(k * s) < 1.0e-4f)
    {
        const float e = min(-p.heightFalloff * (y0 - p.heightRef), kFogMaxExponent);
        return min(p.density * exp(e) * s, kFogMaxOpticalDepth);
    }

    // tau = density * (exp(-falloff*(y0-href)) - exp(-falloff*(y1-href))) / k
    //
    // 指数は「上限だけ」を打ち止める。下側は exp が 0 へ落ちるのが正しい極限なので
    // クランプしてはいけない。始点側（小さい値）と区間長側（大きい値）を別々に
    // クランプすると、本来 0 に潰れるはずの積が残って、フォグ層より上の面にまで
    // 薄く霞がかかる（heightFalloff を大きくするほど顕著になる）。
    const float e0 = min(-p.heightFalloff * (y0 - p.heightRef), kFogMaxExponent);
    const float e1 = min(-p.heightFalloff * (y1 - p.heightRef), kFogMaxExponent);

    const float tau = p.density * (exp(e0) - exp(e1)) / k;
    return min(max(tau, 0.0f), kFogMaxOpticalDepth);
}

/// @brief 区間 [t0, t1] の透過率を返す（1 = フォグなし、0 = 完全にフォグ色）
float FogTransmittance(FogParameters p, float3 origin, float3 dir, float t0, float t1)
{
    const float tau = FogOpticalDepth(p, origin, dir, t0, t1);
    return lerp(1.0f, exp(-tau), saturate(p.maxOpacity));
}

/// @brief カメラから worldPos までの区間の透過率を返す
float FogTransmittanceToPoint(FogParameters p, float3 worldPos)
{
    const float3 toSurface = worldPos - p.cameraWorldPos;
    const float distance = length(toSurface);
    if (distance < 1.0e-5f)
    {
        return 1.0f;
    }

    const float3 dir = toSurface / distance;
    const float t0 = min(p.startDistance, distance);
    return FogTransmittance(p, p.cameraWorldPos, dir, t0, distance);
}

/// @brief 視線方向のフォグ色（内散乱色）を求める
/// @param p             フォグパラメータ
/// @param rayDir        カメラから見た正規化済み視線方向
/// @param skyLuminance  その方向の空の輝度（Sky-View LUT の値）
/// @details 基準色を空の色へ寄せてから、太陽方向に前方散乱のローブを乗せる。
///          ローブは cos^n の 1 項だけで、ミー位相関数のような厳密さは狙わない
///          （フォグは美術パラメータで、大気散乱のように物理値で駆動しないため）。
/// @note ローブは絶対色ではなく「基準色への倍率」として乗せる。Sky-View LUT の輝度は
///       数十のオーダーで、美術値の色（〜1）と lerp すると太陽方向でフォグが
///       暗くなってしまうため。倍率なら空色ブレンドの有無で挙動が変わらない。
float3 FogInscatteringColor(FogParameters p, float3 rayDir, float3 skyLuminance)
{
    // 空色ブレンド: 大気があるシーンでは遠景のフォグが空へ溶ける。
    // 大気が無いフレームは C++ 側が skyColorBlend = 0 にするので fogColor がそのまま残る
    const float3 base = lerp(p.fogColor, skyLuminance, saturate(p.skyColorBlend));

    // sunDirection は進行方向（太陽→地表）なので、太陽を見る向きはその逆
    const float cosTheta = dot(rayDir, -p.sunDirection);
    const float lobe = pow(saturate(cosTheta), max(p.sunExponent, 1.0e-3f));

    // 太陽が無いフレームは C++ 側が sunTint = (1,1,1) / sunGain = 1 にするので恒等になる
    const float3 gain = lerp(float3(1.0f, 1.0f, 1.0f), p.sunTint * p.sunGain, lobe);
    return base * gain;
}

/// @brief 空の輝度を持たない呼び出し側（前方描画）用のフォグ色
/// @details skyLuminance に fogColor を渡すのでブレンドは恒等になり、
///          太陽の内散乱だけが乗る。
float3 FogInscatteringColor(FogParameters p, float3 rayDir)
{
    return FogInscatteringColor(p, rayDir, p.fogColor);
}

/// @brief ワールド座標が分かっている面の色へフォグを合成する
/// @details 前方描画（半透明・水面・パーティクル）はこれを呼ぶ。
///          全画面パスと同じ数式を通るので、不透明との境目が出ない。
float3 ApplyFog(FogParameters p, float3 worldPos, float3 color)
{
    const float3 toSurface = worldPos - p.cameraWorldPos;
    const float distance = length(toSurface);
    if (distance < 1.0e-5f)
    {
        return color;
    }

    const float3 dir = toSurface / distance;
    const float t0 = min(p.startDistance, distance);
    const float transmittance = FogTransmittance(p, p.cameraWorldPos, dir, t0, distance);

    return lerp(FogInscatteringColor(p, dir), color, transmittance);
}
