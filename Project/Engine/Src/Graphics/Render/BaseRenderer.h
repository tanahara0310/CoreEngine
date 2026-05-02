#pragma once
#include "IRenderer.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include <d3d12.h>
#include <memory>

namespace CoreEngine
{
    class ShaderReflectionData;

    /// @brief 全レンダラー共通の基底クラス
    /// @note パイプライン・シェーダー関連のサブシステムを集約し、重複コストを削減
    class BaseRenderer : public IRenderer {
    public:
        virtual ~BaseRenderer() = default;

    protected:
        // ──────────────────────────────────────────────────────────
        // 共通サブシステム（各レンダラーで重複していたもの）
        // ──────────────────────────────────────────────────────────
        std::unique_ptr<RootSignatureManager> rootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> psoMg_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler> shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

        // ──────────────────────────────────────────────────────────
        // 共通状態
        // ──────────────────────────────────────────────────────────
        ID3D12PipelineState* pipelineState_ = nullptr;
        BlendMode currentBlendMode_ = BlendMode::kBlendModeNone;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;
    };
}
