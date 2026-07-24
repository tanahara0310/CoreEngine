#pragma once
#include "Graphics/Render/IRenderer.h"
#include "Graphics/Render/IGBufferRenderer.h"
#include "Graphics/Render/Model/IBLSceneParams.h"
#include "Graphics/Render/Model/IBLParameters.h"
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
    class InstanceBatchManager;
    class CustomShaderPipeline;
}

namespace CoreEngine
{
    /// @brief ModelRenderer / SkinnedModelRenderer 共通基底クラス
    class BaseModelRenderer : public IRenderer, public IGBufferRenderer {
    public:
        /// @brief フォワードパスを開始（RootSignature・PSO・シーン定数をバインド）
        void BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) override;
        /// @brief GBuffer パスを開始（GBuffer 用 RootSignature・PSO をバインド）
        void BeginGBufferPass(ID3D12GraphicsCommandList* cmdList) override; // IGBufferRenderer
        /// @brief パスを終了（GBuffer フラグをリセット）
        void EndPass() override;
        /// @brief カメラの GPU 仮想アドレスを取得して保持
        void SetCamera(const ICamera* camera) override;

        /// @brief フォワードパス用 RootSignature を取得
        ID3D12RootSignature* GetRootSignature() const { return forwardRootSignatureMg_->GetRootSignature(); }
        /// @brief GBuffer パス用 RootSignature を取得
        ID3D12RootSignature* GetGBufferRootSignature() const { return gBufferRootSignatureMg_->GetRootSignature(); }

        /// @brief 現在のパス（Forward/GBuffer）に応じたPSOを取得
        /// @note GPUスキニング(CS)Dispatch後にPSOを復元するために使用する
        ID3D12PipelineState* GetCurrentPipelineState() const {
            return isInGBufferPass_ ? gBufferPipelineState_ : forwardPipelineState_;
        }

        /// @brief ライトマネージャーを設定
        void SetLightManager(LightManager* lightManager) { lightManager_ = lightManager; }

        /// @brief IBL関連パラメータを一括設定
        /// @param params IBLパラメータ構造体
        void SetIBLParameters(const IBLParameters& params);

        /// @brief フォワード受影用 RT シャドウマスク SRV を設定（gRTShadowMask t6）
        /// @details 毎フレーム DeferredLightingPass::Setup がメインライトのマスクを供給する。
        ///          マスク未提供フレームは white1x1（=影なし）が入る。
        void SetRTShadowMask(D3D12_GPU_DESCRIPTOR_HANDLE handle) { rtShadowMaskHandle_ = handle; }

        /// @brief 環境マップテクスチャが設定済みか確認
        bool HasEnvironmentMap() const { return iblParams_.HasEnvironmentMap(); }

        /// @brief IBLに必要なテクスチャ（Irradiance / Prefiltered / BRDF LUT）が全て設定済みか確認
        bool HasIBLMaps() const { return iblParams_.IsFullyConfigured(); }

        /// @brief フォワードパスのリソース名からルートパラメータインデックスを取得（-1: 未登録）
        int GetRootParamIndex(const std::string& resourceName) const;
        /// @brief GBuffer パスのリソース名からルートパラメータインデックスを取得（-1: 未登録）
        int GetGBufferRootParamIndex(const std::string& resourceName) const;

        /// @brief モデル描画パケットをバインドして描画コマンドを発行する
        /// Model が組み立てた ModelDrawPacket を受け取り、現在のパス（Forward/GBuffer）に
        /// 応じたルートパラメータへのバインドと DrawIndexedInstanced の呼び出しを行う。
        /// @param customPipeline カスタムシェーダーパイプライン（nullptr の場合は標準インデックスを使用）
        void BindModelDrawPacket(ID3D12GraphicsCommandList* cmdList, const ModelDrawPacket& packet,
            const CustomShaderPipeline* customPipeline = nullptr);

        /// @brief カスタム PSO 適用後に既定 PSO をコマンドリストへ再設定する
        /// InstanceBatchManager::DrawBatch() がカスタム PSO を使用した後に呼び出す。
        void RestoreDefaultPSO(ID3D12GraphicsCommandList* cmdList);

        /// @brief カスタム RootSignature 切り替え後にシーンレベルのリソースを再バインドする
        /// D3D12 は SetGraphicsRootSignature を呼ぶと全バインドが無効になるため、
        /// カスタム RS のインデックスでカメラ・ライト・IBL 等を再設定する。
        void BindSceneResourcesWithCustomPipeline(
            ID3D12GraphicsCommandList* cmdList,
            const CustomShaderPipeline* customPipeline);

        /// @brief カメラ CBV の GPU 仮想アドレスを取得（DeferredLightingPass 連携用）
        D3D12_GPU_VIRTUAL_ADDRESS GetCameraCBVAddress() const { return cameraCBV_; }

        /// @brief インスタンシングバッチマネージャーを設定（ModelManager から注入）
        void SetInstanceBatchManager(InstanceBatchManager* manager) { instanceBatchManager_ = manager; }

        /// @brief ShaderCompiler を取得（CustomShaderPipeline 構築用）
        ShaderCompiler* GetShaderCompiler() { return shaderCompiler_.get(); }

        /// @brief ShaderReflectionBuilder を取得（CustomShaderPipeline 構築用）
        ShaderReflectionBuilder* GetReflectionBuilder() { return reflectionBuilder_.get(); }

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

        // IBL関連を構造体に集約
        IBLParameters iblParams_;

        D3D12_GPU_DESCRIPTOR_HANDLE rtShadowMaskHandle_ = {};

        // IBL シーンパラメータ定数バッファ（environmentRotation）
        Microsoft::WRL::ComPtr<ID3D12Resource> iblParamsBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS iblParamsCBVAddress_ = 0;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> forwardReflectionData_;
        std::unique_ptr<ShaderReflectionData> gBufferReflectionData_;
        // 自身が最後に Begin したパス種別（PSO 復元とバッチ Flush 用の内部状態。
        // 描画側のパス判定は DrawViewInfo で明示的に渡されるため、外部へは公開しない）
        bool isInGBufferPass_ = false;

        // キャッシュ済みルートパラメータインデックス（Initialize後に一度だけ解決）
        struct CachedIndices {
            // BeginPass (Forward/GBuffer 共通名) 用
            int camera = -1;
            int lightCounts = -1;
            int directionalLights = -1;
            int pointLights = -1;
            int spotLights = -1;
            int areaLights = -1;
            int envTexture = -1;
            int rtShadowMask = -1; ///< gRTShadowMask (t6) — フォワード受影用RTシャドウマスク
            int irradianceMap = -1;
            int prefilteredMap = -1;
            int brdfLUT = -1;
            int iblParams = -1;
            // BindModelDrawPacket 用
            int transform = -1;        ///< gTransformationMatrix (CBV) — スキニングモデル用
            int instanceData = -1;     ///< gInstanceData (Root SRV) — 通常モデル用インスタンシング
            int material = -1;
            int texture = -1;
            int normalMap = -1;
            int metallicRoughnessMap = -1; ///< gMetallicRoughnessMap (G=Roughness, B=Metallic)
            int emissiveMap = -1;
            int aoMap = -1;
            int matrixPalette = -1;
        };
        CachedIndices forwardCache_;
        CachedIndices gBufferCache_;

        InstanceBatchManager* instanceBatchManager_ = nullptr;
        ID3D12GraphicsCommandList* currentCommandList_ = nullptr;

        /// @brief Initialize 完了後に一度だけ呼び、全 Root Param インデックスをキャッシュする
        void CacheRootParamIndices();
    };
}
