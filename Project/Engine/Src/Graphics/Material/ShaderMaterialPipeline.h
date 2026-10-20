#pragma once

#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/RootSlot.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/MathCore.h"

#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    class ShaderCompiler;
    class ShaderMaterial;
    class ShaderReflectionBuilder;

    /// @brief .mat で頂点シェーダを指定しないときに使う頂点シェーダ
    inline constexpr const char* kStandardMaterialVertexShader = "Engine/Assets/Shaders/Sprite/SpriteMaterial.VS.hlsl";

    /// @brief 描くたびにエンジンがマテリアルのシェーダへ渡す値（`SpriteMaterial.hlsli` の `gSpriteDraw`）
    struct SpriteDrawData
    {
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        Matrix4x4 uvTransform = MathCore::Matrix::Identity();
        Vector2 textureSize = { 1.0f, 1.0f };
        float time = 0.0f;
        float unscaledTime = 0.0f;
        float deltaTime = 0.0f;
        uint32_t filterMode = 0;
        Vector2 padding = { 0.0f, 0.0f };
    };

    static constexpr Cb::Field kSpriteDrawDataFields[] = {
        CB_FIELD(SpriteDrawData, color), CB_FIELD(SpriteDrawData, uvTransform),
        CB_FIELD(SpriteDrawData, textureSize), CB_FIELD(SpriteDrawData, time),
        CB_FIELD(SpriteDrawData, unscaledTime), CB_FIELD(SpriteDrawData, deltaTime),
        CB_FIELD(SpriteDrawData, filterMode), CB_FIELD(SpriteDrawData, padding),
    };
    CB_VERIFY_LAYOUT(SpriteDrawData, kSpriteDrawDataFields);
    CB_BIND_HLSL(SpriteDrawData, kSpriteDrawDataFields, "gSpriteDraw");

    /// @brief マテリアルの値の項目 1 つ（シェーダの定数バッファから読む）
    struct MaterialParamField
    {
        /// @brief 数の種類
        enum class Kind { Float, Int, Uint, Bool };

        std::string name;
        Kind kind = Kind::Float;
        uint32_t offset = 0;     ///< 定数バッファの先頭からのバイト数
        uint32_t components = 1; ///< 1〜4
        uint32_t elements = 0;   ///< 配列の要素数（配列でなければ 0）
    };

    /// @brief マテリアルの値の並び（シェーダの b0 の定数バッファ）
    struct MaterialParamLayout
    {
        std::string bufferName;  ///< 定数バッファの名前（ルートパラメータを引く名前）
        uint32_t size = 0;       ///< 定数バッファの大きさ（バイト。無ければ 0）
        std::vector<MaterialParamField> fields;
        std::vector<std::string> textures; ///< `gTexture` 以外のテクスチャの名前

        /// @brief 名前で項目を引く（無ければ nullptr）
        const MaterialParamField* Find(std::string_view name) const;
    };

    /// @brief マテリアルのシェーダで組んだパイプライン（ブレンドごとの PSO とルートシグネチャ）
    class ShaderMaterialPipeline
    {
    public:
        /// @brief 描き先ごとの違い
        struct Target
        {
            const char* debugName = "SpriteMaterial";
            bool depthTest = true; ///< スプライトは奥行きで隠れる・UI は隠れない
        };

        /// @brief シェーダをコンパイルして組む
        /// @param pixelShaderPath ピクセルシェーダ（`main`）
        /// @param vertexShaderPath 頂点シェーダ（`main`。空なら標準）
        /// @return 組めなければ false
        bool Build(ID3D12Device* device, ShaderCompiler& compiler, ShaderReflectionBuilder& reflectionBuilder,
            const std::string& pixelShaderPath, const std::string& vertexShaderPath, const Target& target);

        ID3D12PipelineState* GetPipelineState(BlendMode mode) const;
        ID3D12RootSignature* GetRootSignature() const;

        const MaterialParamLayout& GetLayout() const { return layout_; }

        RootSlot GetTransformSlot() const { return transformSlot_; }
        RootSlot GetDrawSlot() const { return drawSlot_; }
        RootSlot GetTextureSlot() const { return textureSlot_; }
        RootSlot GetParamsSlot() const { return paramsSlot_; }
        /// @brief `GetLayout().textures` と同じ並びのルートパラメータ
        const std::vector<RootSlot>& GetExtraTextureSlots() const { return extraTextureSlots_; }

    private:
        std::unique_ptr<RootSignatureManager> rootSignature_;
        std::unique_ptr<PipelineStateManager> pipelineStates_;
        MaterialParamLayout layout_;
        RootSlot transformSlot_;
        RootSlot drawSlot_;
        RootSlot textureSlot_;
        RootSlot paramsSlot_;
        std::vector<RootSlot> extraTextureSlots_;
    };

    /// @brief マテリアルのシェーダの組み合わせごとにパイプラインを 1 つずつ持つ
    /// @details シェーダのファイルが変わったら（`ShaderMaterialLibrary` の番号が進んだら）組み直す。
    ///          組み直しに失敗したら前のパイプラインを使い続ける。
    ///          組み直した古いパイプラインは、描画中のフレームが使っているかもしれないので捨てずに持つ。
    class ShaderMaterialPipelineCache
    {
    public:
        explicit ShaderMaterialPipelineCache(ShaderMaterialPipeline::Target target) : target_(target) {}

        /// @brief マテリアルのシェーダのパイプラインを引く（無ければ組む）
        /// @return 組めなければ nullptr
        const ShaderMaterialPipeline* Get(ID3D12Device* device, ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder, const ShaderMaterial& material);

    private:
        struct Entry
        {
            std::unique_ptr<ShaderMaterialPipeline> pipeline;
            uint64_t shaderRevision = 0;
        };

        ShaderMaterialPipeline::Target target_;
        std::unordered_map<std::string, Entry> entries_;
        std::vector<std::unique_ptr<ShaderMaterialPipeline>> retired_;
    };
}
