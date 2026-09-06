// ライン描画用の頂点シェーダー
cbuffer Camera : register(b0)
{
    matrix viewProjection;
}

// 面と同一平面上に置かれた線の Z ファイティング対策。
//
// 三角形と線ではラスタライザが深度を求める経路が違うため、数学的に同じ平面上にあっても
// ピクセルごとに数 ULP の誤差が出る。その結果、線が面に飲まれる／出るがピクセル単位で
// 入れ替わってチリチリする（TAA のジッタでサンプル位置が毎フレーム動くのでさらに目立つ）。
//
// D3D の RASTERIZER_DESC::DepthBias は「ワイヤフレームの三角形を除き、点・線には
// 適用されない」仕様なので、LINELIST で描くこのパスは PSO 側では直せない。
// そこでクリップ空間の z を w に比例して引き、透視除算後の NDC 深度が距離によらず
// 一定量だけ手前へ来るようにする。
//
// 深度バッファは D24_UNORM なので量子化幅は 1/2^24 ≒ 6.0e-8。その 150 倍ほどを取れば
// 補間誤差には確実に勝てて、かつ見た目のズレにはならない
// （near=0.1 なら 100m 先で 1m 相当の食い込み。太さ 1px の線では判別できない）。
static const float kDepthBias = 1.0e-5f;

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR;
    float alpha : ALPHA;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float alpha : ALPHA;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    // w を掛けてから引くので、除算後の NDC 深度はどの距離でも kDepthBias だけ手前になる
    output.position.z -= kDepthBias * output.position.w;
    output.color = input.color;
    output.alpha = input.alpha;
    return output;
}
