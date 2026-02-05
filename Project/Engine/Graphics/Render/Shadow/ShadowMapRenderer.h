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

    private:
        /// @brief PSOを作成（バイアス設定反映）
        void CreatePipelineStates();

    private:
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> normalModelPSO_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<PipelineStateManager> skinnedModelPSO_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();

        ID3D12Device* device_ = nullptr;
        ID3D12PipelineState* currentPipelineState_ = nullptr;
        ID3D12GraphicsCommandList* cmdList_ = nullptr;

        // バイアス設定
        ShadowBiasSettings biasSettings_;

    // ライトビュープロジェクション行列（CPU側で保持）
    Matrix4x4 lightViewProjection_;
    };
}
