#pragma once
#include "Graphics/Render/IRenderer.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Math/Matrix/Matrix4x4.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>

namespace CoreEngine
{
    // 前方宣言
    class ShaderReflectionData;

    /// @brief シャドウバイアス設定
    struct ShadowBiasSettings {
        int depthBias = 3000;              // 固定バイアス
        float slopeScaledDepthBias = 3.0f; // 傾斜スケールバイアス
        float depthBiasClamp = 0.0f;       // バイアスクランプ
    };

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

        /// @brief バイアス設定を変更（PSOを再作成）
        /// @param settings 新しいバイアス設定
        void SetBiasSettings(const ShadowBiasSettings& settings);

        /// @brief 現在のバイアス設定を取得
        /// @return バイアス設定
        const ShadowBiasSettings& GetBiasSettings() const { return biasSettings_; }

#ifdef _DEBUG
        /// @brief ImGuiでバイアス設定を調整
        void DrawImGui();
#endif

        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        /// @brief PSOを作成（バイアス設定反映）
        void CreatePipelineStates();

    private:
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> normalModelPSO_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<PipelineStateManager> skinnedModelPSO_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

        ID3D12Device* device_ = nullptr;
        ID3D12PipelineState* currentPipelineState_ = nullptr;
        ID3D12GraphicsCommandList* cmdList_ = nullptr;

        // バイアス設定
        ShadowBiasSettings biasSettings_;

        // ライトビュープロジェクション行列（CPU側で保持）
        Matrix4x4 lightViewProjection_;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;
    };
}
