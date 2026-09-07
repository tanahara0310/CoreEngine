#include "MsdfText.hlsli"

ConstantBuffer<TextBatch> gBatch : register(b0);

// アトラスは Texture2DArray。1 枚が埋まったら次の枚へ送るので、
// 文字列が複数枚にまたがっても 1 ドローコールで描ける
Texture2DArray<float4> gAtlas   : register(t0);
SamplerState           gSampler : register(s0);

// 縁取り用にしきい値をずらせる量の上限（距離場の値）。
// 距離場は輪郭の外側 pxRange/2 px ぶんしか持たないので、これ以上ずらすと
// クワッドの端で α が 0 に落ちきらず、文字のまわりに矩形が出る。
// UIText.cpp の kMaxOutlineSd と同じ値にしておくこと
//（CPU 側はエディタのスライダー上限、こちらが最後の砦）
static const float kMaxOutlineSd = 0.375f;

// 太さ調整でずらせる量の上限（距離場の値）
static const float kMaxWeightSd = 0.45f;

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// ------------------------------------------------------------
// MSDF の復元は中央値ひとつで終わる。
//
// アトラスの RGB には「それぞれ別のエッジ群だけを見た距離場」が入っている。
// 辺の途中では 3ch のうち 2 つが同じ正しい距離を持ち、コーナーでは 2 本の
// エッジの距離場が交差する。中央値を取ると、辺では正しい距離が、
// コーナーではその交点＝鋭い角が復元される。
// ------------------------------------------------------------
float Median(float r, float g, float b)
{
    return max(min(r, g), min(max(r, g), b));
}

// ------------------------------------------------------------
// テクスチャ空間の pxRange を「今この画素で何画面ピクセルぶんか」へ変換する。
//
// fwidth(uv) は隣の画素との UV 差分＝画面 1px あたりの UV 変化量なので、
// その逆数が「UV 1.0 が画面何 px に伸びているか」になる。
// 拡大率・回転・パースが変わると fwidth も変わるため、
// アンチエイリアス幅が表示サイズへ自動追従する。
// ここを定数にすると、拡大時にボケ、縮小時にジャギる。
// ------------------------------------------------------------
float ScreenPxRange(float2 uv)
{
    float2 unitRange     = gBatch.pxRange / float2(gBatch.atlasWidth, gBatch.atlasHeight);
    float2 screenTexSize = 1.0f / fwidth(uv);

    // 1.0 でクランプしないと、縮小時に AA 幅が 1px を割って文字が消える
    return max(0.5f * dot(unitRange, screenTexSize), 1.0f);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    // texcoord.z が配列の添字（正規化しない実数の枚番号）
    float4 msd = gAtlas.Sample(gSampler, input.texcoord);

    // 0.5 が輪郭。これより大きければ字の内側。
    // 塗りは median(rgb)。コーナーが鋭く出るのはこちらだけ
    float signedDistance = Median(msd.r, msd.g, msd.b);

    // 縁取りは a に入っている「真の SDF」で判定する（アトラスは MTSDF）。
    // rgb は各チャンネルがエッジの無限延長への垂線距離＝擬似距離なので、
    // 輪郭から離れるほど真の距離より大きい値を返す。縁取りのしきい値は
    // 輪郭のかなり外側にあるため、median(rgb) で見ると遠方が余計に濃く出る
    float trueSignedDistance = msd.a;

    float screenPxRange = ScreenPxRange(input.texcoord.xy);

    // ------------------------------------------------------------
    // 縁取りと太さ調整は「しきい値をずらす」だけで作れる。
    // これが距離場を持っていることの効きどころで、
    // ビットマップフォントなら縁取り用のアトラスを別に焼く必要がある。
    //
    // 距離場は輪郭の外側 pxRange/2 px ぶんしか情報を持たないので、
    // ずらせる量には上限がある（kMaxOutlineSd で頭打ちにしている）。
    // 太い縁取りが要るならベイク時の pxRange を上げること。
    //
    // 幅と太さは頂点から来る（テキストごとに違ってよい）。
    // 定数バッファに置くとテキストごとにドローコールが要るため。
    // ------------------------------------------------------------
    float outlineWidthEm = input.style.x;
    float weightEm       = input.style.y;

    float weightSd  = clamp(weightEm       * gBatch.sdUnitsPerEm, -kMaxWeightSd, kMaxWeightSd);
    float outlineSd = clamp(outlineWidthEm * gBatch.sdUnitsPerEm,  0.0f,         kMaxOutlineSd);

    // ------------------------------------------------------------
    // しきい値には下限がある。これを割ると α がクワッドの端で 0 に
    // 落ちきらず、そこで断ち切られて文字のまわりに矩形が出る。
    // 内訳は 2 つ。
    //
    //  0.5 / pxRange       … クワッド最外テクセルの中心は輪郭から
    //                        pxRange/2 - 0.5 px しか離れていないので、
    //                        距離場の値は 0 ではなくここで底を打つ
    //                        （アトラスは RGBA8。範囲外は 0 に飽和する）
    //  0.5 / screenPxRange … AA の立ち上がり半幅。これを跨ぎ切るだけの
    //                        余地が要る。表示サイズで変わるので毎画素で出す
    //
    // しきい値を外側へずらすのは縁取りだけでなく、太さ調整を正に振ったとき
    // （＝太くするとき）の塗りも同じなので、両方に効かせる。
    // 上限（kMaxOutlineSd）が表示サイズに依らない静的な歯止めで、
    // こちらは実際の表示サイズに追従する動的な歯止めにあたる。
    // ------------------------------------------------------------
    float edgeFloor = 0.5f / gBatch.pxRange + 0.5f / screenPxRange;

    float fillEdge    = max(0.5f - weightSd,        edgeFloor);
    float outlineEdge = max(fillEdge - outlineSd,   edgeFloor);

    // 下限で削られた後の実効幅。元の outlineSd で判定すると、
    // 幅 0 まで潰れた縁取りが塗りと同じ α を二重に乗せてしまう
    float effectiveOutlineSd = fillEdge - outlineEdge;

    float fillAlpha    = saturate(screenPxRange * (signedDistance     - fillEdge)    + 0.5f);
    float outlineAlpha = saturate(screenPxRange * (trueSignedDistance - outlineEdge) + 0.5f);

    // 幅 0 のときに縁取りを完全に無効化する。
    // 残しておくと、しきい値が塗りと同一になって輪郭の α が二重に乗る
    outlineAlpha *= step(0.0001f, effectiveOutlineSd);

    float4 fill    = float4(input.color.rgb,        input.color.a        * fillAlpha);
    float4 outline = float4(input.outlineColor.rgb, input.outlineColor.a * outlineAlpha);

    // 塗りを縁取りの上へ source-over で合成する
    float  alpha = fill.a + outline.a * (1.0f - fill.a);
    float3 rgb   = (fill.rgb * fill.a + outline.rgb * outline.a * (1.0f - fill.a))
                 / max(alpha, 1e-5f);

    PixelShaderOutput output;
    output.color = float4(rgb, alpha);
    return output;
}
