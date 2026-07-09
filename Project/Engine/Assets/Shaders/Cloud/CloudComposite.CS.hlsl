/// @file CloudComposite.CS.hlsl
/// @brief 半解像度の雲バッファをフル解像度 SceneColor へ合成する
/// @details 式: 出力 = 雲色（前乗算） + シーン色 × 透過率。
///          雲バッファはレイマーチ時に深度遮蔽済みなので、ここでは合成のみ行う。
///          出力先は中間テクスチャ（パスが SceneColor へコピーバックする）。

#include "Common/CloudCommon.hlsli"

ConstantBuffer<CloudConstants> gCloud : register(b0);
Texture2D<float4> gSceneColor : register(t0);
Texture2D<float4> gCloudBuffer : register(t1);
Texture2D<float>  gSceneDepth : register(t2);
SamplerState gSamplerLinearWrap : register(s0);
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint width;
    uint height;
    gOutput.GetDimensions(width, height);
    if (dtid.x >= width || dtid.y >= height)
    {
        return;
    }

    float4 scene = gSceneColor[dtid.xy];

    // ===== 深度エッジ対策 =====
    // 半解像度でレイマーチした雲バッファをバイリニアでアップサンプルすると、
    // 手前の不透明物（建物・地形など）の輪郭付近で、隣接する半解像度テクセル間の
    // 「雲あり／雲なし」の値が混ざり、輪郭ににじんだハロが出る。
    // 不透明物が雲層の最近距離（layerBottomAltitudeM）より手前にある画素では
    // 雲を合成しないことで、この境界にじみを避ける（設計書 5.6 の簡易対策）。
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(width, height);
    float ndcDepth = gSceneDepth[dtid.xy];
    bool opaqueBeforeCloudLayer = false;
    if (ndcDepth < 0.9999999f)
    {
        float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
        float4 wp = mul(float4(ndc, ndcDepth, 1.0f), gCloud.invViewProj);
        wp /= wp.w;
        float distToOpaque = length(wp.xyz - gCloud.cameraWorldPos);
        opaqueBeforeCloudLayer = distToOpaque < gCloud.layerBottomAltitudeM;
    }

    if (opaqueBeforeCloudLayer)
    {
        gOutput[dtid.xy] = scene;
        return;
    }

    // 半解像度の雲をテントフィルタでアップサンプル（画面端の WRAP 巻き込みを避けて内側へクランプ）。
    // 単一バイリニアだと、遠方の薄い雲や輪郭で半解像度テクセルの階段（ギザギザ）が
    // そのままフル解像度へ拡大される。中心 + 対角 4 タップの小さなガウス近似で
    // 輪郭を 1 テクセル分だけ均し、階段とレイマーチのジッタ起因のエッジノイズを丸める。
    float2 cloudTexel = 1.0f / float2(gCloud.outputWidth, gCloud.outputHeight);
    float2 halfTexel = 0.5f * cloudTexel;
    float2 uvMin = halfTexel;
    float2 uvMax = 1.0f - halfTexel;
    float2 cloudUv = clamp(uv, uvMin, uvMax);

    float4 cloud = gCloudBuffer.SampleLevel(gSamplerLinearWrap, cloudUv, 0) * 0.5f;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        float2 offset = float2((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f) * cloudTexel;
        float2 tapUv = clamp(cloudUv + offset, uvMin, uvMax);
        cloud += gCloudBuffer.SampleLevel(gSamplerLinearWrap, tapUv, 0) * 0.125f;
    }

    // cloud.a = 透過率。前乗算輝度 cloud.rgb をシーンの上に重ねる。
    gOutput[dtid.xy] = float4(cloud.rgb + scene.rgb * cloud.a, scene.a);
}
