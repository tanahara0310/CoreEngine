// ============================================================
// MSDF テキスト描画（3D ワールド空間） 共通定義
// ============================================================
// UI 版（UI/MsdfText.hlsli）との違いは「頂点に何が焼き込まれているか」だけ。
// UI 版はスクリーン px、こちらはワールド座標。
// テキストごとの色・縁取り・ワールド行列は頂点へ焼き込んであるので、
// 何個並べてもドローコールは 1 本にまとまる（詳細は Text3DRenderer を参照）。
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
struct Text3DBatch
{
    // ワールド → クリップ空間
    // ワールド行列は頂点へ焼き込み済みなので、ここに置くのはビュー射影だけでよい
    float4x4 viewProjection;

    // アトラスを焼いたときの距離場の有効範囲（px）
    float pxRange;
    // アトラス 1 枚あたりの画素サイズ
    float atlasWidth;
    float atlasHeight;
    // em 単位の長さを距離場の値へ換算する係数（= glyphPixelSize / pxRange）
    float sdUnitsPerEm;
};
