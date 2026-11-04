#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/RootSignature/RootSlot.h"

#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class EngineSystem;
    class RenderTarget;
    class RootSignatureManager;
    class ShaderCompiler;
    class ShaderReflectionBuilder;
    class ShaderReflectionData;
}

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief グラフの Main Preview（画面外の 256×256 のテクスチャへ描く）
    /// @details スプライトは板 1 枚を描き、ポストエフェクトはコンピュートシェーダで全部の画素を書く。
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
        /// @param target グラフの目標（スプライト・ポストエフェクト）
        /// @param revision シェーダの中身が変わるたびに変わる番号
        /// @param values プロパティの値とテクスチャ
        /// @param mainTexture 板のテクスチャ・ポストエフェクトでは画面の代わりの画像（`Application/Assets/…`。空なら既定）
        void Request(const std::filesystem::path& shaderFile, Target target, uint64_t revision, const ShaderMaterial& values,
            const std::string& mainTexture);

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

        /// @brief シェーダが変わっていればパイプラインを組み直す
        bool EnsurePipeline();

        /// @brief ポストエフェクトのパイプラインを組む（組めなければ nullptr）
        std::unique_ptr<ComputePipeline> BuildComputePipeline(ID3D12Device* device);

        /// @brief 板 1 枚を描く（スプライト）
        void RecordSprite(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE main,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        /// @brief コンピュートシェーダで全部の画素を書く（ポストエフェクト）
        void RecordCompute(ID3D12GraphicsCommandList* commandList, D3D12_GPU_DESCRIPTOR_HANDLE input,
            D3D12_GPU_DESCRIPTOR_HANDLE white);

        EngineSystem* engine_ = nullptr;
        std::unique_ptr<ShaderCompiler> compiler_;
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_;
        std::unique_ptr<ShaderMaterialPipeline> pipeline_;
        std::unique_ptr<ComputePipeline> compute_;
        std::vector<std::unique_ptr<ShaderMaterialPipeline>> retiredPipelines_; ///< 描画中のフレームが使っているかもしれない古いもの
        std::vector<std::unique_ptr<ComputePipeline>> retiredComputes_;
        RenderTarget* target_ = nullptr;

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
