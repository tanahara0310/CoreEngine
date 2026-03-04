#pragma once

#include "Graphics/Render/IRenderer.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Line/Line.h"
#include "Math/MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>

// 前方宣言
class ICamera;


namespace CoreEngine
{

    /// @brief ライン描画用レンダラーパイプライン
    class LineRendererPipeline : public IRenderer {
    public:
        /// @brief ライン頂点データ
        struct LineVertex {
            Vector3 position;
            Vector3 color;
            float alpha;
        };

        /// @brief 最大頂点数
        static constexpr uint32_t kMaxVertexCount = 65536;

        // IRendererインターフェースの実装
        void Initialize(ID3D12Device* device) override;
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        void EndPass() override;
        RenderPassType GetRenderPassType() const override { return RenderPassType::Line; }
        void SetCamera(const ICamera* camera) override;

        /// @brief 初期化（DirectXCommonとResourceFactory付き）
        /// @param dxCommon DirectXCommon
        /// @param resourceFactory ResourceFactory
        void Initialize(DirectXCommon* dxCommon, ResourceFactory* resourceFactory);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief ラインをバッチに追加
        /// @param line ライン
        void AddLine(const Line& line);

        /// @brief 複数のラインをバッチに追加
        /// @param lines ライン配列
        void AddLines(const std::vector<Line>& lines);

        /// @brief バッチをフラッシュして描画
        void FlushBatch();

        /// @brief バッチをクリア
        void ClearBatch();

        /// @brief 頂点バッファを更新（低レベルAPI - 通常は使用しない）
        /// @param vertices 頂点データ
        void UpdateVertexBuffer(const std::vector<LineVertex>& vertices);

        /// @brief ラインを描画（低レベルAPI - 通常は使用しない）
        /// @param cmdList コマンドリスト
        /// @param vertexCount 頂点数
        void DrawLines(ID3D12GraphicsCommandList* cmdList, uint32_t vertexCount);

        /// @brief WVP行列を設定（低レベルAPI - 通常は使用しない）
        /// @param view ビュー行列
        /// @param proj プロジェクション行列
        void SetWVPMatrix(const Matrix4x4& view, const Matrix4x4& proj);

        /// @brief DirectXCommonを取得
        DirectXCommon* GetDirectXCommon() { return dxCommon_; }

        /// @brief ResourceFactoryを取得
        ResourceFactory* GetResourceFactory() { return resourceFactory_; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

        ID3D12PipelineState* pipelineState_ = nullptr;
        BlendMode currentBlendMode_ = BlendMode::kBlendModeNormal;

        // DirectXCommonとResourceFactory
        DirectXCommon* dxCommon_ = nullptr;
        ResourceFactory* resourceFactory_ = nullptr;

        // カメラ
        const ICamera* camera_ = nullptr;

        // 頂点バッファ
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vbView_{};
        std::vector<LineVertex> vertices_;

        // WVP行列バッファ
        Microsoft::WRL::ComPtr<ID3D12Resource> wvpBuffer_;
        Matrix4x4* wvpData_ = nullptr;

        // コマンドリスト（BeginPass/EndPassで使用）
        ID3D12GraphicsCommandList* currentCmdList_ = nullptr;

        // バッチング用
        std::vector<Line> lineBatch_;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;
    };
}
