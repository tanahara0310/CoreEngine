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

/// @brief 空（大気散乱）描画用レンダラー
/// @details 内向きボックスの頂点変換は Skybox.VS.hlsl、
///          空の色は大気散乱（SkyAtmosphere.PS.hlsl）で決まる。
class SkyBoxRenderer : public BaseRenderer {
public:
    // IRendererインターフェースの実装
    void Initialize(ID3D12Device* device) override;
    void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
    void EndPass() override;
    RenderPassType GetRenderPassType() const override { return RenderPassType::SkyBox; }
    void SetCamera(const Camera* camera) override;

    /// @brief ルートシグネチャを取得
    ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

    /// @brief パイプラインが利用可能か
    bool IsPipelineReady() const { return pipelineState_ != nullptr; }

    /// @brief シェーダーリソース名からルートパラメータインデックスを取得
    int GetRootParamIndex(const std::string& resourceName) const;
};
}
