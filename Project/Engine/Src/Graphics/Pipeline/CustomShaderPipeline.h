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

        /// @brief リソース名からフォワードパス用ルートパラメータ（番号＋差し方）を取得
        /// @param resourceName シェーダー内のリソース名（例: "gWave", "gFoamTexture"）
        /// @return 見つからない場合は kind == None
        /// @note 描画中に呼ぶと map 検索になる。モデル描画共通リソースは GetModelBindings() を使う
        RootSlot GetRootSlot(const std::string& resourceName) const;

        /// @brief モデル描画共通リソース（ModelBind::kCustom）の解決済み表
        /// @details 構築時に 1 回解決済みなので、描画中の名前引きが不要になる
        const BindingTable& GetModelBindings() const { return modelBindings_; }

        /// @brief フォワードパスのリフレクション結果（シェーダー固有の契約を解決するため）
        /// @return 未構築なら nullptr
        const ShaderReflectionData* GetForwardReflection() const { return forwardReflection_.get(); }

        /// @brief リソース名からフォワードパス用ルートパラメータインデックスを取得
        /// @param resourceName シェーダー内のリソース名（例: "gWave", "gFoamTexture"）
        /// @return インデックス（未登録の場合は -1）
        /// @deprecated GetRootSlot() へ移行すること
        int GetRootParamIndex(const std::string& resourceName) const;

        /// @brief リソース名からコンピュートパス用ルートパラメータインデックスを取得
        /// @param resourceName シェーダー内のリソース名
        /// @return インデックス（未登録の場合は -1）
        int GetComputeRootParamIndex(const std::string& resourceName) const;

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

        bool hasForwardPSO_ = false;
        bool hasComputePSO_ = false;
    };

} // namespace CoreEngine
