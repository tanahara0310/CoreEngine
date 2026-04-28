#pragma once
#include "Graphics/Render/IRenderer.h"
#include "Graphics/Render/UI/UIMaterial.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Math/MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>

namespace CoreEngine
{
    /// @brief UI 描画専用レンダラー（カメラ非依存・スクリーン固定座標）
    /// @details
    ///  - Camera2D を使わず、画面ピクセル座標（左上原点・Y軸下正）の正射影で描画する
    ///  - UI パスは描画パイプラインの最後に呼び出され、常に最前面となる
    ///  - 内部実装は SpriteRenderer と類似しているが、PSO/RootSignature/定数バッファプールを
    ///    完全に独立させている
    class UIRenderer : public IRenderer {
    public:
        /// @brief トランスフォーム行列（HLSL 側 cbuffer と一致）
        struct TransformationMatrix {
            Matrix4x4 WVP;
            Matrix4x4 world;
        };

        /// @brief 最大 UI 要素数
        static constexpr size_t kMaxUICount = 1024;

        /// @brief フレーム数（ダブルバッファリング）
        static constexpr UINT kFrameCount = 2;

        // ===== IRenderer インターフェース =====
        void Initialize(ID3D12Device* device) override;
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        void EndPass() override;
        RenderPassType GetRenderPassType() const override { return RenderPassType::UI; }
        void SetCamera(const ICamera* camera) override;

        /// @brief 初期化（DirectXCommon と ResourceFactory 付き）
        void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief 利用可能な定数バッファのインデックスを取得
        size_t GetAvailableConstantBuffer();

        /// @brief WVP 行列を計算（スクリーン固定座標系）
        /// @param position UI 要素のスクリーン座標（左上原点・ピクセル）
        /// @param scale    スケール
        /// @param rotation 回転（ラジアン）
        Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const;

        /// @brief 基準解像度を設定（可変対応）
        /// @note 0 以下の場合はウィンドウサイズに自動追従する
        void SetReferenceResolution(float width, float height);

        /// @brief 現在使用中のスクリーンサイズを取得
        Vector2 GetScreenSize() const;

        /// @brief DirectXCommon を取得
        DirectXCommon* GetDirectXCommon() { return dxCommon_; }

        /// @brief ResourceFactory を取得
        ResourceFactory* GetResourceFactory() { return resourceFactory_; }

        /// @brief マテリアルデータプールを取得
        std::vector<UIMaterial*>& GetMaterialDataPool() { return materialDataPool_[currentFrameIndex_]; }

        /// @brief トランスフォームデータプールを取得
        std::vector<TransformationMatrix*>& GetTransformDataPool() { return transformDataPool_[currentFrameIndex_]; }

        /// @brief マテリアルリソースを取得
        Microsoft::WRL::ComPtr<ID3D12Resource>& GetMaterialResource(size_t index) { return materialResources_[currentFrameIndex_][index]; }

        /// @brief トランスフォームリソースを取得
        Microsoft::WRL::ComPtr<ID3D12Resource>& GetTransformResource(size_t index) { return transformResources_[currentFrameIndex_][index]; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

        ID3D12PipelineState* pipelineState_ = nullptr;
        BlendMode currentBlendMode_ = BlendMode::kBlendModeNormal;

        DirectXCommon* dxCommon_ = nullptr;
        ResourceFactory* resourceFactory_ = nullptr;

        // 定数バッファプール（フレームごとに分離）
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> materialResources_[kFrameCount];
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> transformResources_[kFrameCount];
        std::vector<UIMaterial*> materialDataPool_[kFrameCount];
        std::vector<TransformationMatrix*> transformDataPool_[kFrameCount];

        size_t currentBufferIndex_ = 0;
        UINT   currentFrameIndex_ = 0;

        std::unique_ptr<ShaderReflectionData> reflectionData_;

        // 基準解像度（0 以下の場合はウィンドウサイズに追従）
        float referenceWidth_ = 0.0f;
        float referenceHeight_ = 0.0f;
    };
}
