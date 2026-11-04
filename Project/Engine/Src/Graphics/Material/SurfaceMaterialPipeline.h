#pragma once

#include "Graphics/Material/ShaderMaterialPipeline.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/RootSlot.h"
#include "Graphics/Shader/ShaderBindingContract.h"

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    class ShaderCompiler;
    class ShaderReflectionBuilder;

    /// @brief 3D モデルのマテリアルを描けないときに使うシェーダ（マゼンタ）
    inline constexpr const char* kErrorSurfaceShader = "Engine/Assets/Shaders/Object/ErrorSurface.Surface.hlsl";

    /// @brief 3D モデルのマテリアルの 1 通りの頂点シェーダとピクセルシェーダの入口
    struct SurfaceMaterialStage
    {
        const char* vertexShader;
        const wchar_t* entryPoint;
        const char* debugName;
    };

    /// @brief GBuffer・GBuffer（スキン）・前方描画・前方描画（スキン）の順
    inline constexpr SurfaceMaterialStage kSurfaceMaterialStages[] = {
        { "Engine/Assets/Shaders/Object/GBuffer.VS.hlsl", L"GBufferMain", "SurfaceGBuffer" },
        { "Engine/Assets/Shaders/Skinning/GBufferSkinning.VS.hlsl", L"GBufferMovingMain", "SurfaceGBufferSkinned" },
        { "Engine/Assets/Shaders/Object/Object3d.VS.hlsl", L"ForwardMain", "SurfaceForward" },
        { "Engine/Assets/Shaders/Skinning/SkinningObject3d.VS.hlsl", L"ForwardMain", "SurfaceForwardSkinned" },
    };

    /// @brief 3D モデルのマテリアルの 1 通り（静的かスキンか・GBuffer か前方描画か）のルートシグネチャと PSO
    struct SurfaceMaterialVariant
    {
        std::unique_ptr<RootSignatureManager> rootSignature;
        std::unique_ptr<PipelineStateManager> pipelineStates;
        const MaterialParamLayout* layout = nullptr; ///< 持ち主のパイプラインの値の並び
        BindingTable modelBindings;                  ///< モデル描画の共通のリソース（`ModelBind::kCustom`）
        RootSlot paramsSlot;                         ///< マテリアルの値（b10）
        std::vector<RootSlot> textureSlots;          ///< `layout->textures` と同じ並び（このパスで使わないものは無効）

        ID3D12RootSignature* GetRootSignature() const;
        ID3D12PipelineState* GetPipelineState(BlendMode mode) const;
    };

    /// @brief 3D モデルのマテリアルのシェーダ（`名前.Surface.hlsl`）で組んだパイプライン
    /// @details 静的なモデルとスキンのモデル、GBuffer（不透明）と前方描画（ブレンドあり）の 4 通りがあり、
    ///          使うときに初めて組む。値の並びは b10 の定数バッファ、テクスチャは t11 から読む。
    class SurfaceMaterialPipeline
    {
    public:
        /// @brief 描くパス
        enum class Pass : uint8_t { GBuffer, Forward };

        SurfaceMaterialPipeline(ID3D12Device* device, ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder, std::string shaderPath);
        ~SurfaceMaterialPipeline();

        SurfaceMaterialPipeline(const SurfaceMaterialPipeline&) = delete;
        SurfaceMaterialPipeline& operator=(const SurfaceMaterialPipeline&) = delete;

        /// @brief 静的なモデルの GBuffer を組み、値の並びを読む
        /// @return 組めなければ false
        bool Build();

        /// @brief 前のパイプラインで組もうとした組み合わせを先に組む
        /// @return 全部組めたら true
        bool BuildVariantsUsedBy(const SurfaceMaterialPipeline& previous);

        /// @brief 組み合わせを引く（初めてなら組む。組めなかった組み合わせは組み直さない）
        /// @return 組めなければ nullptr
        const SurfaceMaterialVariant* GetVariant(Pass pass, bool skinned);

        /// @brief 値の並び（Build の後）
        const MaterialParamLayout& GetLayout() const { return layout_; }

        const std::string& GetShaderPath() const { return shaderPath_; }

    private:
        static constexpr size_t kVariantCount = 4;

        static size_t VariantIndex(Pass pass, bool skinned);

        /// @brief 1 通りを組む
        bool BuildVariant(Pass pass, bool skinned);

        /// @brief ピクセルシェーダを入口を選んでコンパイルする（前方描画は静的とスキンで同じものを使う）
        Microsoft::WRL::ComPtr<IDxcBlob> CompilePixelShader(Pass pass, bool skinned, std::string& error);

        ID3D12Device* device_ = nullptr;
        ShaderCompiler* compiler_ = nullptr;
        ShaderReflectionBuilder* reflectionBuilder_ = nullptr;
        std::string shaderPath_;
        MaterialParamLayout layout_;
        std::array<std::unique_ptr<SurfaceMaterialVariant>, kVariantCount> variants_;
        std::array<bool, kVariantCount> attempted_{};
        Microsoft::WRL::ComPtr<IDxcBlob> forwardPixelShader_;
    };

    /// @brief 3D モデルのマテリアルのシェーダごとにパイプラインを 1 つずつ持つ
    /// @details シェーダのファイルが変わったら（`ShaderMaterialLibrary` の番号が進んだら）組み直す。
    ///          組み直しに失敗したら前のパイプラインを使い続ける。
    ///          組み直した古いパイプラインは、描画中のフレームが使っているかもしれないので捨てずに持つ。
    class SurfaceMaterialPipelineCache
    {
    public:
        SurfaceMaterialPipelineCache(ID3D12Device* device, ShaderCompiler& compiler, ShaderReflectionBuilder& reflectionBuilder);
        ~SurfaceMaterialPipelineCache();

        /// @brief シェーダのパイプラインを引く（無ければ組む）
        /// @return 組めなければ nullptr
        SurfaceMaterialPipeline* Get(const std::string& shaderPath);

        /// @brief 描けないときのパイプライン（マゼンタ）
        SurfaceMaterialPipeline* GetError() { return Get(kErrorSurfaceShader); }

    private:
        struct Entry
        {
            std::unique_ptr<SurfaceMaterialPipeline> pipeline;
            uint64_t shaderRevision = 0;
        };

        ID3D12Device* device_ = nullptr;
        ShaderCompiler* compiler_ = nullptr;
        ShaderReflectionBuilder* reflectionBuilder_ = nullptr;
        std::unordered_map<std::string, Entry> entries_;
        std::vector<std::unique_ptr<SurfaceMaterialPipeline>> retired_;
    };
}
