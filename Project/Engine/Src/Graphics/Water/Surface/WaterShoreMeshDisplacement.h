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

    /// @brief 水面メッシュの頂点の変位を GPU で書き出す（岸の細かい格子と粗い格子）
    /// @details 岸の格子の頂点 (i, j) の変位と、沖から寄せる波の傾き・砕けた波の泡・浜を上がった膜の濡れ跡・
    ///          浜を上がる膜が運ぶ水の粒のずれをテクセル (i, j) に書く
    ///          （WaterShoreMeshDisplacement.CS.hlsl）。FFTWater.VS は岸の格子の頂点をこの値で動かす。
    ///          粗い格子の頂点 (i, j) の変位はテクセル (i, j) に書く（WaterCoarseMeshDisplacement.CS.hlsl）。
    ///          RT の水面パスと水面の高さの問い合わせは、描かれた水面の高さをこれらの値の三角形補間で求める。
    class WaterShoreMeshDisplacement {
    public:
        /// @brief 岸の格子の一辺の頂点の数
        static constexpr uint32_t kVertexCount = WaterSurfaceMeshGenerator::kShoreSubdivisions + 1;

        /// @brief 計算パイプラインとテクスチャを作る
        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            ShaderProgramCache* shaderProgramCache);

        bool IsInitialized() const { return isInitialized_; }

        /// @brief 岸の格子の変位を書くテクスチャの SRV の、シェーダーから見えるヒープの中の番号
        uint32_t GetSrvHeapIndex() const { return srv_.index; }

        /// @brief 岸の格子の変位（xyz）と物が起こした波紋の高さ（w）のテクスチャの SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetDisplacementSRV() const { return srv_.gpuHandle; }

        /// @brief 岸の格子の沖から寄せる波の傾き（xy）と砕けた波の泡（zw）のテクスチャの SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetSurfaceSRV() const { return surfaceSrv_.gpuHandle; }

        /// @brief 岸の格子の浜を上がった膜の濡れ跡（x = 砂の暗さ・y = 砂のつや、z = 今の静水面からの砂の高さ）のテクスチャの SRV
        /// @details ピクセルシェーダーからも読める状態で置く
        D3D12_GPU_DESCRIPTOR_HANDLE GetWetnessSRV() const { return wetnessSrv_.gpuHandle; }

        /// @brief 岸の格子の浜を上がる膜が運ぶ水の粒の水平のずれ（x, z）[m] のテクスチャの SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetFlowSRV() const { return flowSrv_.gpuHandle; }

        /// @brief 粗い格子の一辺の分割数に合わせて、粗い格子の変位を書くテクスチャを用意する
        /// @details 大きさが変わるときは GPU の処理を待ってから作り直す
        /// @return テクスチャがあれば true
        bool EnsureCoarseMesh(uint32_t subdivisions);

        /// @brief 粗い格子の変位を書くテクスチャの SRV の、シェーダーから見えるヒープの中の番号
        uint32_t GetCoarseSrvHeapIndex() const { return coarseSrv_.index; }

        /// @brief 粗い格子の変位を書くテクスチャの一辺の頂点の数（0 = テクスチャなし）
        uint32_t GetCoarseVertexCount() const { return coarseVertexCount_; }

        /// @brief 粗い格子と岸の格子の範囲の頂点の変位を書く（FFT の波面と波紋を計算した後、読む側より前に呼ぶ）
        /// @param surface 格子の範囲と波の状態（粗い格子は水面メッシュの範囲と同じ分割数のテクスチャがあるとき、
        ///                岸の格子は shoreMeshEnabled が 1 のときだけ書く）
        /// @param frameNumber 同じフレームで 2 回書かないための番号
        void Dispatch(ID3D12GraphicsCommandList* cmdList, const WaterSurfaceData& surface, uint64_t frameNumber);

    private:
        bool CreatePipelines();
        bool CreateTextures();

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        bool isInitialized_ = false;

        RootSignatureManager rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        BindingTable bindings_;

        RootSignatureManager coarseRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> coarsePipelineState_;
        BindingTable coarseBindings_;

        /// @brief 岸の格子の頂点ごとの変位（x, y, z）と物が起こした波紋の高さ（w）[m]
        GpuResource texture_{};
        DescriptorHandle srv_{};
        DescriptorHandle uav_{};

        /// @brief 岸の格子の頂点ごとの沖から寄せる波の傾き（x, y）と砕けた波の泡（z, w）
        GpuResource surfaceTexture_{};
        DescriptorHandle surfaceSrv_{};
        DescriptorHandle surfaceUav_{};

        /// @brief 岸の格子の頂点ごとの浜を上がった膜の濡れ跡（x = 砂の暗さ・y = 砂のつや [0,1]、z = 今の静水面からの砂の高さ [m]）
        GpuResource wetnessTexture_{};
        DescriptorHandle wetnessSrv_{};
        DescriptorHandle wetnessUav_{};

        /// @brief 岸の格子の頂点ごとの浜を上がる膜が運ぶ水の粒の水平のずれ（x, z）[m]
        GpuResource flowTexture_{};
        DescriptorHandle flowSrv_{};
        DescriptorHandle flowUav_{};

        /// @brief 粗い格子の頂点ごとの変位（x, y, z）と物が起こした波紋の高さ（w）[m]
        GpuResource coarseTexture_{};
        DescriptorHandle coarseSrv_{};
        DescriptorHandle coarseUav_{};
        uint32_t coarseVertexCount_ = 0;

        uint64_t lastFrameNumber_ = UINT64_MAX;
    };
}
