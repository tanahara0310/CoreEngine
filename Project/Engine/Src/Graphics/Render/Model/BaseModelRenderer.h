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
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Graphics/Render/Model/ModelBindings.h"
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
    class ShaderBinder;
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
        void SetCamera(const Camera* camera) override;

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

    protected:
        /// @brief 現在のブレンドモードに応じたフォグ定数を選ぶ
        D3D12_GPU_VIRTUAL_ADDRESS SelectFogConstants() const;

    public:

        /// @brief フォワード受影用 RT シャドウマスク SRV を設定（gRTShadowMask t6）
        /// @details 毎フレーム DeferredLightingPass::Setup がメインライトのマスクを供給する。
        ///          マスク未提供フレームは white1x1（=影なし）が入る。
        void SetRTShadowMask(D3D12_GPU_DESCRIPTOR_HANDLE handle) { rtShadowMaskHandle_ = handle; }

        /// @brief 今フレームのフォグ定数を受け取る（gFog b4）
        /// @details FogPass が毎フレーム供給する。どれを差すかはブレンドモードで決まる
        ///          （不透明は全画面パスが掛けるので恒等、加算・スクリーンは減衰のみ）。
        void SetFogConstants(D3D12_GPU_VIRTUAL_ADDRESS full,
                             D3D12_GPU_VIRTUAL_ADDRESS additive,
                             D3D12_GPU_VIRTUAL_ADDRESS disabled)
        {
            fogFullCBV_ = full;
            fogAdditiveCBV_ = additive;
            fogDisabledCBV_ = disabled;
        }

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

        // 今フレームのフォグ定数（FogPass が供給。0 = 未供給で差さない）
        D3D12_GPU_VIRTUAL_ADDRESS fogFullCBV_ = 0;
        D3D12_GPU_VIRTUAL_ADDRESS fogAdditiveCBV_ = 0;
        D3D12_GPU_VIRTUAL_ADDRESS fogDisabledCBV_ = 0;

        // IBL シーンパラメータ定数バッファ（environmentRotation）
        Microsoft::WRL::ComPtr<ID3D12Resource> iblParamsBuffer_;
        D3D12_GPU_VIRTUAL_ADDRESS iblParamsCBVAddress_ = 0;

        // シェーダーリフレクションデータ
        std::unique_ptr<ShaderReflectionData> forwardReflectionData_;
        std::unique_ptr<ShaderReflectionData> gBufferReflectionData_;
        // 自身が最後に Begin したパス種別（PSO 復元とバッチ Flush 用の内部状態。
        // 描画側のパス判定は DrawViewInfo で明示的に渡されるため、外部へは公開しない）
        bool isInGBufferPass_ = false;

        // 解決済みルートパラメータ表（Initialize 後に一度だけ解決。添字は ModelBind::Slot）
        BindingTable forwardBindings_;
        BindingTable gBufferBindings_;

        InstanceBatchManager* instanceBatchManager_ = nullptr;
        ID3D12GraphicsCommandList* currentCommandList_ = nullptr;

        /// @brief Initialize 完了後に一度だけ呼び、宣言表をシェーダー実体と突き合わせる
        /// @param forwardDecls  フォワードパス用の宣言表（ModelBind::kForward 等）
        /// @param gBufferDecls  G-Buffer パス用の宣言表
        /// @param debugName     ログ用の識別名
        /// @throws std::runtime_error 契約違反（必須リソースの不在・種別違い）
        /// @note 派生クラスが自分のシェーダーに合う宣言表を渡す
        void ResolveBindings(
            const ShaderBindingDecl* forwardDecls,
            const ShaderBindingDecl* gBufferDecls,
            size_t count,
            const std::string& debugName);

        /// @brief フォワードパスのシーンレベルリソース（カメラ・ライト・IBL）を差す
        /// @param table エンジン既定 RS の表、またはカスタムシェーダーの表
        /// @note 既定パスとカスタムパスで処理が同じなので 1 箇所に集約している
        void BindForwardSceneResources(ShaderBinder& binder, const BindingTable& table);
    };
}
