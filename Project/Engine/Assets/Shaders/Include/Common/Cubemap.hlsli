/// @file Cubemap.hlsli
/// @brief キューブマップの面/UV ↔ 方向ベクトル変換
/// @details IBL プリフィルタ・空キャプチャ・雲キャプチャがそれぞれ同じ switch 文を
///          持っていた。面の並びが 1 つでもずれると環境マップが破綻するので集約する。

#ifndef CUBEMAP_HLSLI
#define CUBEMAP_HLSLI

/// @brief キューブマップ面IDと面内UVから方向ベクトルを求める（DirectX 標準の面順）
/// @param faceIndex 面ID（0:+X 1:-X 2:+Y 3:-Y 4:+Z 5:-Z）
/// @param uv 面内UV（原点左上・[0,1]）
/// @return 正規化済みの方向ベクトル
float3 GetCubemapDirection(uint faceIndex, float2 uv)
{
    // UV を [-1,1] へ（中心が (0,0)）
    float2 texCoord = uv * 2.0f - 1.0f;

    float3 dir;
    switch (faceIndex)
    {
        case 0: dir = float3(1.0f, -texCoord.y, -texCoord.x); break;  // +X (右)
        case 1: dir = float3(-1.0f, -texCoord.y, texCoord.x); break;  // -X (左)
        case 2: dir = float3(texCoord.x, 1.0f, texCoord.y); break;    // +Y (上)
        case 3: dir = float3(texCoord.x, -1.0f, -texCoord.y); break;  // -Y (下)
        case 4: dir = float3(texCoord.x, -texCoord.y, 1.0f); break;   // +Z (前)
        case 5: dir = float3(-texCoord.x, -texCoord.y, -1.0f); break; // -Z (後)
        default: dir = float3(0.0f, 1.0f, 0.0f); break;
    }
    return normalize(dir);
}

#endif // CUBEMAP_HLSLI
