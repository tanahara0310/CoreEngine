#pragma once

#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <memory>
#include <span>
#include <string>

namespace CoreEngine
{
    class ICustomShaderProvider;

    /// @brief アプリ側から渡されたカスタムシェーダーで PSO を構築・保持するコンポーネント
    class CustomShaderPipeline {
    public:
        /// @brief カスタムシェーダーをコンパイルし、リフレクションから RootSignature と PSO を構築する
        bool Build(
            ID3D12Device* device,
            ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder,
            const ICustomShaderProvider& provider);

        /// @brief コンピュートシェーダー 1 本だけでパイプラインを組む
        /// @param computeShaderPath CS のファイル名（ディレクトリは AssetDatabase が解決する）
        /// @return CS の PSO まで組めたら true
        bool BuildCompute(
            ID3D12Device* device,
            ShaderCompiler& compiler,
            ShaderReflectionBuilder& reflectionBuilder,
            const std::wstring& computeShaderPath);

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

        /// @brief コンピュートパス用カスタム RootSignature を取得
        /// @return 構築済みなら RootSignature ポインタ、未構築なら nullptr
        ID3D12RootSignature* GetComputeRootSignature() const;

        /// @brief モデル描画共通リソース（ModelBind::kCustom）の解決済み表
        /// @details 構築時に 1 回解決済みなので、描画中の名前引きが不要になる
        const BindingTable& GetModelBindings() const { return modelBindings_; }

        /// @brief フォワードパスのリフレクション結果（シェーダー固有の契約を解決するため）
        /// @return 未構築なら nullptr
        const ShaderReflectionData* GetForwardReflection() const { return forwardReflection_.get(); }

        /// @brief コンピュートパスのリフレクション結果（シェーダー固有の契約を解決するため）
        /// @return 未構築なら nullptr
        const ShaderReflectionData* GetComputeReflection() const { return computeReflection_.get(); }

        /// @brief コンピュートパスのリフレクションと宣言表を突き合わせて解決する
        /// @param decls    宣言表（静的寿命であること）
        /// @param label    ログと表に出す名前
        /// @param bindings 解決した表の書き込み先
        /// @return 解決できたら true。リフレクションが無いとき・契約違反のときは false（契約違反の内訳は Resolve がエラーのログに出す）
        bool ResolveComputeBindings(
            std::span<const ShaderBindingDecl> decls, const std::string& label, BindingTable& bindings) const;

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
            const std::wstring& psPath,
            D3D12_CULL_MODE cullMode,
            bool depthWriteEnable,
            bool writesMotionVector);

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

        // フォワード用リフレクションと、モデル描画共通リソースの解決済み表
        std::unique_ptr<ShaderReflectionData> forwardReflection_;
        BindingTable modelBindings_;

        // コンピュート用リフレクション（呼び出し側が固有の契約を解決するために保持する）
        std::unique_ptr<ShaderReflectionData> computeReflection_;

        bool hasForwardPSO_ = false;
        bool hasComputePSO_ = false;
    };

} // namespace CoreEngine
