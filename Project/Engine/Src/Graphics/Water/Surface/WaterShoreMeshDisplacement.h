#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Graphics/Water/Surface/WaterSurfaceMeshGenerator.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class ShaderProgramCache;
    struct WaterSurfaceData;

    /// @brief 岸の細かい格子の頂点の変位を GPU で書き出す
    /// @details 頂点 (i, j) の変位をテクセル (i, j) に書く（WaterShoreMeshDisplacement.CS.hlsl）。
    ///          RT の水面パスと水面の高さの問い合わせは、描かれた水面の高さをこの値の三角形補間で求める。
    class WaterShoreMeshDisplacement {
    public:
        /// @brief 一辺の頂点の数
        static constexpr uint32_t kVertexCount = WaterSurfaceMeshGenerator::kShoreSubdivisions + 1;

        /// @brief 計算パイプラインとテクスチャを作る
        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            ShaderProgramCache* shaderProgramCache);

        bool IsInitialized() const { return isInitialized_; }

        /// @brief 変位を書くテクスチャの SRV の、シェーダーから見えるヒープの中の番号
        uint32_t GetSrvHeapIndex() const { return srv_.index; }

        /// @brief 岸の格子の範囲の頂点の変位を書く（FFT の波面を計算した後、読む側より前に呼ぶ）
        /// @param surface 岸の格子の範囲と波の状態（shoreMeshEnabled が 0 なら書かない）
        /// @param frameNumber 同じフレームで 2 回書かないための番号
        void Dispatch(ID3D12GraphicsCommandList* cmdList, const WaterSurfaceData& surface, uint64_t frameNumber);

    private:
        bool CreatePipeline();
        bool CreateTexture();

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        bool isInitialized_ = false;

        RootSignatureManager rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        BindingTable bindings_;

        /// @brief 頂点ごとの変位（x, y, z, 0）[m]
        GpuResource texture_{};
        DescriptorHandle srv_{};
        DescriptorHandle uav_{};

        uint64_t lastFrameNumber_ = UINT64_MAX;
    };
}
