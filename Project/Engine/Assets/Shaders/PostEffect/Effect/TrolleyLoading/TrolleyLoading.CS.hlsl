// TrolleyLoading.CS.hlsl - トロッコが走るローディング画面 コンピュートシェーダー
//
// 絵は Assets/Textures/loading_*.png（.obj から正射投影で焼いたボクセルのスプライト）を
// 奥から順に重ねるだけ。手続き的に描くのは「ローディング中…」の点だけ。
//
// ■ 「ローディング中…」
//   画面中央にドット絵フォントを焼いた 1 枚（loading_text.png）を置き、その右へ点を
//   手続き的に並べる。点は 0→1→2→3 個と増えて戻り、文字は同じ周期でゆっくり明滅する。
//   進捗ではなく時間で回すので、シーン構築が詰まっても「動いている」ことが伝わる。
//   時計は time ではなく textTime（実測の経過時間）。time はコマ落ち対策で 1 フレーム
//   0.1 秒までに切り詰めてあり、重い読み込みの間はほとんど進まない ―― それで点を
//   回すと、肝心のローディング中だけ止まって見える。
//
// ■ 進捗の見せ方
//   カメラは世界に固定してあり（railScroll = 0）、進捗ぶんだけ「トロッコが左から右へ
//   走っていく」。駅も右外から少しだけ寄ってくるが、動く量はトロッコの 1/4 ほどなので
//   遠景が近づく程度にしか見えない。トロッコを止めて駅だけを動かすと、駅の方が
//   歩いて来るように見えて違和感が出る ―― この配分は崩さないこと。
//
// ■ 座標
//   位置は「縦 1080 基準のピクセル」で持ち、uiScale で実解像度へ拡大する。
//   基準解像度を変えるとレイアウトが全部ずれるので kReferenceHeight は触らないこと。
//
// ■ 色空間
//   この段（PostTonemap）の出力はリニアで、sRGB へのエンコードは最終提示で掛かる。
//   一方 Load が返すのは PNG の生の値（sRGB）なので、スプライトも定数で書いた色
//   （点）も SrgbToLinear を通してから合成する。LoadingScreen.CS.hlsl と同じ扱い。
//   これを省くと枕木の (133,87,43) が画面上で (189,158,115) まで浮く。

#include "ShaderMath.hlsli" // PI / TWO_PI

Texture2D<float4> gTexture : register(t0); // 合成前の画面
Texture2D<float4> gCart    : register(t1); // トロッコ＋猿
Texture2D<float4> gRail    : register(t2); // レール 1 周期
Texture2D<float4> gStation : register(t3); // 駅（進捗の到達点）
Texture2D<float4> gScenery : register(t4); // 奥の景色（木・岩を焼き込んだ帯）
Texture2D<float4> gText    : register(t5); // 「ローディング中」（ドット絵フォントを焼いたもの）
RWTexture2D<float4> gOutput : register(u0);

cbuffer TrolleyParams : register(b0)
{
    float screenAlpha;  // 表示強度 (0.0 = 非表示, 1.0 = 完全表示)
    float time;         // 経過時間（秒）
    float progress;     // 読み込みの進捗 (0.0〜1.0)
    float bobSpeed;     // 跳ねる周期を決める速さ（1080 基準の px/秒）

    float parallax;     // 奥の景色の速度比（railScroll に対して）
    float railY;        // レール上端（画面高さに対する比率）
    float cartX;        // 進捗 0 のときのトロッコ左端（画面幅に対する比率）
    float bobAmp;       // 上下の揺れ幅（1080 基準の px）

    float tiltDegrees;  // 前後の傾き（度）
    float cartLift;     // レール上端から車体下端までの距離（0 でレールに載る）
    float stationGoal;  // 進捗 1.0 で駅が来る位置（トロッコ左端からの距離）
    float stationDrop;  // レール上端から駅の下端までの距離

    float sceneryDrop;  // レール上端から景色の下端までの距離
    float scale;        // 全体の拡大率。上の距離もスプライトも一括で掛かる
    float cartGoalX;    // 進捗 1 のときのトロッコ左端（画面幅に対する比率）
    float railScroll;   // レールと景色が流れる速さ（1080 基準の px/秒。0 で世界に固定）

    float textTime;     // 「ローディング中…」用の経過時間（秒。実測のまま＝上の time とは別）
    float textScale;    // 文字の拡大率（1.0 で焼いたままの大きさ）
    float textY;        // 文字列の中心の高さ（画面高さに対する比率）
    float dotInterval;  // 点が 1 つ増える間隔（秒）

    float textGap;      // 文字列の右端から最初の点までの距離（1080 基準 px）
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

// 「ローディング中」の右に並べる点。大きさはフォントのドット（84px 焼き = 7px）を単位にする
static const uint  kDotCount     = 3;      // 点の数。1 周期でこの数まで増えて 0 へ戻る
static const float kDotSize      = 14.0f;  // 点 1 つの一辺（1080 基準 px。フォント 2 ドット分）
static const float kDotStride    = 28.0f;  // 点の左端どうしの間隔
static const float kDotBaseInset = 7.0f;   // 画像の下端から点の下端まで（文字のベースラインに揃える）
static const float kTextDimMin   = 0.72f;  // 明滅の下限（1.0 との間を往復する）
static const float3 kTextColor   = float3(1.0f, 1.0f, 1.0f); // 点の色（sRGB。文字の白と揃える）

float3 SrgbToLinear(float3 c)
{
    float3 lo = c / 12.92f;
    float3 hi = pow(max(c + 0.055f, 0.0f) / 1.055f, 2.4f);
    return lerp(lo, hi, step(0.04045f, c));
}

float2 Rotate(float2 p, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

/// スプライトを 1 テクセル読む。local はスプライト左上を原点としたテクセル座標
/// @return rgb はリニアへ変換済み、a はそのまま（アルファはガンマを持たない）
/// @note 点サンプルなのでボクセルのドットが潰れない（拡大してもエッジが甘くならない）
/// @note リニア化はここ 1 か所に閉じてある。呼び出し側で掛け忘れると色が浮く
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
        // 位置も大きさも 1080 基準の距離に uiScale を掛けて出しているので、
        // ここへ scale を畳み込むだけで全体が相似のまま拡大・縮小する。
        // 走る速さも跳ねる周期も 1080 基準の距離のまま計算するため、
        // 縮めても「レール 1 本ぶん進むと 1 回跳ねる」の関係は崩れない
        float  uiScale = (float)screenHeight / kReferenceHeight * scale;
        float2 pix     = (float2)dispatchId.xy + 0.5f;
        float  railTop = railY * (float)screenHeight;

        uint railW,  railH;  gRail.GetDimensions(railW, railH);
        uint cartW,  cartH;  gCart.GetDimensions(cartW, cartH);
        uint sceneW, sceneH; gScenery.GetDimensions(sceneW, sceneH);
        uint stnW,   stnH;   gStation.GetDimensions(stnW, stnH);

        // 進捗を走行量へ。両端を緩めて、発車と到着をなめらかにする
        float travel = saturate(progress);
        travel = travel * travel * (3.0f - 2.0f * travel);

        // トロッコは進捗ぶんだけ左から右へ進む。駅は後で travel から別に置く
        float cartStart = cartX * (float)screenWidth;
        float cartLeft  = lerp(cartStart, cartGoalX * (float)screenWidth, travel);

        // 背景の流し（既定は 0 ＝ カメラを世界に固定する）
        float scroll = railScroll * time;

        // 跳ねと傾きだけは時間で回す。読み込みが詰まって進捗が止まっても、
        // トロッコがその場で揺れ続けるので画面が固まって見えない
        float phase = TWO_PI * bobSpeed * time / (float)railW;
        float bob   = (sin(phase) - 0.5f) * bobAmp * uiScale;
        float tilt  = sin(phase + 1.1f) * radians(tiltDegrees);

        // ---- 奥の景色（ゆっくり流れる） ----
        {
            float lx = fmod(pix.x / uiScale + scroll * parallax, (float)sceneW);
            float ly = (pix.y - (railTop + sceneryDrop * uiScale)) / uiScale + (float)sceneH;
            float4 c = LoadSprite(gScenery, float2(lx, ly));
            color = lerp(color, c.rgb, c.a * screenAlpha);
        }

        // ---- レール（剰余で無限スクロール） ----
        {
            float lx = fmod(pix.x / uiScale + scroll, (float)railW);
            float ly = (pix.y - railTop) / uiScale;
            float4 c = LoadSprite(gRail, float2(lx, ly));
            color = lerp(color, c.rgb, c.a * screenAlpha);
        }

        // ---- 駅：右外から寄ってくるが、動く量はトロッコよりずっと小さい ----
        {
            float goal   = cartGoalX * (float)screenWidth + stationGoal * uiScale;
            float startX = (float)screenWidth + kStationEnter * uiScale;
            float sx     = lerp(startX, goal, saturate(progress));
            float2 local = float2((pix.x - sx) / uiScale,
                                  (pix.y - (railTop + stationDrop * uiScale)) / uiScale + (float)stnH);
            float4 c = LoadSprite(gStation, local);
            color = lerp(color, c.rgb, c.a * screenAlpha);
        }

        // ---- トロッコ本体（レールの上を、跳ねながら前後に傾いて進む） ----
        {
            float  cartTop = railTop - (cartLift + (float)cartH) * uiScale + bob;
            float2 half    = float2((float)cartW, (float)cartH) * 0.5f * uiScale;
            float2 center  = float2(cartLeft, cartTop) + half;
            float2 local   = (Rotate(pix - center, -tilt) + half) / uiScale;
            float4 c = LoadSprite(gCart, local);
            color = lerp(color, c.rgb, c.a * screenAlpha);
        }

        // ---- 「ローディング中…」（画面中央）----
        // 文字列と点をひとかたまりとして中央へ置く。トロッコの scale とは独立に
        // 拡大したいので、uiScale ではなく textScale から作り直す
        {
            uint textW, textH; gText.GetDimensions(textW, textH);
            float textScl = (float)screenHeight / kReferenceHeight * textScale;

            float dotsW     = kDotSize + kDotStride * (float)(kDotCount - 1);
            float groupW    = ((float)textW + textGap + dotsW) * textScl;
            float groupLeft = ((float)screenWidth - groupW) * 0.5f;
            float textTop   = textY * (float)screenHeight - (float)textH * 0.5f * textScl;

            // 点が 1 周する時間。文字の明滅も同じ周期に乗せて足並みを揃える
            float interval = max(dotInterval, 0.01f);
            float cycle    = interval * (float)(kDotCount + 1);
            float dotPhase = fmod(textTime, cycle);
            float pulse    = lerp(kTextDimMin, 1.0f, 0.5f + 0.5f * sin(TWO_PI * textTime / cycle));

            float4 t = LoadSprite(gText, (pix - float2(groupLeft, textTop)) / textScl);
            color = lerp(color, t.rgb, t.a * screenAlpha * pulse);

            float dotsLeft = groupLeft + ((float)textW + textGap) * textScl;
            float dotTop   = textTop + ((float)textH - kDotBaseInset - kDotSize) * textScl;
            float side     = kDotSize * textScl;

            [unroll]
            for (uint d = 0; d < kDotCount; ++d)
            {
                // d 番目は (d+1) 個目の間隔を過ぎてから出る（0 個の状態から始まる）
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
