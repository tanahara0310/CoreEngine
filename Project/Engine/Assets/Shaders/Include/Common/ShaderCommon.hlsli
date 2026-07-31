/// @file ShaderCommon.hlsli
/// @brief シェーダー共通ヘッダの一括 include 用ファサード
/// @details 何を使うか決まっているなら個別ヘッダを直接 include したほうが依存が明確になる。
///          「とりあえず共通のものが欲しい」場合だけこちらを使うこと。
///          深度復元（DepthReconstruction.hlsli）は cbuffer 由来の行列を前提とする
///          用途限定のヘルパーなので、ここには含めず必要な場所で個別に include する。

#ifndef SHADER_COMMON_HLSLI
#define SHADER_COMMON_HLSLI

#include "ShaderMath.hlsli"
#include "ColorSpace.hlsli"
#include "Hash.hlsli"
#include "Sampling.hlsli"
#include "Cubemap.hlsli"

#endif // SHADER_COMMON_HLSLI
