#pragma once

#ifdef CORE_EDITOR

#include "Graphics/Material/ShaderMaterial.h"
#include "Graphics/Material/ShaderMaterialPipeline.h"

#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace CoreEngine
{
    class EngineSystem;
    class RenderTarget;
    class ShaderCompiler;
    class ShaderReflectionBuilder;
}

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief グラフの Main Preview（画面外のテクスチャに板 1 枚を描く）
    /// @details 窓を描くときに `Request` し、そのフレームの描画の始めに `Record` でコマンドを積む。
    ///          窓は次に描くときに `GetTexture` の絵を出す。
    class ShaderGraphPreview
    {
    public:
        ShaderGraphPreview();
        ~ShaderGraphPreview();

        void Initialize(EngineSystem* engine);
        void Finalize();

        /// @brief このフレームに描く中身を決める
        /// @param shaderFile 検査を通ったピクセルシェーダ
        /// @param revision シェーダの中身が変わるたびに変わる番号
        /// @param values プロパティの値とテクスチャ
        /// @param mainTexture 板のテクスチャ（`Application/Assets/…`。空なら白）
        void Request(const std::filesystem::path& shaderFile, uint64_t revision, const ShaderMaterial& values,
            const std::string& mainTexture);

        /// @brief 頼まれていればプレビューを描く（描画の始めに呼ぶ）
        void Record(ID3D12GraphicsCommandList* commandList);

        /// @brief 描いた絵（まだ無ければ ptr が 0）
        D3D12_GPU_DESCRIPTOR_HANDLE GetTexture() const;

        /// @brief パイプラインを組めなかった訳（組めていれば空）
        const std::string& GetError() const { return error_; }

        static constexpr uint32_t kSize = 256;

    private:
        /// @brief シェーダが変わっていればパイプラインを組み直す
        bool EnsurePipeline();

        EngineSystem* engine_ = nullptr;
        std::unique_ptr<ShaderCompiler> compiler_;
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_;
        std::unique_ptr<ShaderMaterialPipeline> pipeline_;
        std::vector<std::unique_ptr<ShaderMaterialPipeline>> retiredPipelines_; ///< 描画中のフレームが使っているかもしれない古いもの
        RenderTarget* target_ = nullptr;

        std::filesystem::path requestedShader_;
        uint64_t requestedRevision_ = 0;
        std::filesystem::path builtShader_;
        uint64_t builtRevision_ = 0;
        bool requested_ = false;
        bool drawn_ = false;
        ShaderMaterial values_;
        std::string mainTexturePath_;
        TextureManager::LoadedTexture mainTexture_;
        TextureManager::LoadedTexture whiteTexture_;
        std::string error_;
    };
}

#endif // CORE_EDITOR
