#pragma once

#include "Graphics/Render/IRenderer.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Line/Line.h"
#include "Math/MathCore.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <vector>

// 前方宣言
class Camera;


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
        void SetCamera(const Camera* camera) override;

        /// @brief 初期化（GraphicsCoreとResourceFactory付き）
        /// @param dxCommon GraphicsCore
        /// @param resourceFactory ResourceFactory
        void Initialize(GraphicsCore* dxCommon, ResourceFactory* resourceFactory);

        /// @brief ルートシグネチャを取得
        ID3D12RootSignature* GetRootSignature() const { return rootSignatureMg_->GetRootSignature(); }

        /// @brief ラインをバッチに追加
        /// @param line ライン
        void AddLine(const Line& line);

        /// @brief 複数のラインをバッチに追加
        /// @param lines ライン配列
        void AddLines(const std::vector<Line>& lines);

        // ===== ラインソース =====
        // グリッドやコライダーワイヤのような「毎フレーム生成する線」の供給元。
        // EndPass のフラッシュ直前に SubmitLines が呼ばれる（ビューごと・パスのカメラ付き）。
        // 登録した側は寿命が尽きる前に必ず Unregister すること。

        /// @brief ラインソースを登録する
        void RegisterLineSource(class ILineSource* source);

        /// @brief ラインソースの登録を解除する
        void UnregisterLineSource(class ILineSource* source);

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
        /// @param startVertexLocation 頂点バッファ内の開始位置（深度あり／なしを分けて描くため）
        void DrawLines(ID3D12GraphicsCommandList* cmdList, uint32_t vertexCount,
            uint32_t startVertexLocation = 0);

        /// @brief WVP行列を設定（低レベルAPI - 通常は使用しない）
        /// @param view ビュー行列
        /// @param proj プロジェクション行列
        void SetWVPMatrix(const Matrix4x4& view, const Matrix4x4& proj);

        /// @brief GraphicsCoreを取得
        GraphicsCore* GetGraphicsCore() { return dxCommon_; }

        /// @brief ResourceFactoryを取得
        ResourceFactory* GetResourceFactory() { return resourceFactory_; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    private:
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();

        // 深度テストを行わない PSO（Line::depthTest == false のライン用）。
        // 骨のデバッグ表示のようにメッシュ内部にある線を隠さず見せるために使う。
        std::unique_ptr<PipelineStateManager> overlayPsoMg_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

        ID3D12PipelineState* pipelineState_ = nullptr;
        BlendMode currentBlendMode_ = BlendMode::kBlendModeNormal;

        // GraphicsCoreとResourceFactory
        GraphicsCore* dxCommon_ = nullptr;
        ResourceFactory* resourceFactory_ = nullptr;

        // カメラ
        const Camera* camera_ = nullptr;

        // 登録されたラインソース（所有権は持たない。登録側が Unregister する契約）
        std::vector<class ILineSource*> lineSources_;

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
