#pragma once
#include "Graphics/Render/IRenderer.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

namespace CoreEngine
{
// 前方宣言
class ShaderReflectionData;

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

    /// @brief シェーダーリソース名からルートパラメータインデックスを取得
    int GetRootParamIndex(const std::string& resourceName) const;
    
private:
    std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
    std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
    std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
    std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();
    
    ID3D12PipelineState* pipelineState_ = nullptr;

    // シェーダーリフレクションデータ
    std::unique_ptr<ShaderReflectionData> reflectionData_;
};
}
