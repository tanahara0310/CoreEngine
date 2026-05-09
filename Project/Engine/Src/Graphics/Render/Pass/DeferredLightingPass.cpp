#include "DeferredLightingPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderingTechnique/Lighting/DeferredLightingTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void DeferredLightingPass::Execute(const RenderContext& context)
    {
        // 必須コンポーネントの確認
        if (!context.renderingTechniqueManager || !context.renderTargetManager
            || !context.gBufferManager || !context.dxCommon) {
            output_.Reset();
            return;
        }

        // DeferredLighting 技術を取得
        auto* deferredLighting = context.renderingTechniqueManager->GetTechnique<DeferredLightingTechnique>(
            RenderingTechniqueNames::DeferredLighting);
        if (!deferredLighting) {
            output_.Reset();
            return;
        }

        // 出力先レンダーターゲット名を設定
        deferredLighting->SetRenderTargetName(targetName_);

        // ===== カメラ CBV アドレス（GBufferPass で設定済み） =====
        if (context.renderManager) {
            // GBufferPass 時点でカメラ CBV を保持する BaseModelRenderer からアドレスを取得
            if (auto* modelRenderer = dynamic_cast<BaseModelRenderer*>(
                    context.renderManager->GetRenderer(RenderPassType::Model))) {
                deferredLighting->SetCameraCBVAddress(modelRenderer->GetCameraCBVAddress());
            }

            // シーン共通 IBL 回転を転送（スカイボックス回転と連動）
            deferredLighting->SetEnvironmentRotation(context.renderManager->GetIBLRotation());

            // 環境輝度スケールを転送（SkyBox intensity と連動）
            deferredLighting->SetIBLIntensity(context.renderManager->GetEnvironmentIntensity());
        }

        // ===== IBL パラメータを GPU バッファに書き込む =====
        deferredLighting->UpdateIBLParams();

        // ===== Shadow Map と LightViewProjection の設定 =====
        if (context.shadowMapManager) {
            // ライト VP 行列を GPU バッファに書き込む（毎フレーム更新）
            deferredLighting->UpdateLightViewProjection(
                context.shadowMapManager->GetLightViewProjection());
        }

        // ===== RT シャドウマスクの設定（ライトごとに独立） =====
        if (context.rtShadowManager && context.rtShadowManager->IsInitialized()) {
            auto viewId = static_cast<RayTracingShadowManager::ViewID>(context.currentRTShadowViewId);
            // 全ライト分のハンドルをリセットしてから有効なものをセット
            for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
                deferredLighting->SetRTShadowHandle({}, li);
            }
            for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
                if (context.rtShadowManager->IsDispatchedThisFrame(viewId, li)) {
                    auto srvHandle = context.rtShadowManager->GetShadowSRVHandle(viewId, li);
                    if (srvHandle.ptr != 0) {
                        deferredLighting->SetRTShadowHandle(srvHandle, li);
                    }
                }
            }
        } else {
            for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
                deferredLighting->SetRTShadowHandle({}, li);
            }
        }

        // ===== SSAO SRV を渡す（SSAOPass の出力が入力として届いている場合） =====
        if (input_.isValid && input_.srvHandle.ptr != 0) {
            deferredLighting->SetSSAOHandle(input_.srvHandle);
        } else {
            deferredLighting->SetSSAOHandle({});
        }

        // ===== ライティングパスを実行 =====
        D3D12_GPU_DESCRIPTOR_HANDLE outputHandle{};
        deferredLighting->Execute(context, outputHandle);

        // 結果を次のパスに渡す
        if (outputHandle.ptr != 0) {
            auto* target = context.renderTargetManager->GetRenderTarget(targetName_);
            output_.srvHandle = outputHandle;
            output_.resource  = target ? target->GetResource() : nullptr;
            output_.isValid   = true;
        } else {
            output_.Reset();
        }
    }
}

