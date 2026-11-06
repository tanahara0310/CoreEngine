// SceneDepth.hlsli
// 画面の深度を読む（シェーダーグラフの Scene Depth・Scene Depth Difference・Screen）。
// 深度はふつうの Z（近い所が 0・遠い所が 1）。深度を読めない所（3D の不透明・UI 画像・プレビュー）には、
// エンジンが「奥に何も無い」1×1 のテクスチャ（値が 1）を差す。
// 使わなければ資源は DXIL に残らない。
#ifndef SCENE_DEPTH_HLSLI
#define SCENE_DEPTH_HLSLI

/// @brief 深度を直す値（C++ 側 SceneDepth::ShaderParams と一致させる）
struct SceneDepthParams
{
    float nearZ; ///< カメラの near（m）
    float farZ; ///< カメラの far（m）
    float2 screenSize; ///< 画面の大きさ（px）
    float orthographic; ///< 1 = 平行投影
    float3 padding;
};

Texture2D<float> gSceneDepthTexture : register(t0, space4);
ConstantBuffer<SceneDepthParams> gSceneDepthParams : register(b0, space4);

/// @brief 画面の大きさ（px）
float2 SceneScreenSize()
{
    return gSceneDepthParams.screenSize;
}

/// @brief 画素の位置（px）を画面の UV（0〜1・左上が原点）にする
float2 SceneScreenUV(float2 pixel)
{
    return pixel / max(SceneScreenSize(), float2(1.0f, 1.0f));
}

/// @brief 画面の UV（0〜1・左上が原点）の深度の値（0〜1）
float SceneDepthRaw(float2 uv)
{
    uint width;
    uint height;
    gSceneDepthTexture.GetDimensions(width, height);
    const int2 size = int2(width, height);
    const int2 pixel = clamp(int2(floor(uv * float2(size))), int2(0, 0), size - 1);
    return gSceneDepthTexture.Load(int3(pixel, 0));
}

/// @brief 深度の値を目からの距離（m）にする
float SceneDepthEye(float rawDepth)
{
    const float nearZ = gSceneDepthParams.nearZ;
    const float farZ = gSceneDepthParams.farZ;
    if (gSceneDepthParams.orthographic != 0.0f)
    {
        return nearZ + rawDepth * (farZ - nearZ);
    }
    return nearZ * farZ / max(farZ - rawDepth * (farZ - nearZ), 1.0e-6f);
}

/// @brief 深度の値を 0〜1 の線形の深度（目からの距離 ÷ far）にする
float SceneDepthLinear01(float rawDepth)
{
    return SceneDepthEye(rawDepth) / max(gSceneDepthParams.farZ, 1.0e-6f);
}

/// @brief 画面の UV の奥の物と、この面（深度の値 surfaceRawDepth）の、目からの距離の差（m）
float SceneDepthDifferenceEye(float2 uv, float surfaceRawDepth)
{
    return SceneDepthEye(SceneDepthRaw(uv)) - SceneDepthEye(surfaceRawDepth);
}

/// @brief 画面の UV の奥の物と、この面の、0〜1 の線形の深度の差
float SceneDepthDifferenceLinear01(float2 uv, float surfaceRawDepth)
{
    return SceneDepthDifferenceEye(uv, surfaceRawDepth) / max(gSceneDepthParams.farZ, 1.0e-6f);
}

#endif // SCENE_DEPTH_HLSLI
