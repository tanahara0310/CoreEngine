#pragma once
#include "Graphics/Render/BaseRenderer.h"
#include "Graphics/Render/Shadow/ShadowDrawPacket.h"
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
    class ShadowMapRenderer : public BaseRenderer {
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

#ifdef USE_IMGUI
        /// @brief ImGuiでバイアス設定を調整
        void DrawImGui();
#endif

        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief 現在設定されているPSOを取得
        /// @note GPUスキニング(CS)Dispatch後にPSOを復元するために使用する
        ID3D12PipelineState* GetCurrentPipelineState() const { return currentPipelineState_; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

        /// @brief シャドウ描画パケットをバインドして描画コマンドを発行する
        /// Model が組み立てた ShadowDrawPacket を受け取り、IA 設定・gLightTransform の
        /// バインド・DrawIndexedInstanced の呼び出しを行う。
        void BindShadowDrawPacket(ID3D12GraphicsCommandList* cmdList, const ShadowDrawPacket& packet);

    private:
        /// @brief PSOを作成（バイアス設定反映）
        void CreatePipelineStates();

    private:
        // BaseRenderer から rootSignatureMg_, shaderCompiler_, reflectionBuilder_, reflectionData_ を継承

        // シャドウマップは通常モデル用とスキニングモデル用の2つの PSO を持つため、psoMg_ は使わず個別管理
        std::unique_ptr<PipelineStateManager> normalModelPSO_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<PipelineStateManager> skinnedModelPSO_ = std::make_unique<PipelineStateManager>();

        ID3D12Device* device_ = nullptr;
        ID3D12PipelineState* currentPipelineState_ = nullptr;
        ID3D12GraphicsCommandList* cmdList_ = nullptr;

        // バイアス設定
        ShadowBiasSettings biasSettings_;

        // ライトビュープロジェクション行列（CPU側で保持）
        Matrix4x4 lightViewProjection_;
    };
}
