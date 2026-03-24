#pragma once
#include "Graphics/Render/IRenderer.h"
#include "Graphics/Render/Model/IBLSceneParams.h"
#include "Graphics/Render/Model/ModelDrawPacket.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Math/Vector/Vector3.h"
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

namespace CoreEngine {
    class LightManager;
    class ShaderReflectionData;
}

namespace CoreEngine
{
    /// @brief ModelRenderer / SkinnedModelRenderer 共通基底クラス
    class BaseModelRenderer : public IRenderer {
    public:
        /// @brief フォワードパスを開始（RootSignature・PSO・シーン定数をバインド）
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        /// @brief GBuffer パスを開始（GBuffer 用 RootSignature・PSO をバインド）
        void BeginGBufferPass(ID3D12GraphicsCommandList* cmdList) override;
        /// @brief パスを終了（GBuffer フラグをリセット）
        void EndPass() override;
        /// @brief カメラの GPU 仮想アドレスを取得して保持
        void SetCamera(const ICamera* camera) override;

        /// @brief フォワードパス用 RootSignature を取得
        ID3D12RootSignature* GetRootSignature() const { return forwardRootSignatureMg_->GetRootSignature(); }
        /// @brief GBuffer パス用 RootSignature を取得
        ID3D12RootSignature* GetGBufferRootSignature() const { return gBufferRootSignatureMg_->GetRootSignature(); }

        /// @brief ライトマネージャーを設定
        void SetLightManager(LightManager* lightManager) { lightManager_ = lightManager; }
        /// @brief 環境マップ SRV ハンドルを設定
        void SetEnvironmentMap(D3D12_GPU_DESCRIPTOR_HANDLE handle) { environmentMapHandle_ = handle; }
        /// @brief シャドウマップ SRV ハンドルを設定
        void SetShadowMap(D3D12_GPU_DESCRIPTOR_HANDLE handle) { shadowMapHandle_ = handle; }
        /// @brief ライトビュープロジェクション CBV アドレスを設定
        void SetLightViewProjection(D3D12_GPU_VIRTUAL_ADDRESS addr) { lightViewProjectionCBV_ = addr; }
        /// @brief Irradiance Map SRV ハンドルを設定
        void SetIrradianceMap(D3D12_GPU_DESCRIPTOR_HANDLE handle) { irradianceMapHandle_ = handle; }
        /// @brief Prefiltered Map SRV ハンドルを設定
        void SetPrefilteredMap(D3D12_GPU_DESCRIPTOR_HANDLE handle) { prefilteredMapHandle_ = handle; }
        /// @brief BRDF LUT SRV ハンドルを設定
        void SetBRDFLUT(D3D12_GPU_DESCRIPTOR_HANDLE handle) { brdfLUTHandle_ = handle; }

        /// @brief シーン共通 IBL 環境回転角度を設定（ラジアン）
        void SetIBLRotation(const Vector3& rotation) { iblRotation_ = rotation; }

        /// @brief 環境マップテクスチャが設定済みか確認
        bool HasEnvironmentMap() const { return environmentMapHandle_.ptr != 0; }

        /// @brief IBLに必要なテクスチャ（Irradiance / Prefiltered / BRDF LUT）が全て設定済みか確認
        bool HasIBLMaps() const {
            return irradianceMapHandle_.ptr != 0
                && prefilteredMapHandle_.ptr != 0
                && brdfLUTHandle_.ptr != 0;
        }

        /// @brief フォワードパスのリソース名からルートパラメータインデックスを取得（-1: 未登録）
        int GetRootParamIndex(const std::string& resourceName) const;
        /// @brief GBuffer パスのリソース名からルートパラメータインデックスを取得（-1: 未登録）
        int GetGBufferRootParamIndex(const std::string& resourceName) const;
        /// @brief 現在 GBuffer パス中かどうかを返す
        bool IsInGBufferPass() const { return isInGBufferPass_; }

        /// @brief モデル描画パケットをバインドして描画コマンドを発行する
        /// Model が組み立てた ModelDrawPacket を受け取り、現在のパス（Forward/GBuffer）に
        /// 応じたルートパラメータへのバインドと DrawIndexedInstanced の呼び出しを行う。
        void BindModelDrawPacket(ID3D12GraphicsCommandList* cmdList, const ModelDrawPacket& packet);

        /// @brief カメラ CBV の GPU 仮想アドレスを取得（DeferredLightingPass 連携用）
        D3D12_GPU_VIRTUAL_ADDRESS GetCameraCBVAddress() const { return cameraCBV_; }

    protected:
        std::unique_ptr<RootSignatureManager> forwardRootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<RootSignatureManager> gBufferRootSignatureMg_ = std::make_unique<RootSignatureManager>();
        std::unique_ptr<PipelineStateManager> forwardPsoMg_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<PipelineStateManager> gBufferPsoMg_ = std::make_unique<PipelineStateManager>();
        std::unique_ptr<ShaderCompiler>        shaderCompiler_ = std::make_unique<ShaderCompiler>();
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_ = std::make_unique<ShaderReflectionBuilder>();

        ID3D12PipelineState* forwardPipelineState_ = nullptr;
        ID3D12PipelineState* gBufferPipelineState_ = nullptr;
        BlendMode currentBlendMode_ = BlendMode::kBlendModeNone;
        D3D12_GPU_VIRTUAL_ADDRESS cameraCBV_ = 0;

        LightManager* lightManager_ = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE environmentMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE shadowMapHandle_ = {};
        D3D12_GPU_VIRTUAL_ADDRESS   lightViewProjectionCBV_ = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE irradianceMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE prefilteredMapHandle_ = {};
        D3D12_GPU_DESCRIPTOR_HANDLE brdfLUTHandle_ = {};

        // IBL シーンパラメータ定数バッファ（environmentRotation）
        Microsoft::WRL::ComPtr<ID3D12Resource> iblParamsBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS              iblParamsCBVAddress_ = 0;
        Vector3 iblRotation_ = {};

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> forwardReflectionData_;
        std::unique_ptr<ShaderReflectionData> gBufferReflectionData_;
        bool isInGBufferPass_ = false;
    };
}
