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
        /// @brief カスタムシェーダーをコンパイルし、リフレクションから独自 RootSignature と PSO を構築する
        /// @param device D3D12 デバイス
        /// @param compiler 既存の ShaderCompiler インスタンス
        /// @param reflectionBuilder 既存の ShaderReflectionBuilder インスタンス
        /// @param provider シェーダーパスを提供するオブジェクト
        /// @return 構築に成功したか
        bool Build(
            ID3D12Device* device,
            ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder,
            const ICustomShaderProvider& provider);

        /// @brief フォワードパス用のカスタム PSO を取得
        /// @param mode ブレンドモード
        /// @return 構築済みなら PSO ポインタ、未構築なら nullptr
        ID3D12PipelineState* GetForwardPSO(BlendMode mode = BlendMode::kBlendModeNone) const;

        /// @brief コンピュートシェーダー用 PSO を取得
        /// @return 構築済みなら PSO ポインタ、未構築なら nullptr
        ID3D12PipelineState* GetComputePSO() const;

        /// @brief フォワードパス用カスタム RootSignature を取得
        /// @return 構築済みなら RootSignature ポインタ、未構築なら nullptr
        ID3D12RootSignature* GetForwardRootSignature() const;

        /// @brief リソース名からフォワードパス用ルートパラメータインデックスを取得
        /// @param resourceName シェーダー内のリソース名（例: "gWave", "gFoamTexture"）
        /// @return インデックス（未登録の場合は -1）
        int GetRootParamIndex(const std::string& resourceName) const;

        /// @brief フォワードパス PSO が有効かどうか
        bool HasForwardPSO() const;

        /// @brief コンピュート PSO が有効かどうか
        bool HasComputePSO() const;

    private:
        /// @brief フォワードパイプラインステートを構築する
        bool BuildForwardPipeline(
            ID3D12Device* device,
            ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder,
            const std::wstring& vsPath,
            const std::wstring& psPath);

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

        // フォワードパス用（独自 RS を保持）
        std::unique_ptr<RootSignatureManager> forwardRootSignatureMg_;
        PipelineStateManager forwardPsoMg_;

        // コンピュートパス用
        std::unique_ptr<RootSignatureManager> computeRootSignatureMg_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePSO_;

        bool hasForwardPSO_ = false;
        bool hasComputePSO_ = false;
    };

} // namespace CoreEngine
