#pragma once
#include "Engine/Graphics/Render/IRenderer.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include "Engine/Graphics/RootSignatureManager.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Math/Matrix/Matrix4x4.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>

// Root Parameter インデックス定数

namespace CoreEngine
{
	namespace ShadowMapRendererRootParam {
		static constexpr UINT kLightTransform = 0;        // b0: LightTransform (VS)
		static constexpr UINT kMatrixPalette = 1;         // t0: MatrixPalette (VS) - スキニング用
	}

	/// @brief シャドウマップ生成用レンダラー
	/// @note 通常モデルとスキニングモデルの両方に対応
	class ShadowMapRenderer : public IRenderer {
	public:
		void Initialize(ID3D12Device* device) override;
		void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
		void EndPass() override;
		RenderPassType GetRenderPassType() const override { return RenderPassType::ShadowMap; }
		void SetCamera(const ICamera* camera) override;

	/// @brief ライトビュープロジェクション行列を設定
	/// @param lightViewProjection ライトから見たビュープロジェクション行列
	void SetLightViewProjection(const Matrix4x4& lightViewProjection);

		/// @brief 通常モデル用のPSOを設定
		void SetPSOForNormalModel();

		/// @brief スキニングモデル用のPSOを設定
		void SetPSOForSkinnedModel();

		ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

	private:
		std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
		std::unique_ptr<PipelineStateManager> normalModelPSO_ = std::make_unique<PipelineStateManager>();
		std::unique_ptr<PipelineStateManager> skinnedModelPSO_ = std::make_unique<PipelineStateManager>();
		std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();

		ID3D12PipelineState* currentPipelineState_ = nullptr;
		ID3D12GraphicsCommandList* cmdList_ = nullptr;

	// ライトビュープロジェクション行列（CPU側で保持）
	Matrix4x4 lightViewProjection_;
	};
}
