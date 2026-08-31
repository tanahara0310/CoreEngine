// ============================================================
// MSDF テキスト描画 共通定義
// ============================================================
// テキストごとの色・縁取り・行列は頂点へ焼き込んである。
// そのおかげで、何個テキストを並べてもドローコールは 1 本にまとまる
// （バッチングの詳細は TextRenderer を参照）。
struct VertexShaderOutput
{
    float4 position     : SV_POSITION;
    // xy = アトラス UV / z = アトラス配列の何枚目か
    float3 texcoord     : TEXCOORD0;
    float4 color        : COLOR0;
    float4 outlineColor : COLOR1;
    // x = 縁取り幅（em） / y = 太さ調整（em）
    float2 style        : TEXCOORD1;
};

// バッチのあいだ変わらない定数だけを置く
struct TextBatch
{
    // スクリーン px → クリップ空間
    float4x4 projection;

    // アトラスを焼いたときの距離場の有効範囲（px）
    float pxRange;
    // アトラス 1 枚あたりの画素サイズ
    float atlasWidth;
    float atlasHeight;
    // em 単位の長さを距離場の値へ換算する係数（= glyphPixelSize / pxRange）
    float sdUnitsPerEm;
};
