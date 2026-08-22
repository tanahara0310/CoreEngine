#pragma once
#include <wrl.h>
#include <dxcapi.h>
#include <string>

#include "PostEffectBase.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"


namespace CoreEngine {

    /// @brief グラフィクスパイプライン（VS+PS）用ポストエフェクト基底クラス
    class PostEffectGraphicsBase : public PostEffectBase {
    public:
        /// @brief VS+PS パイプラインによる初期化
        void Initialize(GraphicsCore* dxCommon) override;

        /// @brief オフスクリーン RT への描画
        void Draw(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle) override;

        /// @brief バックバッファ(_SRGB) への最終描画
        void DrawToBackBuffer(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle) override;

        PostEffectExecutionType GetExecutionType() const override {
            return PostEffectExecutionType::Graphics;
        }

    protected:
        /// @brief ピクセルシェーダーのファイルパスを返す（派生クラスで必ずオーバーライドする）
        virtual const std::wstring& GetPixelShaderPath() const = 0;

        /// @brief 追加の定数バッファバインドフック
        virtual void BindOptionalCBVs(ID3D12GraphicsCommandList* /*commandList*/) {}

    private:
        /// @brief Draw/DrawToBackBuffer の共通処理
        void DrawInternal(D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle, PipelineStateManager& psm);

        Microsoft::WRL::ComPtr<IDxcBlob> fullscreenVertexShaderBlob_;
        Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob_;

        PipelineStateManager pipelineStateManager_;
        PipelineStateManager backBufferPipelineStateManager_; ///< バックバッファ(_SRGB)用PSO
    };

} // namespace CoreEngine
