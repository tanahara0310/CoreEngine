#pragma once

#include "Graphics/Water/Surface/WaterSurfaceTypes.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "WaterRenderResources.h"

#include <d3d12.h>

namespace CoreEngine
{
    class CustomShaderPipeline;

/// @brief 水面シェーダ向けの GPU リソースバインド補助
/// @details WaterPlaneObject から SRV / CBV バインドの詳細を分離するためのクラス。
///          宣言表（WaterBind::kDecls）をパイプラインごとに 1 回解決して保持するので、
///          描画中に名前で map を引くことはない。
class WaterShaderResourceBinder {
public:
    /// @brief 水面描画に必要な定数バッファと SRV をコマンドリストへバインドする
    /// @note pipeline が差し替わったら（FFT ON/OFF の切り替え等）宣言表を再解決する
    void Bind(
        ID3D12GraphicsCommandList* cmdList,
        const CustomShaderPipeline* pipeline,
        D3D12_GPU_VIRTUAL_ADDRESS waterCBGpuAddress,
        D3D12_GPU_VIRTUAL_ADDRESS frameCBGpuAddress,
        const WaterRenderResources& renderResources);

private:
    /// @brief 必要なら宣言表を解決し直す
    /// @details 再解決の判定は RootSignature ポインタで行う。パイプラインを作り直すと
    ///          RootSignature も作り直されるので、ポインタ一致なら同じビルドだと言える。
    void EnsureResolved(const CustomShaderPipeline* pipeline);

    BindingTable table_;
    const void* resolvedRootSignature_ = nullptr;
};
}
