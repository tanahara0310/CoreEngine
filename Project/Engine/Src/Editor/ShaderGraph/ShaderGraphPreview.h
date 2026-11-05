#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/RootSignature/RootSlot.h"

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class EngineSystem;
    class PipelineStateManager;
    class RenderTarget;
    class RootSignatureManager;
    class SceneDepth;
    class ShaderCompiler;
    class ShaderReflectionBuilder;
    class ShaderReflectionData;
    struct VertexData;
}

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief 3D の Main Preview に置く形
    enum class PreviewShape : uint8_t { Sphere, Cube, Plane };

    /// @brief 3D の Main Preview の見せ方
    struct PreviewView
    {
        PreviewShape shape = PreviewShape::Sphere;
        float yaw = 0.0f;   ///< 縦軸まわりの回転（ラジアン）
        float pitch = 0.0f; ///< 横軸まわりの回転（ラジアン）
    };

    /// @brief 形の表示名
    const char* DisplayName(PreviewShape shape);

    /// @brief グラフの Main Preview（画面外の 256×256 のテクスチャへ描く）
    /// @details スプライトは板 1 枚を描き、ポストエフェクトはコンピュートシェーダで全部の画素を書く。
    ///          3D は球・立方体・平面のどれかを、決まったライト 1 つと環境光で照らして描く。
    ///          窓を描くときに `Request` し、そのフレームの描画の始めに `Record` でコマンドを積む。
    ///          窓は次に描くときに `GetTexture` の絵を出す。
    class ShaderGraphPreview
    {
    public:
        ShaderGraphPreview();
        ~ShaderGraphPreview();

        void Initialize(EngineSystem* engine);
        void Finalize();

        /// @brief このフレームに描く中身を決める
        /// @param shaderFile 検査を通ったシェーダ
        /// @param target グラフの目標
        /// @param revision シェーダの中身が変わるたびに変わる番号
        /// @param values プロパティの値とテクスチャ
        /// @param mainTexture 板のテクスチャ・ポストエフェクトでは画面の代わりの画像・3D ではモデルの画像
        ///        （`Application/Assets/…`。空なら既定）
        /// @param view 3D の形と向き
        void Request(const std::filesystem::path& shaderFile, Target target, uint64_t revision, const ShaderMaterial& values,
            const std::string& mainTexture, const PreviewView& view = {});

        /// @brief 頼まれていればプレビューを描く（描画の始めに呼ぶ）
        void Record(ID3D12GraphicsCommandList* commandList);

        /// @brief 描いた絵（まだ無ければ ptr が 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetTexture() const;

        /// @brief パイプラインを組めなかった訳（組めていれば空）
        const std::string& GetError() const { return error_; }

        static constexpr uint32_t kSize = 256;

    private:
        /// @brief ポストエフェクトのコンピュートシェーダのパイプライン
        struct ComputePipeline
        {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
            std::unique_ptr<RootSignatureManager> rootSignature;
            std::unique_ptr<ShaderReflectionData> reflection;
            MaterialParamLayout layout;
            RootSlot inputSlot;
            RootSlot outputSlot;
            RootSlot frameSlot;
            RootSlot paramsSlot;
            std::vector<RootSlot> textureSlots;

            ComputePipeline();
            ~ComputePipeline();
        };

        /// @brief 3D の頂点シェーダとピクセルシェーダのパイプライン
        struct SurfacePipeline
        {
            std::unique_ptr<RootSignatureManager> rootSignature;
            std::unique_ptr<PipelineStateManager> pipelineStates;
            std::unique_ptr<ShaderReflectionData> reflection;
            MaterialParamLayout layout;
            RootSlot previewSlot;
            RootSlot materialSlot;
            RootSlot cameraSlot;
            RootSlot vertexAnimSlot;
            RootSlot paramsSlot;
            std::array<RootSlot, 5> modelTextureSlots; ///< gTexture・法線・金属と粗さ・発光・AO のマップ
            std::vector<RootSlot> textureSlots;

            SurfacePipeline();
            ~SurfacePipeline();
        };

        /// @brief 3D の形の頂点と番号
        struct PreviewMesh
        {
            std::vector<VertexData> vertices;
            std::vector<uint32_t> indices;

            PreviewMesh();
            ~PreviewMesh();
        };

        /// @brief シェーダが変わっていればパイプラインを組み直す
        bool EnsurePipeline();

        /// @brief ポストエフェクトのパイプラインを組む（組めなければ nullptr）
        std::unique_ptr<ComputePipeline> BuildComputePipeline(ID3D12Device* device);

        /// @brief 3D のパイプラインを組む（組めなければ nullptr）
        std::unique_ptr<SurfacePipeline> BuildSurfacePipeline(ID3D12Device* device);

        /// @brief 形の頂点と番号（初めてなら作る）
        const PreviewMesh& GetMesh(PreviewShape shape);

        /// @brief 板 1 枚を描く（スプライト）
        void RecordSprite(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE main,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        /// @brief コンピュートシェーダで全部の画素を書く（ポストエフェクト）
        void RecordCompute(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE input,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        /// @brief 形を 1 つ描く（3D）
        void RecordSurface(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE main,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        EngineSystem* engine_ = nullptr;
        std::unique_ptr<ShaderCompiler> compiler_;
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_;
        std::unique_ptr<ShaderMaterialPipeline> pipeline_;
        std::unique_ptr<ComputePipeline> compute_;
        std::unique_ptr<SurfacePipeline> surface_;
        std::vector<std::unique_ptr<ShaderMaterialPipeline>> retiredPipelines_; ///< 描画中のフレームが使っているかもしれない古いもの
        std::vector<std::unique_ptr<ComputePipeline>> retiredComputes_;
        std::vector<std::unique_ptr<SurfacePipeline>> retiredSurfaces_;
        RenderTarget* target_ = nullptr;   ///< スプライトとポストエフェクト（透けたところは透明）
        RenderTarget* target3D_ = nullptr; ///< 3D（暗い背景）
        RenderTarget* drawnTarget_ = nullptr;
        std::unique_ptr<SceneDepth> depth_; ///< 3D の深度（シーンの深度とは別）
        std::array<std::unique_ptr<PreviewMesh>, 3> meshes_;
        PreviewView view_;

        std::filesystem::path requestedShader_;
        Target requestedTarget_ = Target::Sprite;
        uint64_t requestedRevision_ = 0;
        std::filesystem::path builtShader_;
        Target builtTarget_ = Target::Sprite;
        uint64_t builtRevision_ = 0;
        bool requested_ = false;
        bool drawn_ = false;
        ShaderMaterial values_;
        std::string mainTexturePath_;
        TextureManager::LoadedTexture mainTexture_;
        TextureManager::LoadedTexture whiteTexture_;
        TextureManager::LoadedTexture checkerTexture_;
        std::string error_;
    };
}

#endif // CORE_EDITOR
