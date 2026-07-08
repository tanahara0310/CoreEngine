#pragma once
#include "Graphics/Render/BaseRenderer.h"
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
class SkyBoxRenderer : public BaseRenderer {
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

    // ===== 大気散乱モード（SkyAtmosphere.PS.hlsl） =====
    // キューブマップの代わりに大気散乱で空を描く第2パイプライン。
    // メッシュ・VS・深度設定はキューブマップ版と共通。

    /// @brief 大気散乱用パイプラインが利用可能か
    bool IsAtmospherePipelineReady() const { return atmospherePipelineState_ != nullptr; }

    /// @brief 大気散乱用ルートシグネチャを取得
    ID3D12RootSignature* GetAtmosphereRootSignature() const { return atmosphereRootSignatureMg_->GetRootSignature(); }

    /// @brief 大気散乱用パイプラインステートを取得
    ID3D12PipelineState* GetAtmospherePipelineState() const { return atmospherePipelineState_; }

    /// @brief 大気散乱用シェーダーのリソース名からルートパラメータインデックスを取得
    int GetAtmosphereRootParamIndex(const std::string& resourceName) const;

private:
    // 大気散乱モード用サブシステム（キューブマップ版とルートシグネチャ構成が異なるため別管理）
    std::unique_ptr<RootSignatureManager> atmosphereRootSignatureMg_ = std::make_unique<RootSignatureManager>();
    std::unique_ptr<PipelineStateManager> atmospherePsoMg_ = std::make_unique<PipelineStateManager>();
    std::unique_ptr<ShaderReflectionData> atmosphereReflectionData_;
    ID3D12PipelineState* atmospherePipelineState_ = nullptr;
};
}
