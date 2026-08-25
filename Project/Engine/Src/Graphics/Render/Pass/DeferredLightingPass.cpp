#include "pch.h"
#include "DeferredLightingPass.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Render/RenderingTechnique/Lighting/DeferredLightingTechnique.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Camera/View/ViewInfo.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Render/RenderGraph.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
    void DeferredLightingPass::DeclareResources(RenderGraphBuilder& builder, const RenderContext& context)
    {
        // GBuffer / SSAO / RTShadow / SceneDepth を読み、SceneColor を生成する。
        builder.Read(FrameBlackboard::SceneDepth, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferAlbedoAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferNormalRoughness, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::GBufferEmissiveMetallic, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (context.viewSettings.enableSSAO) {
            builder.Read(FrameBlackboard::SSAO, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        builder.Read(FrameBlackboard::WaterCaustics, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        builder.Read(FrameBlackboard::RTWaterCaustics, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (context.viewSettings.enableRTShadow) {
            builder.Read(FrameBlackboard::RTShadowMask, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        builder.Write(FrameBlackboard::SceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }

    void DeferredLightingPass::ConfigureForView(const RenderContext& context)
    {
        SetRenderTargetName(context.viewSettings.sceneColorTargetName);
    }

    void DeferredLightingPass::Setup(const RenderContext& context)
    {
        if (!context.renderingTechniqueManager) {
            return;
        }

        auto* deferredLighting = context.renderingTechniqueManager->GetTechnique<DeferredLightingTechnique>(
            RenderingTechniqueNames::DeferredLighting);
        if (!deferredLighting) {
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

            // ===== 深度復元用 View*Projection 逆行列（ビューごとに毎回更新） =====
            // 実行中のビューの ViewInfo から取る。gCamera（フレーム 1 回しか書かれない CBV）
            // には頼らない。逆行列は ViewInfo 構築時に 1 回だけ計算済み。
            if (context.frameViews) {
                const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
                if (view.isValid) {
                    deferredLighting->UpdateDepthReconstruction(
                        context.viewSettings.viewType, view.invViewProjection);
                }
            }
        }

        // ===== IBL パラメータを GPU バッファに書き込む =====
        deferredLighting->UpdateIBLParams();

        // ===== RT シャドウマスクの設定（ライトごとに独立） =====
        D3D12_GPU_DESCRIPTOR_HANDLE mainLightMask{}; // フォワード受影用（メインライトのマスク）
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
                        if (li == 0) {
                            mainLightMask = srvHandle;
                        }
                    }
                }
            }
        } else {
            for (uint32_t li = 0; li < RayTracingShadowManager::kMaxDirectionalLights; ++li) {
                deferredLighting->SetRTShadowHandle({}, li);
            }
        }

        // ===== フォワード描画物（葉・草・水面等）の受影用にマスクを共有 =====
        // gRTShadowMask(t6) としてバインドされる。マスク未提供フレームは white1x1 に戻し、
        // シェーダ側の寸法ガード（1x1→影なし）へ倒す（t6 未バインドを防ぐ）。
        if (context.renderManager) {
            if (mainLightMask.ptr == 0) {
                mainLightMask = TextureManager::GetInstance().Load("white1x1.png").gpuHandle;
            }
            for (auto passType : { RenderPassType::Model, RenderPassType::SkinnedModel }) {
                if (auto* renderer = dynamic_cast<BaseModelRenderer*>(
                        context.renderManager->GetRenderer(passType))) {
                    renderer->SetRTShadowMask(mainLightMask);
                }
            }
        }

        // ===== SSAO SRV を渡す（SSAOPass の出力が入力として届いている場合） =====
        D3D12_GPU_DESCRIPTOR_HANDLE ssaoHandle{};
        bool hasBlackboardSSAO = false;
        if (context.frameBlackboard) {
            hasBlackboardSSAO = context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SSAO, ssaoHandle);
        }

        if (hasBlackboardSSAO) {
            deferredLighting->SetSSAOHandle(ssaoHandle);
        } else {
            deferredLighting->SetSSAOHandle({});
        }

        D3D12_GPU_DESCRIPTOR_HANDLE waterCausticsHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE rtWaterCausticsHandle{};
        bool hasWaterCaustics = false;
        bool hasRTWaterCaustics = false;
        if (context.frameBlackboard) {
            hasWaterCaustics = context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::WaterCaustics, waterCausticsHandle);
            hasRTWaterCaustics = context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::RTWaterCaustics, rtWaterCausticsHandle);
        }

        auto* caustics = context.renderingTechniqueManager->GetTechnique<WaterCausticsTechnique>(
            RenderingTechniqueNames::WaterCaustics);

        // コースティクスの合成入力を決める。テクニックが無効なら合成しない。
        // 生成方式（RT / スクリーンスペース）は Backend で明示的に選ぶ
        // （RT 出力があるだけで無条件優先すると、RT が 1 ピクセルも出さない状況で
        //  スクリーンスペース版へフォールバックできず完全に消える）。
        const bool causticsEnabled = (caustics == nullptr) || caustics->IsEnabled();
        const bool preferRayTracing =
            (caustics == nullptr) || caustics->GetBackend() == WaterCausticsTechnique::Backend::RayTracing;

        bool usingRT = false;
        if (!causticsEnabled) {
            deferredLighting->SetWaterCausticsHandle({});
        } else if (preferRayTracing && hasRTWaterCaustics) {
            usingRT = true;
            deferredLighting->SetWaterCausticsHandle(rtWaterCausticsHandle);
        } else if (!preferRayTracing && hasWaterCaustics) {
            deferredLighting->SetWaterCausticsHandle(waterCausticsHandle);
        } else if (hasRTWaterCaustics) {
            // 選択した方式の出力が未生成のフレームは、もう一方があればそれで代替する
            usingRT = true;
            deferredLighting->SetWaterCausticsHandle(rtWaterCausticsHandle);
        } else if (hasWaterCaustics) {
            deferredLighting->SetWaterCausticsHandle(waterCausticsHandle);
        } else {
            deferredLighting->SetWaterCausticsHandle({});
        }

        {
            DeferredLightingTechnique::WaterCausticsDebugSettings debugSettings{};
            if (caustics) {
                debugSettings.debugViewMode = caustics->GetParams().debugViewMode;
                debugSettings.debugDisplayScale = caustics->GetParams().debugDisplayScale;
            }

            // ===== 水中ライティング（直接光の二重計上排除） =====
            // RT コースティクス（＝完全な透過直接光）が合成されるフレームのみ、
            // 水中ピクセルのメインライト直接光を置換しアンビエントを Beer–Lambert で減衰させる。
            // スクリーンスペース版は透過直接光の全量を持たない（模様のみ）ため置換しない。
            const WaterSurfaceData* surface = context.waterSurfaceState;
            if (usingRT && surface && surface->regionValid != 0 && context.rtWaterCausticsManager) {
                const WaterCausticsRayTracingSettings& rtSettings =
                    context.rtWaterCausticsManager->GetSettings();
                debugSettings.waterVolumeEnabled = 1;
                debugSettings.waterHeight = surface->waterHeight;
                debugSettings.regionCenterXZ[0] = surface->regionCenterXZ[0];
                debugSettings.regionCenterXZ[1] = surface->regionCenterXZ[1];
                debugSettings.regionHalfExtentXZ[0] = surface->regionHalfExtentXZ[0];
                debugSettings.regionHalfExtentXZ[1] = surface->regionHalfExtentXZ[1];
                debugSettings.absorptionCoeff[0] = rtSettings.absorptionCoeff[0];
                debugSettings.absorptionCoeff[1] = rtSettings.absorptionCoeff[1];
                debugSettings.absorptionCoeff[2] = rtSettings.absorptionCoeff[2];
            }
            deferredLighting->SetWaterCausticsDebugSettings(debugSettings);
        }

        if (caustics && caustics->GetParams().debugLogEnabled != 0) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "DeferredLightingPass: WaterCaustics input approx=0x{:X} hasApprox={} rt=0x{:X} hasRT={} enabled={} backend={} usingRT={} debugViewMode={} debugScale={:.2f}",
                waterCausticsHandle.ptr,
                hasWaterCaustics,
                rtWaterCausticsHandle.ptr,
                hasRTWaterCaustics,
                causticsEnabled,
                static_cast<uint32_t>(caustics->GetBackend()),
                usingRT,
                caustics->GetParams().debugViewMode,
                caustics->GetParams().debugDisplayScale);
        }
    }

    void DeferredLightingPass::Execute(const RenderContext& context)
    {
        // 必須コンポーネントの確認
        if (!context.renderingTechniqueManager || !context.renderTargetManager
            || !context.gBufferManager || !context.dxCommon) {
            return;
        }

        auto* deferredLighting = context.renderingTechniqueManager->GetTechnique<DeferredLightingTechnique>(
            RenderingTechniqueNames::DeferredLighting);
        if (!deferredLighting) {
            return;
        }

        // GBuffer / SSAO / Shadow の情報からシーンカラーを生成する。
        D3D12_GPU_DESCRIPTOR_HANDLE outputHandle{};
        deferredLighting->Execute(context, outputHandle);

        // 結果を Blackboard に公開する。
        if (outputHandle.ptr != 0) {
            auto* target = context.renderTargetManager->GetRenderTarget(targetName_);

            if (context.frameBlackboard) {
                context.frameBlackboard->SetResource(
                    FrameBlackboard::SceneColor,
                    outputHandle,
                    target ? &target->Resource() : nullptr);
            }
        }
    }
}

