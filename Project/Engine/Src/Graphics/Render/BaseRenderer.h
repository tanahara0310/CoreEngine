#pragma once
#include "IRenderer.h"
#include "Graphics/Pipeline/GraphicsPipelineUtil.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include <d3d12.h>
#include <memory>
#include <string>

namespace CoreEngine
{
    class ShaderReflectionData;

    /// @brief 全レンダラー共通の基底クラス
    /// @note パイプライン・シェーダー関連のサブシステムを集約し、重複コストを削減
    class BaseRenderer : public IRenderer {
    public:
        virtual ~BaseRenderer() = default;

    protected:
        /// @brief シェーダーをコンパイルし、リフレクション（reflectionData_）とルートシグネチャ（rootSignatureMg_）を作る
        /// @param reflectionName リフレクションに付ける名前
        /// @param errorLabel     失敗したときの例外に入れる名前（"Failed to create <errorLabel> Root Signature: <理由>"）
        /// @return コンパイル済みのシェーダー（PSO の構築に使う）
        /// @throws std::runtime_error ルートシグネチャを作れなかったとき
        GraphicsPipelineUtil::ShaderPair BuildShadersAndRootSignature(
            ID3D12Device* device, const std::wstring& vsPath, const std::wstring& psPath,
            const std::string& reflectionName, const RootSignatureConfig& config, const std::string& errorLabel);

        /// @brief ブレンドモードが変わったときだけ psoMg_ から PSO を引き直し、ルートシグネチャ・PSO・三角形リストを差す
        void BeginBlendModePass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode);

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
