// FFT Ocean 法線マップのミップチェーン生成（2x2 ボックスダウンサンプル）
// 法線マップがミップ無しのままだと、遠方・かすめ角で法線がピクセル毎に暴れて
// フレネル反射率がスペックル化する（水面の白い泡状ノイズや色まだらの原因）。
// エンコード値 [0,1] を [-1,1] に復号してから平均・再正規化することで、
// ダウンサンプル後も単位長の法線を保つ。
Texture2D<float4> gSourceNormalMip : register(t0);
RWTexture2D<float4> gDestNormalMip : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint destWidth = 0;
    uint destHeight = 0;
    gDestNormalMip.GetDimensions(destWidth, destHeight);
    if (dispatchThreadId.x >= destWidth || dispatchThreadId.y >= destHeight)
    {
        return;
    }

    // 解像度は 2 のべき乗（IFFT の前提）なので、ソースは常にちょうど 2 倍のサイズ。
    const int2 baseCoord = int2(dispatchThreadId.xy) * 2;

    float3 accumulated = float3(0.0f, 0.0f, 0.0f);
    [unroll]
    for (int offsetY = 0; offsetY < 2; ++offsetY)
    {
        [unroll]
        for (int offsetX = 0; offsetX < 2; ++offsetX)
        {
            const float3 encodedNormal = gSourceNormalMip.Load(int3(baseCoord + int2(offsetX, offsetY), 0)).xyz;
            accumulated += encodedNormal * 2.0f - 1.0f;
        }
    }

    // 相殺して長さがほぼゼロになった場合は真上へフォールバックする
    float3 averagedNormal = (length(accumulated) < 1.0e-4f)
        ? float3(0.0f, 1.0f, 0.0f)
        : normalize(accumulated);

    gDestNormalMip[dispatchThreadId.xy] = float4(averagedNormal * 0.5f + 0.5f, 1.0f);
}
