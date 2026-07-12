// LensFlareFindSource.CS.hlsl - 支配的な光源の UV 位置を検出する
// ゴーストへ絞り羽根形状（多角形）マスクを正しい位置で重ねるための前段。
//
// 輝度加重の重心（サブテクセル精度の連続値）を求める。以前は argmax（最大輝度
// テクセル）だったが、それはカメラ移動でフレアが振動する不具合の原因だった:
//   1. 輝度抽出は maxBrightness でクランプされるため、太陽ディスク内の多数の
//      テクセルが「同値の最大」でタイになり、argmax はタイの先頭（走査順）を返す。
//      カメラがサブピクセル動くだけで先頭が飛び回る。
//   2. sourceUv は 1/4 解像度のテクセル単位でしか動けず、ゴースト中心の逆算
//      (1-sourceUv-0.5s)/(1-s) が 1/|1-s| 倍（既定設定で最大5倍）に増幅する。
//   3. エディタカメラの慣性で操作後もしばらく微動が続くため、
//      「動かすと振動→しばらくして収束」に見える。
// 重心はタイの影響を受けず連続に動くため振動しない。残留ジッタ（雲の移流による
// 輝度分布の揺れ等）は末尾の適応型時間平滑化で吸収する。

#include "LensFlareCommon.hlsli"

Texture2D<float4> gBright : register(t0);
RWTexture2D<float2> gSourcePos : register(u0); // 1x1: 支配的光源の UV（前フレーム値を履歴として再利用）

static const uint kThreadCount = 256;
groupshared float gSumW[kThreadCount];
groupshared float2 gSumUvW[kThreadCount];

[numthreads(kThreadCount, 1, 1)]
void main(uint3 groupThreadId : SV_GroupThreadID)
{
    const uint tid = groupThreadId.x;
    const uint totalTexels = gFlareWidth * gFlareHeight;
    const float2 flareSize = float2(gFlareWidth, gFlareHeight);

    float sumW = 0.0f;
    float2 sumUvW = float2(0.0f, 0.0f);

    [loop]
    for (uint idx = tid; idx < totalTexels; idx += kThreadCount)
    {
        const uint2 coord = uint2(idx % gFlareWidth, idx / gFlareWidth);
        const float lum = LensFlareLuminance(gBright.Load(int3(coord, 0)).rgb);
        // 輝度の2乗で重み付けし、高輝度コア（太陽ディスク）へ重心を寄せる。
        // 1乗だと周辺グレアの裾（面積が広い）に重心が引っ張られる。
        const float w = lum * lum;
        sumW += w;
        sumUvW += w * ((float2(coord) + 0.5f) / flareSize);
    }

    gSumW[tid] = sumW;
    gSumUvW[tid] = sumUvW;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = kThreadCount / 2; stride > 0; stride >>= 1)
    {
        if (tid < stride)
        {
            gSumW[tid] += gSumW[tid + stride];
            gSumUvW[tid] += gSumUvW[tid + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (tid == 0)
    {
        const float2 centroidUv = (gSumW[0] > 1e-5f)
            ? gSumUvW[0] / gSumW[0]
            : float2(0.5f, 0.5f);

        // ===== 適応型の時間平滑化 =====
        // 静止時の残留ジッタ（雲の移流・ノイズで輝度分布が揺れる）は小さな変化として
        // 現れるので緩やかに追従し、カメラパン等の大きな変化には即時追従する
        // （一律の平滑化だとゴーストが光源に遅れて泳ぐ）。
        const float2 prevUv = gSourcePos[uint2(0, 0)];
        // 初回フレームは未初期化（不定値/NaN の可能性）。NaN は比較が false になるので
        // 「有効範囲内」判定に失敗し、自動的に即時追従（履歴破棄）になる。
        const bool prevValid = all(prevUv >= -0.5f) && all(prevUv <= 1.5f);
        const float jitterThreshold = 2.0f / flareSize.y; // 2テクセル相当
        const float dist = length(centroidUv - prevUv);
        const bool smoothFollow = (prevValid && dist < jitterThreshold);

        // 注意: lerp(NaN, x, 1) は NaN のままなので、即時追従は lerp ではなく代入にする
        // （未初期化の初回フレームで NaN が永続するのを防ぐ）
        gSourcePos[uint2(0, 0)] = smoothFollow
            ? lerp(prevUv, centroidUv, 0.25f)
            : centroidUv;
    }
}
