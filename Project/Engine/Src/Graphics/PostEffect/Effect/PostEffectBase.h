#pragma once
#include <d3d12.h>
#include <string>
#include <wrl.h>
#include <memory>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"


namespace CoreEngine {

    /// @brief ポストエフェクトの実行方式
    enum class PostEffectExecutionType {
        Graphics, ///< グラフィクスパイプライン（VS+PS、デフォルト）
        Compute,  ///< コンピュートパイプライン（CS）
    };
    class ShaderReflectionData;

    class PostEffectBase {
    public:
        virtual ~PostEffectBase() = default;
        virtual void Initialize(DirectXCommon* dxCommon);
        virtual void Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

        /// @brief バックバッファ(_SRGB)への最終描画用
        /// @param inputSrvHandle 入力テクスチャのSRVハンドル
        virtual void DrawToBackBuffer(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle);

        /// @brief コンピュートシェーダーによるエフェクト実行
        /// @param inputSrvHandle  入力テクスチャのSRVハンドル
        /// @param outputUavHandle 出力テクスチャのUAVハンドル
        /// @param width           出力幅
        /// @param height          出力高さ
        virtual void Dispatch(
            D3D12_GPU_DESCRIPTOR_HANDLE /*inputSrvHandle*/,
            D3D12_GPU_DESCRIPTOR_HANDLE /*outputUavHandle*/,
            uint32_t /*width*/,
            uint32_t /*height*/) {
        }

        /// @brief エフェクトの実行方式を返す
        /// @return Graphics または Compute
        virtual PostEffectExecutionType GetExecutionType() const { return PostEffectExecutionType::Graphics; }

        /// @brief ImGuiでパラメータを調整する関数
        virtual void DrawImGui() {}

        /// @brief 更新処理（デフォルトは空実装）
        /// @param deltaTime フレーム時間
        virtual void Update(float /*deltaTime*/) {}

        /// @brief エフェクトの有効/無効を設定
        /// @param enabled 有効にするかどうか
        virtual void SetEnabled(bool enabled) { enabled_ = enabled; }

        /// @brief エフェクトが有効かどうかを取得
        /// @return 有効ならtrue
        bool IsEnabled() const { return enabled_; }

        /// @brief 常時有効なエフェクトかどうかを取得（無効化不可）
        /// @return 常時有効ならtrue
        virtual bool IsAlwaysEnabled() const { return false; }

        /// @brief シェーダーリソース名からルートパラメータインデックスを取得
        int GetRootParamIndex(const std::string& resourceName) const;

    protected:
        virtual const std::wstring& GetPixelShaderPath() const { static const std::wstring empty; return empty; }
        virtual std::string GetEffectName() const { return "PostEffect"; }
        virtual void BindOptionalCBVs(ID3D12GraphicsCommandList*/* commandList*/) {}

        /// @brief ルートシグネチャ構築前に呼ばれる設定フック
        virtual void OnConfigureRootSignature(RootSignatureConfig& /*config*/) {}

        /// @brief CS シェーダーのファイル名を返す（CS 派生クラスで必ずオーバーライドする）
        virtual std::wstring GetComputeShaderPath() const { return L""; }

        /// @brief CS 共通初期化：コンパイル・リフレクション・RootSignature・PSO を一括構築する
        /// @details Compute 派生クラスの Initialize() から呼び出す
        void InitializeComputeCore();

        /// @brief 定数バッファ生成フック（InitializeComputeCore の後に呼ばれる）
        virtual void OnCreateConstantBuffers() {}

        /// @brief Draw/DrawToBackBuffer の共通処理
        /// @param inputSrvHandle 入力テクスチャのSRVハンドル
        /// @param psm 使用するパイプラインステートマネージャー
        void DrawInternal(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle, PipelineStateManager& psm);

    protected:
        DirectXCommon* directXCommon_ = nullptr;

        Microsoft::WRL::ComPtr<IDxcBlob> fullscreenVertexShaderBlob_;
        Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob_;
        Microsoft::WRL::ComPtr<IDxcBlob> computeShaderBlob_;       ///< CS用シェーダーブロブ
        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePso_;   ///< CS用PSO

        std::unique_ptr<RootSignatureManager> rootSignatureManager_;
        PipelineStateManager pipelineStateManager_;
        PipelineStateManager backBufferPipelineStateManager_; ///< バックバッファ(_SRGB)用PSO

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> reflectionData_;

        bool enabled_ = true; // デフォルトで有効
    };
}
