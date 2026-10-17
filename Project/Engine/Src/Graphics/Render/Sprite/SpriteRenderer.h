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

        // IRendererインターフェースの実装
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        void EndPass() override;
        RenderPassType GetRenderPassType() const override { return RenderPassType::Sprite; }
        void SetCamera(const Camera* camera) override;

        /// @brief 初期化（GraphicsCore 付き）
        /// @param dxCommon GraphicsCore
        void Initialize(GraphicsCore* dxCommon);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief GraphicsCoreを取得
        GraphicsCore* GetGraphicsCore() { return dxCommon_; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        // BaseRenderer から継承したサブシステムを使用（rootSignatureMg_, psoMg_, shaderCompiler_, reflectionBuilder_ は削除）

        GraphicsCore* dxCommon_ = nullptr;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;

        /// @brief パイプラインのみを初期化（Initialize(GraphicsCore*) から呼び出す）
        void InitializePipeline(ID3D12Device* device);

        /// @brief IRenderer::Initialize(ID3D12Device*) のオーバーライド（直接呼び出し禁止）
        void Initialize(ID3D12Device* device) override;
    };
}
