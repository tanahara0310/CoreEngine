#pragma once

#include "ParticleRenderer.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>

// 前方宣言
namespace CoreEngine {
    class GpuParticleSystem;
}

/// @brief GPUパーティクル専用レンダラー
/// ParticleRenderer の描画基盤（Particle.VS/PS・PSO・共有頂点バッファ）を継承し、
/// 描画前に Emit / Update の ComputeShader をディスパッチする。
/// 設計は Docs/Engine/Particle/GpuParticleSystem.md 参照。

namespace CoreEngine
{
class GpuParticleRenderer : public ParticleRenderer {
public:
    GpuParticleRenderer() = default;
    ~GpuParticleRenderer() override = default;

    /// @brief 初期化（基底クラス + Compute パイプライン構築）
    /// @param device D3D12デバイス
    void Initialize(ID3D12Device* device) override;

    /// @brief このレンダラーがサポートする描画タイプを取得
    RenderPassType GetRenderPassType() const override { return RenderPassType::GpuParticle; }

    /// @brief GPUパーティクルシステムを描画（CSディスパッチ + DrawInstanced）
    /// @param system GPUパーティクルシステム
    void DrawGpu(GpuParticleSystem* system);

private:
    /// @brief CS 1本分のパイプライン（RootSignature + PSO + ルートパラメータインデックス）
    struct ComputePass {
        std::unique_ptr<RootSignatureManager> rootSignatureMg = std::make_unique<RootSignatureManager>();
        std::unique_ptr<ShaderReflectionData> reflectionData;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;

        int GetRootParamIndex(const std::string& name) const;
    };

    /// @brief CSをコンパイルしてComputePassを構築
    void CreateComputePass(ID3D12Device* device, ComputePass& pass, const wchar_t* shaderPath, const char* debugName);

    /// @brief Emit / Update CS のディスパッチとカウンタコピー
    void DispatchCompute(GpuParticleSystem* system);

    ComputePass emitPass_;
    ComputePass updatePass_;

    // ExecuteIndirect 用コマンドシグネチャ（DRAW 1個・ルートシグネチャなし）
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> commandSignature_;

    // Emit CS のルートパラメータインデックス
    int emitParticlesIdx_ = -1;
    int emitCounterIdx_ = -1;
    int emitFreeListIdx_ = -1;
    int emitParamsIdx_ = -1;

    // Update CS のルートパラメータインデックス
    int updateParticlesIdx_ = -1;
    int updateInstancingIdx_ = -1;
    int updateCounterIdx_ = -1;
    int updateFreeListIdx_ = -1;
    int updateParamsIdx_ = -1;
};
}
