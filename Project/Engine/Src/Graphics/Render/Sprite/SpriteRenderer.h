#pragma once
#include "Graphics/Render/BaseRenderer.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Math/MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>


namespace CoreEngine
{

    // 前方宣言
    struct SpriteMaterial;

    /// @brief スプライト描画用レンダラー
    class SpriteRenderer : public BaseRenderer {
    public:
        /// @brief トランスフォーム行列
        struct TransformationMatrix {
            Matrix4x4 WVP;
            Matrix4x4 world;
        };

        static constexpr Cb::Field kTransformationMatrixFields[] = {
            CB_FIELD(TransformationMatrix, WVP), CB_FIELD(TransformationMatrix, world),
        };
        CB_VERIFY_LAYOUT(TransformationMatrix, kTransformationMatrixFields);

        /// @brief 最大スプライト数
        static constexpr size_t kMaxSpriteCount = 1024;

        /// @brief per-frame リソースのリング段数
        /// @details FrameSync のスロット数上限に合わせる。添字は実行時の
        ///          GraphicsCore::Frame().FrameIndex() を使うこと（ここで 2 を直書きすると
        ///          設定の frameCount と食い違う）。
        static constexpr UINT kFrameCount = kMaxFramesInFlight;

        // IRendererインターフェースの実装
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        void EndPass() override;
        RenderPassType GetRenderPassType() const override { return RenderPassType::Sprite; }
        void SetCamera(const Camera* camera) override;

        /// @brief 初期化（GraphicsCoreとResourceFactory付き）
        /// @param dxCommon GraphicsCore
        /// @param resourceFactory ResourceFactory
        void Initialize(GraphicsCore* dxCommon, ResourceFactory* resourceFactory);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief 利用可能な定数バッファのインデックスを取得
        /// @return バッファインデックス
        size_t GetAvailableConstantBuffer();

        /// @brief WVP行列を計算
        /// @param position 位置
        /// @param scale スケール
        /// @param rotation 回転
        /// @return WVP行列
        Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation) const;

        /// @brief WVP 行列を計算（カメラ使用版）
        Matrix4x4 CalculateWVPMatrix(const Vector3& position, const Vector3& scale, const Vector3& rotation, const Camera* camera) const;

        /// @brief GraphicsCoreを取得
        GraphicsCore* GetGraphicsCore() { return dxCommon_; }

        /// @brief ResourceFactoryを取得
        ResourceFactory* GetResourceFactory() { return resourceFactory_; }

        /// @brief マテリアルデータプールを取得
        std::vector<SpriteMaterial*>& GetMaterialDataPool() { return materialDataPool_[currentFrameIndex_]; }

        /// @brief トランスフォームデータプールを取得
        std::vector<TransformationMatrix*>& GetTransformDataPool() { return transformDataPool_[currentFrameIndex_]; }

        /// @brief マテリアルリソースを取得
        Microsoft::WRL::ComPtr<ID3D12Resource>& GetMaterialResource(size_t index) { return materialResources_[currentFrameIndex_][index]; }

        /// @brief トランスフォームリソースを取得
        Microsoft::WRL::ComPtr<ID3D12Resource>& GetTransformResource(size_t index) { return transformResources_[currentFrameIndex_][index]; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        // BaseRenderer から継承したサブシステムを使用（rootSignatureMg_, psoMg_, shaderCompiler_, reflectionBuilder_ は削除）

        // GraphicsCoreとResourceFactory
        GraphicsCore* dxCommon_ = nullptr;
        ResourceFactory* resourceFactory_ = nullptr;

        // 定数バッファプール（フレームごとに分離）
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> materialResources_[kFrameCount];
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> transformResources_[kFrameCount];
        std::vector<SpriteMaterial*> materialDataPool_[kFrameCount];
        std::vector<TransformationMatrix*> transformDataPool_[kFrameCount];

        // 現在のバッファインデックスとフレームインデックス
        size_t currentBufferIndex_ = 0;
        UINT currentFrameIndex_ = 0;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;

        /// @brief パイプラインのみを初期化（Initialize(GraphicsCore*, ResourceFactory*) から呼び出す）
        void InitializePipeline(ID3D12Device* device);

        /// @brief IRenderer::Initialize(ID3D12Device*) のオーバーライド（直接呼び出し禁止）
        void Initialize(ID3D12Device* device) override;
    };
}
