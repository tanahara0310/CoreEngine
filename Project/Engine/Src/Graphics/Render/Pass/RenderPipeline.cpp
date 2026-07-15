#include "pch.h"
#include "RenderPipeline.h"

// 各パスのリソース宣言は RenderPass::DeclareResources へ移設済み。
// ここに残る具象パス include は以下の理由による暫定依存（Phase C/F で削除予定）:
//  - BackBufferPass:      最終入力論理リソース名の接続（ConfigureBackBufferInput）
//  - PostEffectPass:      有効エフェクト列のノード分解（AppendPostEffectPasses）
//  - GeometryPass/DeferredLightingPass: Deferred/Forward 境界のクリア制御ハック
#include "BackBufferPass.h"
#include "DeferredLightingPass.h"
#include "GeometryPass.h"
#include "PostEffectPass.h"
#include "Graphics/Common/Core/DepthStencilManager.h"
#include "Graphics/RayTracing/RayTracingShadowManager.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/RenderTarget/BackBufferRenderTarget.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetNames.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Shadow/ShadowMapManager.h"
#include "Graphics/PostEffect/Effect/PostEffectBase.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/LensFlare/LensFlare.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Camera/CameraManager.h"
#include "Camera/ICamera.h"
#include "Scene/IScene.h"
#include "Scene/SceneManager.h"
#include "Math/MathCore.h"
#include <algorithm>

namespace CoreEngine
{
    namespace {
        D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneColorHandle(const RenderContext& context, const std::string& resourceName)
        {
            if (resourceName.empty()) {
                return {};
            }

            if (const FrameBlackboardResource* resource = context.frameBlackboard
                ? context.frameBlackboard->GetResource(resourceName)
                : nullptr;
                resource && resource->srvHandle.ptr != 0) {
                return resource->srvHandle;
            }

            if (!context.renderTargetManager) {
                return {};
            }

            if (RenderTarget* target = context.renderTargetManager->GetRenderTarget(resourceName)) {
                return target->GetSRVHandle();
            }

            return {};
        }

        // RenderPassPhase（パス挿入フェーズ）を GpuTimingCategory（タイミング表示カテゴリ）へ変換する。
        // フェーズは 1:1 対応のため、パス追加時に UI 側の分類テーブルを編集する必要がない。
        GpuTimingCategory ToGpuTimingCategory(RenderPassPhase phase)
        {
            switch (phase) {
            case RenderPassPhase::FrameSetup:    return GpuTimingCategory::Setup;
            case RenderPassPhase::Shadow:        return GpuTimingCategory::Shadow;
            case RenderPassPhase::GBuffer:       return GpuTimingCategory::GBuffer;
            case RenderPassPhase::PreLighting:   return GpuTimingCategory::PreLighting;
            case RenderPassPhase::Lighting:      return GpuTimingCategory::Lighting;
            case RenderPassPhase::PostLighting:  return GpuTimingCategory::PostLighting;
            case RenderPassPhase::Sky:           return GpuTimingCategory::Sky;
            case RenderPassPhase::Transparent:   return GpuTimingCategory::Transparent;
            case RenderPassPhase::Water:         return GpuTimingCategory::Water;
            case RenderPassPhase::PostProcess:   return GpuTimingCategory::PostProcess;
            case RenderPassPhase::Overlay:       return GpuTimingCategory::Overlay;
            case RenderPassPhase::Final:         return GpuTimingCategory::Final;
            default:                             return GpuTimingCategory::Setup;
            }
        }

        // 太陽（無限遠の方向光源）のスクリーン UV を計算し、LensFlare へ渡す。
        // レンズフレアの光源を太陽に限定するため（パーティクル等の高輝度オブジェクトを
        // フレア源にしない）、毎フレームここで太陽の投影位置を更新する。
        void UpdateLensFlareSunPosition(const RenderContext& context)
        {
            if (!context.postEffectManager) {
                return;
            }
            auto* lensFlare = context.postEffectManager->GetEffect<LensFlare>(PostEffectNames::LensFlare);
            if (!lensFlare) {
                return;
            }

            // 実際の描画に使われるカメラと同じものを使うこと。
            // GeometryPass 等は sceneManager->GetGameViewCamera3D()（エディタではデバッグカメラ）
            // を RenderManager へ渡しており、CameraManager のアクティブカメラとは別物になり得る。
            // 別のカメラで太陽を投影するとマスク位置がずれ、太陽を直視してもフレアが出なくなる。
            const ICamera* camera = context.sceneManager
                ? context.sceneManager->GetGameViewCamera3D()
                : nullptr;
            if (!camera && context.cameraManager) {
                camera = context.cameraManager->GetActiveCamera(CameraType::Camera3D);
            }
            if (!camera || !context.atmosphereManager) {
                lensFlare->SetSunScreenPosition(0.5f, 0.5f, false);
                return;
            }

            // AtmosphereManager の sunDirection は光の進行方向。太陽の見える方向はその逆。
            const Vector3 sunDir = context.atmosphereManager->GetSunDirection();
            const Vector3 toSun = { -sunDir.x, -sunDir.y, -sunDir.z };

            const Matrix4x4 vp = MathCore::Matrix::Multiply(
                camera->GetViewMatrix(), camera->GetProjectionMatrix());

            // 無限遠の方向ベクトルとして投影（w=0 の行ベクトル変換 = 平行移動行を無視）
            const float clipX = toSun.x * vp.m[0][0] + toSun.y * vp.m[1][0] + toSun.z * vp.m[2][0];
            const float clipY = toSun.x * vp.m[0][1] + toSun.y * vp.m[1][1] + toSun.z * vp.m[2][1];
            const float clipW = toSun.x * vp.m[0][3] + toSun.y * vp.m[1][3] + toSun.z * vp.m[2][3];

            if (clipW <= 1e-5f) {
                // 太陽がカメラ後方にある場合はフレアを出さない
                lensFlare->SetSunScreenPosition(0.5f, 0.5f, false);
                return;
            }

            const float u = clipX / clipW * 0.5f + 0.5f;
            const float v = -clipY / clipW * 0.5f + 0.5f;
            lensFlare->SetSunScreenPosition(u, v, true);
        }

        void EnsureSceneColorTarget(const RenderContext& context)
        {
            if (!context.renderTargetManager || context.viewSettings.sceneColorTargetName.empty()) {
                return;
            }

            if (context.renderTargetManager->HasRenderTarget(context.viewSettings.sceneColorTargetName)) {
                return;
            }

            RenderTargetDescriptor desc(context.viewSettings.sceneColorTargetName);
            context.renderTargetManager->CreateRenderTarget(desc);
        }

        void EnsureWaterCausticsTarget(const RenderContext& context)
        {
            if (!context.renderTargetManager) {
                return;
            }

            if (context.renderTargetManager->HasRenderTarget(RenderTargetNames::WaterCausticsBuffer)) {
                return;
            }

            RenderTargetDescriptor desc(RenderTargetNames::WaterCausticsBuffer);
            desc.needsDepthStencil = false;
            desc.clearColor[0] = 0.0f;
            desc.clearColor[1] = 0.0f;
            desc.clearColor[2] = 0.0f;
            desc.clearColor[3] = 1.0f;
            context.renderTargetManager->CreateRenderTarget(desc);
        }
    }

    RenderPass* RenderPipeline::AddPass(
        std::unique_ptr<RenderPass> pass,
        RenderPassPhase phase,
        int priority)
    {
        if (!pass) {
            return nullptr;
        }

        RenderPassEntry entry;
        entry.pass = std::move(pass);
        entry.phase = phase;
        entry.priority = priority;
        entry.sequence = nextSequence_++;
        entry.owner = activeOwner_;
        RenderPass* passPtr = entry.pass.get();

        // (phase, priority, 登録順) で決まる位置へ挿入し、passes_ を常にソート済みに保つ。
        const auto insertPos = std::find_if(
            passes_.begin(), passes_.end(),
            [&entry](const RenderPassEntry& existing) {
                if (existing.phase != entry.phase) {
                    return existing.phase > entry.phase;
                }
                return existing.priority > entry.priority;
            });
        passes_.insert(insertPos, std::move(entry));

        return passPtr;
    }

    void RenderPipeline::RemovePass(RenderPass* pass)
    {
        if (!pass) {
            return;
        }

        passes_.erase(
            std::remove_if(passes_.begin(), passes_.end(),
                [pass](const RenderPassEntry& entry) { return entry.pass.get() == pass; }),
            passes_.end());
    }

    void RenderPipeline::RemovePassesByOwner(const void* owner)
    {
        if (!owner) {
            return;
        }

        passes_.erase(
            std::remove_if(passes_.begin(), passes_.end(),
                [owner](const RenderPassEntry& entry) { return entry.owner == owner; }),
            passes_.end());
    }

    RenderPass* RenderPipeline::GetPass(const std::string& name)
    {
        for (auto& entry : passes_) {
            if (entry.pass->GetName() == name) {
                return entry.pass.get();
            }
        }
        return nullptr;
    }

    void RenderPipeline::PrepareFrame(const RenderContext& context)
    {
        RegisterFrameResources(context);

        // レンズフレアの光源を太陽に限定するため、太陽のスクリーン位置を毎フレーム更新
        UpdateLensFlareSunPosition(context);

        // 各パス自身の View 依存設定（出力先ターゲット名など）を反映する。
        for (auto& entry : passes_) {
            entry.pass->ConfigureForView(context);
        }

        // Deferred / Forward 境界の暫定連携（Phase F で投入時キュー振り分けへ移行し削除予定）。
        // DeferredLighting が SceneColor へ書き込む場合、Geometry はクリアせず上乗せし、
        // 不透明 Model/SkinnedModel の forward 二重描画をスキップする。
        const auto* deferredLightingPass = GetPass<DeferredLightingPass>();
        const bool deferredEnabled = deferredLightingPass && deferredLightingPass->IsEnabled();

        if (auto* geometryPass = GetPass<GeometryPass>()) {
            geometryPass->SetClearEnabled(!deferredEnabled);
        }

        if (context.renderManager) {
            context.renderManager->SetDeferredLightingActive(deferredEnabled);
        }

        BuildRenderGraph(context);
    }

    void RenderPipeline::RegisterFrameResources(const RenderContext& context)
    {
        if (!context.frameBlackboard) {
            return;
        }

        EnsureSceneColorTarget(context);
        EnsureWaterCausticsTarget(context);

        if (context.depthStencilManager) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::SceneDepth,
                context.depthStencilManager->GetDepthSRVHandle(),
                context.depthStencilManager->GetDepthStencilResource(),
                &context.depthStencilManager->GetCurrentState());
        }

        if (context.renderTargetManager) {
            const std::vector<PostEffectBase*>* enabledEffects = context.postEffectManager
                ? &context.postEffectManager->GetEnabledEffects()
                : nullptr;
            const size_t intermediateCount = (enabledEffects && enabledEffects->size() > 1)
                ? (enabledEffects->size() - 1)
                : 0;

            context.renderTargetManager->EnsurePostEffectIntermediateTargets(intermediateCount);
            context.renderTargetManager->EnsurePostEffectFinalTarget();

            if (RenderTarget* sceneColorTarget = context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName)) {
                D3D12_RESOURCE_STATES* sceneColorState = nullptr;
                if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(sceneColorTarget)) {
                    sceneColorState = &offscreen->GetCurrentState();
                }

                context.frameBlackboard->SetResource(
                    FrameBlackboard::SceneColor,
                    sceneColorTarget->GetSRVHandle(),
                    sceneColorTarget->GetResource(),
                    sceneColorState);
            }

            if (RenderTarget* sceneColorSnapshotTarget = context.renderTargetManager->GetRenderTarget(RenderTargetNames::SceneColorSnapshot)) {
                D3D12_RESOURCE_STATES* sceneColorSnapshotState = nullptr;
                if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(sceneColorSnapshotTarget)) {
                    sceneColorSnapshotState = &offscreen->GetCurrentState();
                }

                context.frameBlackboard->SetResource(
                    FrameBlackboard::SceneColorSnapshot,
                    sceneColorSnapshotTarget->GetSRVHandle(),
                    sceneColorSnapshotTarget->GetResource(),
                    sceneColorSnapshotState);
            }

            for (size_t index = 0; index < intermediateCount; ++index) {
                if (RenderTarget* postEffectIntermediateTarget = context.renderTargetManager->GetPostEffectIntermediateTarget(index)) {
                    D3D12_RESOURCE_STATES* stateRef = nullptr;
                    if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(postEffectIntermediateTarget)) {
                        stateRef = &offscreen->GetCurrentState();
                    }

                    context.frameBlackboard->SetResource(
                        FrameBlackboard::MakePostEffectIntermediateName(index),
                        postEffectIntermediateTarget->GetSRVHandle(),
                        postEffectIntermediateTarget->GetResource(),
                        stateRef);
                }
            }

            if (RenderTarget* postEffectFinalTarget = context.renderTargetManager->GetPostEffectFinalTarget()) {
                D3D12_RESOURCE_STATES* finalState = nullptr;
                if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(postEffectFinalTarget)) {
                    finalState = &offscreen->GetCurrentState();
                }

                context.frameBlackboard->SetResource(
                    FrameBlackboard::PostEffectFinal,
                    postEffectFinalTarget->GetSRVHandle(),
                    postEffectFinalTarget->GetResource(),
                    finalState);
            }

            if (RenderTarget* backBufferTarget = context.renderTargetManager->GetRenderTarget(RenderTargetNames::BackBuffer)) {
                D3D12_RESOURCE_STATES* backBufferState = nullptr;
                if (auto* backBuffer = dynamic_cast<BackBufferRenderTarget*>(backBufferTarget)) {
                    backBufferState = &backBuffer->GetCurrentState();
                }

                context.frameBlackboard->SetResource(
                    FrameBlackboard::BackBuffer,
                    backBufferTarget->GetSRVHandle(),
                    backBufferTarget->GetResource(),
                    backBufferState);
            }

            if (RenderTarget* waterCausticsTarget = context.renderTargetManager->GetRenderTarget(RenderTargetNames::WaterCausticsBuffer)) {
                D3D12_RESOURCE_STATES* waterCausticsState = nullptr;
                if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(waterCausticsTarget)) {
                    waterCausticsState = &offscreen->GetCurrentState();
                }

                context.frameBlackboard->SetResource(
                    FrameBlackboard::WaterCaustics,
                    waterCausticsTarget->GetSRVHandle(),
                    waterCausticsTarget->GetResource(),
                    waterCausticsState);
            }
        }

        if (context.shadowMapManager) {
            context.frameBlackboard->SetResource(
                FrameBlackboard::ShadowMap,
                context.shadowMapManager->GetSRVHandle(),
                context.shadowMapManager->GetShadowMapResource(),
                &context.shadowMapManager->GetCurrentState());
        }

        // RTShadowMask は View 依存の実体を持つため、現在の View に対応する
        // リソースをここで登録する（旧 RenderGraph::ResolveResources の特例を移設）。
        if (context.rtShadowManager) {
            const RayTracingShadowManager::ViewID rtShadowViewId =
                (context.currentRTShadowViewId == static_cast<uint32_t>(RayTracingShadowManager::ViewID::ReflectionView))
                ? RayTracingShadowManager::ViewID::ReflectionView
                : RayTracingShadowManager::ViewID::GameView;
            context.frameBlackboard->SetResource(
                FrameBlackboard::RTShadowMask,
                context.rtShadowManager->GetShadowSRVHandle(rtShadowViewId, 0),
                context.rtShadowManager->GetShadowResource(rtShadowViewId, 0),
                &context.rtShadowManager->GetShadowCurrentState(rtShadowViewId, 0));
        }

        if (context.gBufferManager) {
            const struct {
                const char* logicalName;
                GBufferManager::Target target;
            } gBufferTargets[] = {
                { FrameBlackboard::GBufferAlbedoAO, GBufferManager::Target::AlbedoAO },
                { FrameBlackboard::GBufferNormalRoughness, GBufferManager::Target::NormalRoughness },
                { FrameBlackboard::GBufferEmissiveMetallic, GBufferManager::Target::EmissiveMetallic },
                { FrameBlackboard::GBufferWorldPosition, GBufferManager::Target::WorldPosition },
                { FrameBlackboard::GBufferMotionVector, GBufferManager::Target::MotionVector },
            };

            for (const auto& entry : gBufferTargets) {
                context.frameBlackboard->SetResource(
                    entry.logicalName,
                    context.gBufferManager->GetSRVHandle(entry.target),
                    context.gBufferManager->GetResource(entry.target),
                    &context.gBufferManager->GetCurrentState(entry.target));
            }
        }
    }

    void RenderPipeline::BuildRenderGraph(const RenderContext& context)
    {
        renderGraph_.Reset();
        postEffectSubpasses_.clear();
        finalDisplayResourceName_ = FrameBlackboard::SceneColor;
        ConfigureBackBufferInput(FrameBlackboard::SceneColor);

        // 各パスの Read / Write 宣言（DeclareResources）のみから Graph を構築する。
        // 登録順は (phase, priority, 登録順) でソート済みの passes_ に従い、
        // 実行順・バリアは RenderGraph が宣言から導出する。
        for (auto& entry : passes_) {
            if (!entry.pass->IsEnabledForView(context.viewSettings)) {
                continue;
            }

            // PostEffect の placeholder は直接登録せず、有効エフェクト列をノード列へ分解する。
            if (dynamic_cast<PostEffectPass*>(entry.pass.get())) {
                AppendPostEffectPasses(context);
                continue;
            }

            RenderPass* passPtr = entry.pass.get();
            renderGraph_.AddPass(passPtr->GetName(), passPtr, [passPtr, &context](RenderGraphBuilder& builder) {
                passPtr->DeclareResources(builder, context);
                }, ToGpuTimingCategory(entry.phase));
        }

        renderGraph_.Compile(context);
    }

    void RenderPipeline::AppendPostEffectPasses(const RenderContext& context)
    {
        if (!context.postEffectManager) {
            return;
        }

        const std::vector<PostEffectBase*>& enabledEffects = context.postEffectManager->GetEnabledEffects();
        const std::vector<std::string>& enabledEffectNames = context.postEffectManager->GetEnabledEffectNames();
        if (enabledEffects.empty()) {
            return;
        }

        std::string currentInput = FrameBlackboard::SceneColor;
        size_t effectIndex = 0;

        for (PostEffectBase* effect : enabledEffects) {
            if (!effect) {
                continue;
            }

            const bool isLastEffect = (effectIndex + 1 >= enabledEffects.size());
            const std::string outputResource = isLastEffect
                ? std::string(FrameBlackboard::PostEffectFinal)
                : FrameBlackboard::MakePostEffectIntermediateName(effectIndex);

            // 登録名（"LensFlare"/"Bloom"/"Outline" 等）をそのままパス名として使う。
            // タイミング表示やデバッグログでエフェクトを個別に識別できるようにするため。
            const std::string effectName = (effectIndex < enabledEffectNames.size())
                ? enabledEffectNames[effectIndex]
                : (std::string("PostEffect_") + std::to_string(effectIndex));

            auto postEffectPass = std::make_unique<PostEffectPass>();
            postEffectPass->SetEffect(effect, effectName);
            postEffectPass->SetInputResourceName(currentInput);
            postEffectPass->SetOutputResourceName(outputResource);
            PostEffectPass* passPtr = postEffectPass.get();
            postEffectSubpasses_.push_back(std::move(postEffectPass));

            renderGraph_.AddPass(effectName, passPtr, [passPtr, &context](RenderGraphBuilder& builder) {
                passPtr->DeclareResources(builder, context);
                }, GpuTimingCategory::PostProcess);

            currentInput = outputResource;
            ++effectIndex;
        }

        ConfigureBackBufferInput(currentInput);
    }

    void RenderPipeline::ConfigureBackBufferInput(const std::string& finalPostEffectResource)
    {
        finalDisplayResourceName_ = finalPostEffectResource;

        if (auto* backBufferPass = GetPass<BackBufferPass>()) {
            backBufferPass->SetInputResourceName(finalPostEffectResource);
        }
    }

    void RenderPipeline::SyncFinalDisplayHandle(const RenderContext& context)
    {
        if (!context.postEffectManager || !context.frameBlackboard) {
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE finalHandle{};
        if (context.frameBlackboard->TryGetSrvHandle(finalDisplayResourceName_, finalHandle)) {
            context.postEffectManager->SetFinalDisplayTextureHandle(finalHandle);
        }
    }

    void RenderPipeline::ExecuteRenderGraph(const RenderContext& context)
    {
        RenderGraphContext graphContext;
        graphContext.renderContext = &context;

        renderGraph_.Execute(graphContext);
        SyncFinalDisplayHandle(context);
    }

    void RenderPipeline::ExecuteView(
        const RenderContext& context,
        const std::function<void()>& beforeExecute,
        const std::function<void()>& afterExecute)
    {
        PrepareFrame(context);

        if (beforeExecute) {
            beforeExecute();
        }

        ExecuteRenderGraph(context);

        if (afterExecute) {
            afterExecute();
        }
    }

    RenderViewResult RenderPipeline::ExecuteRenderView(
        const RenderContext& context,
        const std::function<void()>& beforeExecute,
        const std::function<void()>& afterExecute)
    {
        ExecuteView(context, beforeExecute, afterExecute);
        return BuildRenderViewResult(context);
    }

    RenderViewResult RenderPipeline::BuildRenderViewResult(const RenderContext& context) const
    {
        RenderViewResult result{};
        result.name = RenderTargetNames::SceneColor;
        result.outputTargetName = context.viewSettings.sceneColorTargetName;

        if (!context.renderTargetManager) {
            return result;
        }

        RenderTarget* viewTarget = context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName);
        if (!viewTarget) {
            return result;
        }

        result.name = context.viewSettings.sceneColorTargetName;
        result.viewSrv = viewTarget->GetSRVHandle();

        if (context.depthStencilManager) {
            result.sceneDepthSrv = context.depthStencilManager->GetDepthSRVHandle();
        }

        result.sceneColorSrv = ResolveSceneColorHandle(context, finalDisplayResourceName_);
        if (result.sceneColorSrv.ptr == 0) {
            result.sceneColorSrv = ResolveSceneColorHandle(context, context.viewSettings.sceneColorTargetName);
        }

        result.isValid = result.viewSrv.ptr != 0;
        return result;
    }

    void RenderPipeline::Clear()
    {
        passes_.clear();
    }
}
