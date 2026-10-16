// TrolleyLoading.CS.hlsl - トロッコが走るロード画面（プロジェクトのロード画面のシェーダ）
//
// 絵は Textures/Loading/*.png（ドット絵）を奥から順に重ねる：奥の並木 → レール → 駅 → トロッコ → 「ローディング中」。
// 並木は等間隔に置いて大きさだけ区画ごとに変え、「ローディング中」の右の点は時間で 0→3 個と増やす。
//
// ■ スプライトの寸法
//   4 テクセル = ボクセル 1 個で、どれも 64 テクセル幅のマス目（rail 64x64 / monkey 64x68 / station 64x128 / tree 64x64）。
//   全部を同じ uiScale で並べる。レールはマスの下 8 テクセルにだけ絵があり、上面はマスの上から kRailSurfaceRatio の位置。
//   ほかの絵は下端を接地させる。
//
// ■ 進み具合
//   カメラは世界に固定し、進み具合のぶんだけトロッコが左から右へ走る。駅も右外から寄ってくるが、動く量はトロッコの 1/4 ほど。
//   跳ねと傾きは時間で回すので、進み具合が止まっても揺れ続ける。
//
// ■ 座標と色
//   位置は縦 1080 基準のピクセルで持ち、uiScale で実解像度へ拡大する。
//   この段の出力はリニアで、Load が返すのは PNG の値（sRGB）なので、スプライトも点の色も SrgbToLinear を通して合成する。

#include "ShaderMath.hlsli" // PI / TWO_PI

Texture2D<float4> gTexture : register(t0); // 合成前の画面
Texture2D<float4> gImage0  : register(t1); // トロッコ＋サル
Texture2D<float4> gImage1  : register(t2); // レール 1 周期
Texture2D<float4> gImage2  : register(t3); // 駅（進み具合の到達点）
Texture2D<float4> gImage3  : register(t4); // 奥の並木（1 本ぶん）
Texture2D<float4> gImage4  : register(t5); // 「ローディング中」（ドット絵フォントを焼いたもの）
RWTexture2D<float4> gOutput : register(u0);

cbuffer ProjectLoadingParams : register(b0)
{
    float screenAlpha; // 表示の強さ（0 = 出さない ／ 1 = 出し切る）
    float time;        // 出し始めからの時間（1 フレーム 0.1 秒まで）
    float realTime;    // 出し始めからの実時間
    float progress;    // 読み込みの進み具合（0〜1）
    float gaugeAlpha;
    float pad0;
    float pad1;
    float pad2;
};

cbuffer ScreenParams : register(b1)
{
    uint screenWidth;
    uint screenHeight;
    float2 pad;
};

static const uint  kGroupSize       = 8;
static const float kReferenceHeight = 1080.0f; // レイアウト値の基準解像度
static const float kStationEnter    = 60.0f;   // 駅が画面右外から現れる距離

// 見た目の値（縦 1080 基準の px）
static const float kBobSpeed      = 256.0f;  // 跳ねる周期を決める速さ（px/秒。レール 1 タイルで 1 回跳ねる）
static const float kParallax      = 0.32f;   // 奥の景色の速度比（kRailScroll に対して）
static const float kRailY         = 0.90f;   // レール上面（画面高さに対する比率）
static const float kCartX         = -0.14f;  // 進み具合 0 のトロッコ左端（画面幅に対する比率）
static const float kBobAmp        = 2.0f;    // 上下の揺れ幅
static const float kTiltDegrees   = 1.6f;    // 前後の傾き（度）
static const float kCartLift      = 0.0f;    // レール上面から車体下端までの距離
static const float kStationGoal   = 80.0f;   // 進み具合 1 で駅が来る位置（到着したトロッコの左端から）
static const float kStationDrop   = 8.0f;    // レール上面から駅の下端までの距離
static const float kTreeDrop      = 8.0f;    // レール上面から木の下端までの距離
static const float kScale         = 2.4f;    // 全体の拡大率
static const float kCartGoalX     = 0.73f;   // 進み具合 1 のトロッコ左端（画面幅に対する比率）
static const float kRailScroll    = 0.0f;    // レールと景色が流れる速さ（0 で世界に固定）
static const float kTextScale     = 1.0f;    // 「ローディング中」の拡大率
static const float kTextY         = 0.5f;    // 文字列の中心の高さ（画面高さに対する比率）
static const float kDotInterval   = 0.35f;   // 点が 1 つ増える間隔（秒）
static const float kTextGap       = 21.0f;   // 文字列の右端から最初の点までの距離
static const float kTreeSpacing   = 128.0f;  // 木 1 本ぶんの区画幅（木はこの中央に立つ）
static const float kTreeDim       = 0.55f;   // 奥の木の暗さ（1.0 で描いたままの色）
static const float kTreeScaleVary = 0.35f;   // 木の大きさの振れ幅（0.65〜1.35 倍を 5 段で使う）

// レール上面がタイルの上から何割の位置にあるか（上 4 テクセルがレール、下 4 テクセルが枕木の境目）
static const float kRailSurfaceRatio = 56.0f / 64.0f;

// 奥の並木の大きさ。区画ごとに「段数と互いに素な歩幅」で段を回し、ハッシュで 0〜1 段だけ揺らす（隣と同じ段にならない）
static const uint kTreeSizeSteps  = 5;
static const uint kTreeSizeStride = 3;

// 「ローディング中」の右に並べる点（フォントのドットを単位にする）
static const uint  kDotCount     = 3;
static const float kDotSize      = 14.0f;
static const float kDotStride    = 28.0f;
static const float kDotBaseInset = 7.0f;   // 画像の下端から点の下端まで（文字のベースラインに揃える）
static const float kTextDimMin   = 0.72f;  // 明滅の下限
static const float3 kTextColor   = float3(1.0f, 1.0f, 1.0f); // 点の色（sRGB）

float3 SrgbToLinear(float3 c)
{
    float3 lo = c / 12.92f;
    float3 hi = pow(max(c + 0.055f, 0.0f) / 1.055f, 2.4f);
    return lerp(lo, hi, step(0.04045f, c));
}

// 区画番号から 0〜1 の擬似乱数を作る（ビット混合なので小さい連番でもばらける）
float Hash11(float n)
{
    uint x = (uint)(int)n;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (float)x * (1.0f / 4294967296.0f);
}

float2 Rotate(float2 p, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// スプライトを 1 テクセル読む（local はスプライト左上を原点としたテクセル座標。点サンプル・rgb はリニアへ変換）
float4 LoadSprite(Texture2D<float4> tex, float2 local)
{
    uint w, h;
    tex.GetDimensions(w, h);
    int2 p = int2(floor(local));
    if (p.x < 0 || p.y < 0 || p.x >= (int)w || p.y >= (int)h)
    {
        return (float4)0.0f;
    }
    float4 texel = tex.Load(int3(p, 0));
    return float4(SrgbToLinear(texel.rgb), texel.a);
}

[numthreads(kGroupSize, kGroupSize, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= screenWidth || dispatchId.y >= screenHeight)
    {
        return;
    }

    float3 color = gTexture.Load(int3(dispatchId.xy, 0)).rgb;

    if (screenAlpha > 0.001f)
    {
        float  uiScale = (float)screenHeight / kReferenceHeight * kScale;
        float2 pix     = (float2)dispatchId.xy + 0.5f;
        float  railTop = kRailY * (float)screenHeight;

        uint railW, railH; gImage1.GetDimensions(railW, railH);
        uint cartW, cartH; gImage0.GetDimensions(cartW, cartH);
        uint treeW, treeH; gImage3.GetDimensions(treeW, treeH);
        uint stnW,  stnH;  gImage2.GetDimensions(stnW, stnH);

        // 進み具合を走行量へ（両端を緩めて発車と到着をなめらかにする）
        float travel = saturate(progress);
        travel = travel * travel * (3.0f - 2.0f * travel);

        float cartStart = kCartX * (float)screenWidth;
        float cartLeft  = lerp(cartStart, kCartGoalX * (float)screenWidth, travel);

        float scroll = kRailScroll * time;

        // 跳ねと傾きは時間で回す
        float phase = TWO_PI * kBobSpeed * time / (float)railW;
        float bob   = (sin(phase) - 0.5f) * kBobAmp * uiScale;
        float tilt  = sin(phase + 1.1f) * radians(kTiltDegrees);

        // ---- 奥の並木：区画ごとに 1 本、区画の中央へ置き、大きさだけ変える ----
        {
            float sx      = pix.x / uiScale + scroll * kParallax;
            float spacing = max(kTreeSpacing, (float)treeW);
            float slot    = floor(sx / spacing);

            uint  sizeStep = ((uint)slot * kTreeSizeStride + (uint)(Hash11(slot) * 2.0f)) % kTreeSizeSteps;
            float treeScale = lerp(1.0f - kTreeScaleVary, 1.0f + kTreeScaleVary,
                                   (float)sizeStep / (float)(kTreeSizeSteps - 1));
            // 区画からはみ出さない倍率で頭打ちにする
            treeScale = clamp(treeScale, 0.1f, spacing / (float)treeW);

            float  width  = (float)treeW * treeScale;
            float  left   = slot * spacing + (spacing - width) * 0.5f;
            float  ground = railTop + kTreeDrop * uiScale;
            float2 local  = float2((sx - left) / treeScale,
                                   (pix.y - ground) / (uiScale * treeScale) + (float)treeH);
            float4 c = LoadSprite(gImage3, local);
            color = lerp(color, c.rgb * kTreeDim, c.a * screenAlpha);
        }

        // ---- レール（剰余で繰り返す。レール上面を railTop に合わせる） ----
        {
            float lx = fmod(pix.x / uiScale + scroll, (float)railW);
            float ly = (pix.y - railTop) / uiScale + (float)railH * kRailSurfaceRatio;
            float4 c = LoadSprite(gImage1, float2(lx, ly));
            color = lerp(color, c.rgb, c.a * screenAlpha);
        }

        // ---- 駅：右外から寄ってくる ----
        {
            float goal   = kCartGoalX * (float)screenWidth + kStationGoal * uiScale;
            float startX = (float)screenWidth + kStationEnter * uiScale;
            float sx     = lerp(startX, goal, saturate(progress));
            float2 local = float2((pix.x - sx) / uiScale,
                                  (pix.y - (railTop + kStationDrop * uiScale)) / uiScale + (float)stnH);
            float4 c = LoadSprite(gImage2, local);
            color = lerp(color, c.rgb, c.a * screenAlpha);
        }

        // ---- トロッコ：レールの上を跳ねながら前後に傾いて進む ----
        {
            float  cartTop = railTop - (kCartLift + (float)cartH) * uiScale + bob;
            float2 half    = float2((float)cartW, (float)cartH) * 0.5f * uiScale;
            float2 center  = float2(cartLeft, cartTop) + half;
            float2 local   = (Rotate(pix - center, -tilt) + half) / uiScale;
            float4 c = LoadSprite(gImage0, local);
            color = lerp(color, c.rgb, c.a * screenAlpha);
        }

        // ---- 「ローディング中…」（画面中央。文字列と点をひとかたまりで中央へ置く）----
        {
            uint textW, textH; gImage4.GetDimensions(textW, textH);
            float textScl = (float)screenHeight / kReferenceHeight * kTextScale;

            float dotsW     = kDotSize + kDotStride * (float)(kDotCount - 1);
            float groupW    = ((float)textW + kTextGap + dotsW) * textScl;
            float groupLeft = ((float)screenWidth - groupW) * 0.5f;
            float textTop   = kTextY * (float)screenHeight - (float)textH * 0.5f * textScl;

            // 点が 1 周する時間。文字の明滅も同じ周期に乗せる
            float interval = max(kDotInterval, 0.01f);
            float cycle    = interval * (float)(kDotCount + 1);
            float dotPhase = fmod(realTime, cycle);
            float pulse    = lerp(kTextDimMin, 1.0f, 0.5f + 0.5f * sin(TWO_PI * realTime / cycle));

            float4 t = LoadSprite(gImage4, (pix - float2(groupLeft, textTop)) / textScl);
            color = lerp(color, t.rgb, t.a * screenAlpha * pulse);

            float dotsLeft = groupLeft + ((float)textW + kTextGap) * textScl;
            float dotTop   = textTop + ((float)textH - kDotBaseInset - kDotSize) * textScl;
            float side     = kDotSize * textScl;

            [unroll]
            for (uint d = 0; d < kDotCount; ++d)
            {
                // d 番目は (d+1) 個目の間隔を過ぎてから出る
                bool shown = dotPhase >= (float)(d + 1) * interval;
                float2 local = pix - float2(dotsLeft + kDotStride * (float)d * textScl, dotTop);
                if (shown && local.x >= 0.0f && local.x < side
                          && local.y >= 0.0f && local.y < side)
                {
                    color = lerp(color, SrgbToLinear(kTextColor), screenAlpha * pulse);
                }
            }
        }
    }

    gOutput[dispatchId.xy] = float4(color, 1.0f);
}
