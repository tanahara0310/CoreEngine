#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
#include "Editor/ShaderGraph/ShaderGraphCodeGen.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/RootSignature/RootSlot.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
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

    /// @brief 裏のスレッドでコンパイルしたシェーダ
    struct CompiledShaders
    {
        uint64_t key = 0;
        Target target = Target::Sprite;
        Microsoft::WRL::ComPtr<IDxcBlob> vertex;  ///< スプライトと 3D の頂点シェーダ
        Microsoft::WRL::ComPtr<IDxcBlob> pixel;   ///< スプライトと 3D のピクセルシェーダ
        Microsoft::WRL::ComPtr<IDxcBlob> compute; ///< ポストエフェクトのコンピュートシェーダ
        std::string error;                        ///< コンパイルできなかった訳（空なら通った）
    };

    /// @brief グラフの HLSL を裏のスレッドでコンパイルする（待っている頼みは最後の 1 つだけ残す）
    /// @details 入口とインクルードの解決はメインスレッドで済ませ、コンパイルだけを自前のコンパイラで行う。
    class ShaderGraphCompileWorker
    {
    public:
        ShaderGraphCompileWorker();
        ~ShaderGraphCompileWorker();

        void Start();
        void Stop();

        /// @brief コンパイルを頼む（メインスレッドから。始まっていない前の頼みは捨てる）
        /// @param file HLSL を書くファイル（裏のスレッドが書いてからコンパイルする）
        /// @param prepareCompiler 入口とインクルードを解決するメインスレッドのコンパイラ
        void Submit(uint64_t key, std::string hlsl, Target target, const std::filesystem::path& file,
            const ShaderCompiler& prepareCompiler);

        /// @brief 終わった結果を受け取る（無ければ false）
        bool TakeFinished(CompiledShaders& result);

    private:
        struct Job;
        void Run(std::stop_token stop);

        std::jthread thread_;
        std::mutex mutex_;
        std::condition_variable_any wake_;
        std::unique_ptr<Job> pending_;
        std::optional<CompiledShaders> finished_;
    };

    /// @brief グラフのプレビュー（画面外のテクスチャへ描く）
    /// @details Main Preview は 256×256 に 1 枚、ノードのプレビューは大きな絵にタイルで並べて描く。
    ///          スプライトはタイルごとに板を描き、ポストエフェクトはコンピュートシェーダで全部の画素を書く。
    ///          3D は球・立方体・平面のどれかを、決まったライト 1 つと環境光で照らして描く。
    ///          窓を描くときに `Request`（または `RequestTiles`）し、そのフレームの描画の始めに `Record` でコマンドを積む。
    ///          窓は次に描くときに `GetTexture` の絵を出す。
    class ShaderGraphPreview
    {
    public:
        /// @param name 描き先の名前（インスタンスごとに変える）
        /// @param size 描き先の一辺（px）
        explicit ShaderGraphPreview(std::string name = "ShaderGraphPreview", uint32_t size = kSize);
        ~ShaderGraphPreview();

        void Initialize(EngineSystem* engine);
        void Finalize();

        /// @brief このフレームに描く中身を決める（Main Preview・絵全体に 1 枚）
        /// @param shaderFile 検査を通ったシェーダ（ここでコンパイルする）
        /// @param target グラフの目標
        /// @param revision シェーダの中身が変わるたびに変わる番号
        /// @param values プロパティの値とテクスチャ
        /// @param mainTexture 板のテクスチャ・ポストエフェクトでは画面の代わりの画像・3D ではモデルの画像
        ///        （`Application/Assets/…`。空なら既定）
        /// @param view 3D の形と向き
        void Request(const std::filesystem::path& shaderFile, Target target, uint64_t revision, const ShaderMaterial& values,
            const std::string& mainTexture, const PreviewView& view = {});

        /// @brief 裏でコンパイルしたシェーダを渡す（次の `Record` でパイプラインを組む）
        void SetCompiled(CompiledShaders shaders);

        /// @brief このフレームに描く中身を決める（ノードのプレビュー・タイルで並べる）
        /// @param key `SetCompiled` で渡したシェーダの印（組めたシェーダと違えば描かない）
        /// @param view 球のタイルの向き
        void RequestTiles(uint64_t key, Target target, const std::vector<NodePreviewTile>& tiles, const ShaderMaterial& values,
            const std::string& mainTexture, const PreviewView& view);

        /// @brief 頼まれていればプレビューを描く（描画の始めに呼ぶ）
        void Record(ID3D12GraphicsCommandList* commandList);

        /// @brief 描いた絵（まだ無ければ ptr が 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetTexture() const;

        /// @brief パイプラインを組めなかった訳（組めていれば空）
        const std::string& GetError() const { return error_; }

        /// @brief パイプラインを組めたシェーダの印・組めなかったシェーダの印・最後に描いた絵の印（`SetCompiled` の key）
        uint64_t GetBuiltKey() const { return builtKey_; }
        uint64_t GetFailedKey() const { return failedKey_; }
        uint64_t GetDrawnKey() const { return drawn_ ? drawnKey_ : 0; }

        /// @brief 描き先の一辺（px）
        uint32_t GetSize() const { return size_; }

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

        /// @brief 描くタイル 1 枚（描き先の px）
        struct DrawTile
        {
            float x = 0.0f;
            float y = 0.0f;
            float size = 0.0f;
            PreviewShape shape = PreviewShape::Sphere;
            bool rotate = true; ///< 3D の形を view_ の向きに回す
        };

        /// @brief 白・市松・板の画像を読む（読んであれば何もしない）
        void LoadTextures(Target target, const std::string& mainTexture);

        /// @brief シェーダが変わっていればパイプラインを組み直す（ノードのプレビューは組めたシェーダの印を比べる）
        bool EnsurePipeline();

        /// @brief Main Preview のシェーダをコンパイルする（コンパイルできなければ error に訳）
        CompiledShaders CompileRequested();

        /// @brief コンパイルしたシェーダでパイプラインを組み、今のものと入れ替える（組めなければ false）
        bool BuildPipeline(const CompiledShaders& shaders, bool hasVertexFunction);

        /// @brief ポストエフェクトのパイプラインを組む（組めなければ nullptr）
        std::unique_ptr<ComputePipeline> BuildComputePipeline(ID3D12Device* device, IDxcBlob* blob);

        /// @brief 3D のパイプラインを組む（組めなければ nullptr）
        std::unique_ptr<SurfacePipeline> BuildSurfacePipeline(ID3D12Device* device, IDxcBlob* vertexBlob, IDxcBlob* pixelBlob,
            bool hasVertexFunction);

        /// @brief 形の頂点と番号（初めてなら作る）
        const PreviewMesh& GetMesh(PreviewShape shape);

        /// @brief タイルごとに板を描く（スプライト）
        void RecordSprite(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE main,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        /// @brief コンピュートシェーダで全部の画素を書く（ポストエフェクト）
        void RecordCompute(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE input,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        /// @brief タイルごとに形を描く（3D）
        void RecordSurface(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE main,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        std::string name_;
        uint32_t size_ = kSize;
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
        std::vector<DrawTile> tiles_;

        // ノードのプレビュー（裏でコンパイルしたシェーダ）
        std::optional<CompiledShaders> compiled_; ///< まだパイプラインを組んでいないもの
        uint64_t requestedKey_ = 0;
        uint64_t builtKey_ = 0;
        uint64_t failedKey_ = 0;
        uint64_t drawnKey_ = 0;

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
