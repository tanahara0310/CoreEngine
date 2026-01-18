#pragma once
#include "Engine/Graphics/Render/IRenderer.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>

// SkyBox用 Root Parameter インデックス定数

namespace CoreEngine
{
namespace SkyBoxRendererRootParam {
    static constexpr UINT kWVP = 0;          // トランスフォーム用CBV (b0, VS)
    static constexpr UINT kMaterial = 1;     // マテリアル用CBV (b0, PS)
    static constexpr UINT kTexture = 2;      // テクスチャ用SRV (t0, PS)
}

/// @brief SkyBox描画用レンダラー
class SkyBoxRenderer : public IRenderer {
public:
    // IRendererインターフェースの実装
    void Initialize(ID3D12Device* device) override;
    void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
    void EndPass() override;
    RenderPassType GetRenderPassType() const override { return RenderPassType::SkyBox; }
    void SetCamera(const ICamera* camera) override;
    
    /// @brief ルートシグネチャを取得
    ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }
    
private:
    std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
    std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
    std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
    
    ID3D12PipelineState* pipelineState_ = nullptr;
};
}
