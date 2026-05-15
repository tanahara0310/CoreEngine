#pragma once

#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureManager.h"

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <memory>
#include <string>

namespace CoreEngine
{
    class ICustomShaderProvider;

    /// @brief アプリ側から渡されたカスタムシェーダーで PSO を構築・保持するコンポーネント
    class CustomShaderPipeline {
    public:
        /// @brief カスタムシェーダーをコンパイルし PSO を構築する
        /// @param device D3D12 デバイス
        /// @param compiler 既存の ShaderCompiler インスタンス
        /// @param reflectionBuilder 既存の ShaderReflectionBuilder インスタンス
        /// @param provider シェーダーパスを提供するオブジェクト
        /// @param existingRootSignature 既定のフォワードパス RootSignature（再利用）
        /// @return 構築に成功したか
        bool Build(
            ID3D12Device* device,
            ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder,
            const ICustomShaderProvider& provider,
            ID3D12RootSignature* existingRootSignature);

        /// @brief フォワードパス用のカスタム PSO を取得
        /// @param mode ブレンドモード
        /// @return 構築済みなら PSO ポインタ、未構築なら nullptr
        ID3D12PipelineState* GetForwardPSO(BlendMode mode = BlendMode::kBlendModeNone) const;

        /// @brief コンピュートシェーダー用 PSO を取得
        /// @return 構築済みなら PSO ポインタ、未構築なら nullptr
        ID3D12PipelineState* GetComputePSO() const;

        /// @brief フォワードパス PSO が有効かどうか
        bool HasForwardPSO() const;

        /// @brief コンピュート PSO が有効かどうか
        bool HasComputePSO() const;

    private:
        /// @brief コンピュートパイプラインステートを構築する
        /// @param device D3D12 デバイス
        /// @param compiler ShaderCompiler インスタンス
        /// @param reflectionBuilder ShaderReflectionBuilder インスタンス
        /// @param csPath コンピュートシェーダーのフルパス
        void BuildComputePipeline(
            ID3D12Device* device,
            ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder,
            const std::wstring& csPath);

        PipelineStateManager forwardPsoMg_;
        std::unique_ptr<RootSignatureManager> computeRootSignatureMg_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePSO_;

        bool hasForwardPSO_ = false;
        bool hasComputePSO_ = false;
    };

} // namespace CoreEngine
