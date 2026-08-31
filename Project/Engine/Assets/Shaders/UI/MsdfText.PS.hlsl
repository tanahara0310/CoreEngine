#include "MsdfText.hlsli"

ConstantBuffer<TextBatch> gBatch : register(b0);

// アトラスは Texture2DArray。1 枚が埋まったら次の枚へ送るので、
// 文字列が複数枚にまたがっても 1 ドローコールで描ける
Texture2DArray<float4> gAtlas   : register(t0);
SamplerState           gSampler : register(s0);

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

    // 0.5 が輪郭。これより大きければ字の内側
    float signedDistance = Median(msd.r, msd.g, msd.b);
    float screenPxRange  = ScreenPxRange(input.texcoord.xy);

    // ------------------------------------------------------------
    // 縁取りと太さ調整は「しきい値をずらす」だけで作れる。
    // これが距離場を持っていることの効きどころで、
    // ビットマップフォントなら縁取り用のアトラスを別に焼く必要がある。
    //
    // 距離場は輪郭の外側 pxRange/2 px ぶんしか情報を持たないので、
    // ずらせる量には上限がある（0.45 で頭打ちにしている）。
    // 太い縁取りが要るならベイク時の pxRange を上げること。
    //
    // 幅と太さは頂点から来る（テキストごとに違ってよい）。
    // 定数バッファに置くとテキストごとにドローコールが要るため。
    // ------------------------------------------------------------
    float outlineWidthEm = input.style.x;
    float weightEm       = input.style.y;

    float weightSd  = clamp(weightEm       * gBatch.sdUnitsPerEm, -0.45f, 0.45f);
    float outlineSd = clamp(outlineWidthEm * gBatch.sdUnitsPerEm,  0.0f,  0.45f);

    float fillEdge    = 0.5f - weightSd;
    float outlineEdge = fillEdge - outlineSd;

    float fillAlpha    = saturate(screenPxRange * (signedDistance - fillEdge)    + 0.5f);
    float outlineAlpha = saturate(screenPxRange * (signedDistance - outlineEdge) + 0.5f);

    // 幅 0 のときに縁取りを完全に無効化する。
    // 残しておくと、しきい値が塗りと同一になって輪郭の α が二重に乗る
    outlineAlpha *= step(0.0001f, outlineSd);

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
