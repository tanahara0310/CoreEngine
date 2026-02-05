#pragma once

#include "Engine/Graphics/Render/IRenderer.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>

namespace CoreEngine
{

class Font;
struct SpriteMaterial;
namespace TextRendererRootParam {
    static constexpr UINT kMaterial = 0;
    static constexpr UINT kTransform = 1;
    static constexpr UINT kTexture = 2;
}

/// @brief テキスト描画用レンダラー
class TextRenderer : public IRenderer {
public:
    /// @brief トランスフォーム行列
    struct TransformationMatrix {
        Matrix4x4 WVP;
        Matrix4x4 world;
    };

    static constexpr size_t kMaxGlyphCount = 2048;
    static constexpr UINT kFrameCount = 2;

    void Initialize(ID3D12Device* device) override;
    void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
    void EndPass() override;
    RenderPassType GetRenderPassType() const override { return RenderPassType::Text; }
    void SetCamera(const ICamera* camera) override;

    void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory);

    ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

    size_t GetAvailableConstantBuffer();

    Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const;
    Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation, const ICamera* camera) const;

    DirectXCommon* GetDirectXCommon() { return dxCommon_; }
    ResourceFactory* GetResourceFactory() { return resourceFactory_; }

    std::vector<SpriteMaterial*>& GetMaterialDataPool() { return materialDataPool_[currentFrameIndex_]; }
    std::vector<TransformationMatrix*>& GetTransformDataPool() { return transformDataPool_[currentFrameIndex_]; }

    Microsoft::WRL::ComPtr<ID3D12Resource>& GetMaterialResource(size_t index) { return materialResources_[currentFrameIndex_][index]; }
    Microsoft::WRL::ComPtr<ID3D12Resource>& GetTransformResource(size_t index) { return transformResources_[currentFrameIndex_][index]; }

private:
    std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
    std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
    std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();

    ID3D12PipelineState* pipelineState_ = nullptr;
    BlendMode currentBlendMode_ = BlendMode::kBlendModeAdd;

    DirectXCommon* dxCommon_ = nullptr;
    ResourceFactory* resourceFactory_ = nullptr;

    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> materialResources_[kFrameCount];
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> transformResources_[kFrameCount];
    std::vector<SpriteMaterial*> materialDataPool_[kFrameCount];
    std::vector<TransformationMatrix*> transformDataPool_[kFrameCount];

    size_t currentBufferIndex_ = 0;
    UINT currentFrameIndex_ = 0;
};
}
