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
#include "Graphics/PostEffect/Effect/PostEffectBase.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Utility/Logger/Logger.h"
#include <unordered_set>
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueNames.h"
#include "Graphics/Render/RenderingTechnique/TAA/TAATechnique.h"
#include "Graphics/Render/RenderingTechnique/CAS/CASTechnique.h"
#include "CASPass.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Camera/Camera.h"
#include "Camera/View/ViewBuilder.h"
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

        // TAA が今フレームこの View で走るか。
        // 「履歴ターゲットを用意するか」「ポストエフェクト列の入力を TAAOutput へ差し替えるか」の
        // 両方がこの判定を共有する必要があるため、1 箇所にまとめる。
        bool IsTAAActive(const RenderContext& context)
        {
            if (context.viewSettings.viewType != RenderViewType::GameView) {
                return false;
            }
            if (!context.renderingTechniqueManager) {
                return false;
            }

            const auto* taa = context.renderingTechniqueManager->GetTechnique<TAATechnique>(
                RenderingTechniqueNames::TAA);
            return taa && taa->IsEnabled();
        }

        // CAS が今フレームこの View で走るか
        bool IsCASActive(const RenderContext& context)
        {
            if (context.viewSettings.viewType != RenderViewType::GameView) {
                return false;
            }
            if (!context.renderingTechniqueManager) {
                return false;
            }

            const auto* cas = context.renderingTechniqueManager->GetTechnique<CASTechnique>(
                RenderingTechniqueNames::CAS);
            return cas && cas->IsEnabled();
        }

        void EnsureCASTarget(const RenderContext& context)
        {
            if (!context.renderTargetManager
                || context.renderTargetManager->HasRenderTarget(RenderTargetNames::CASOutput)) {
                return;
            }

            // 入力と同じ HDR フォーマット（既定）。CAS はトーンマップ前に掛かる
            RenderTargetDescriptor desc(RenderTargetNames::CASOutput);
            desc.needsDepthStencil = false;
            desc.clearColor[0] = 0.0f;
            desc.clearColor[1] = 0.0f;
            desc.clearColor[2] = 0.0f;
            desc.clearColor[3] = 1.0f;
            context.renderTargetManager->CreateRenderTarget(desc);
        }

        /// @brief Halton 列（radical inverse）
        /// @details 低食い違い量列。ランダムより偏りが少なく、少ないフレーム数で
        ///          ピクセル内を均等に埋められるため TAA のジッタに使われる。
        float RadicalInverse(uint32_t index, uint32_t base)
        {
            float result = 0.0f;
            const float invBase = 1.0f / static_cast<float>(base);
            float fraction = invBase;

            while (index > 0) {
                result += static_cast<float>(index % base) * fraction;
                index /= base;
                fraction *= invBase;
            }
            return result;
        }

        // TAA のジッタ周期。長いほど収束後の品質は上がるが、動きの多い画では
        // 履歴がすぐ棄却されるため差が出にくい。8 は一般的な妥協点。
        constexpr uint64_t kJitterSampleCount = 8;

        // カメラの射影行列へ今フレームのサブピクセルジッタを入れる。
        // 「実際にサンプル位置をずらして描く」のが TAA の本体で、
        // これが無いと履歴を混ぜても情報が増えずぼけるだけになる。
        //
        // 呼び出しは PrepareFrameViews から「ViewInfo を作る直前」に 1 回だけ。
        // ここで入れたジッタがスナップショットされ、以降フレーム内の全パスが同じ射影を使う。
        void UpdateCameraJitter(const RenderContext& context, Camera* camera)
        {
            if (!camera) {
                return;
            }

            if (!IsTAAActive(context)) {
                camera->SetProjectionJitter(0.0f, 0.0f);
                return;
            }

            const uint32_t width = context.gBufferManager ? context.gBufferManager->GetWidth() : 0;
            const uint32_t height = context.gBufferManager ? context.gBufferManager->GetHeight() : 0;
            if (width == 0 || height == 0) {
                camera->SetProjectionJitter(0.0f, 0.0f);
                return;
            }

            // Halton(2,3) を [-0.5, 0.5) のピクセルオフセットへ
            const uint32_t sampleIndex = static_cast<uint32_t>(context.frameNumber % kJitterSampleCount) + 1u;
            const float offsetPixelX = RadicalInverse(sampleIndex, 2u) - 0.5f;
            const float offsetPixelY = RadicalInverse(sampleIndex, 3u) - 0.5f;

            // ピクセル → NDC（NDC の幅 2.0 が画面幅に対応する）
            const float jitterNdcX = offsetPixelX * 2.0f / static_cast<float>(width);
            const float jitterNdcY = offsetPixelY * 2.0f / static_cast<float>(height);

            camera->SetProjectionJitter(jitterNdcX, jitterNdcY);

            // モーションベクターからジッタ分を差し引くための差分を TAA へ渡す
            if (context.renderingTechniqueManager) {
                if (auto* taa = context.renderingTechniqueManager->GetTechnique<TAATechnique>(
                        RenderingTechniqueNames::TAA)) {
                    taa->SetJitter(jitterNdcX, jitterNdcY, context.frameNumber);
                }
            }
        }

        void EnsureTAATargets(const RenderContext& context)
        {
            if (!context.renderTargetManager) {
                return;
            }

            for (uint32_t index = 0; index < 2; ++index) {
                const char* targetName = TAATechnique::GetHistoryTargetName(index);
                if (context.renderTargetManager->HasRenderTarget(targetName)) {
                    continue;
                }

                // SceneColor と同じ HDR フォーマット（RenderTargetDescriptor の既定）で作る。
                // トーンマップ前に解決するため、ここを LDR にすると白飛びが履歴に焼き付く。
                RenderTargetDescriptor desc(targetName);
                desc.needsDepthStencil = false;
                desc.clearColor[0] = 0.0f;
                desc.clearColor[1] = 0.0f;
                desc.clearColor[2] = 0.0f;
                desc.clearColor[3] = 1.0f;
                context.renderTargetManager->CreateRenderTarget(desc);
            }
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
        int priority,
        std::optional<GpuTimingCategory> timingCategoryOverride)
    {
        if (!pass) {
            return nullptr;
        }

        RenderPassEntry entry;
        entry.pass = std::move(pass);
        entry.phase = phase;
        entry.priority = priority;
        entry.timingCategoryOverride = timingCategoryOverride;
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

        InvalidateGraphSnapshots();
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

        InvalidateGraphSnapshots();
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

    void RenderPipeline::PrepareFrameViews(const RenderContext& context, FrameViews& outViews)
    {
        // ===== 「どのカメラで描くか」を決める唯一の場所 =====
        // 以前は BaseScene / RenderManager / RenderPipeline / SSAO がそれぞれ別の規則で
        // カメラを解決しており、「一致させること」というコメントで整合を守ろうとしていた。
        // ここで 1 回解決し、以降は ViewInfo という値を配る。
        Camera* camera = context.sceneManager
            ? context.sceneManager->GetGameViewCamera3D()
            : nullptr;

        // 射影行列へのジッタ注入は、この後の描画が作る WVP / invViewProj / 視錐台へ
        // 効かせる必要があるため、必ず ViewInfo のスナップショットより前に行う。
        UpdateCameraJitter(context, camera);

        outViews.Set(RenderViewType::GameView, ViewBuilder::Build(camera, RenderViewType::GameView));

        Camera* camera2D = context.sceneManager
            ? context.sceneManager->GetGameViewCamera2D()
            : nullptr;
        outViews.Set2D(ViewBuilder::Build(camera2D, RenderViewType::GameView));
    }

    void RenderPipeline::PrepareFrame(const RenderContext& context)
    {
        RegisterFrameResources(context);

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

        if (IsTAAActive(context)) {
            EnsureTAATargets(context);
        }

        if (IsCASActive(context)) {
            EnsureCASTarget(context);
        }

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
            // エフェクトは直前の出力しか読まないので、交互に使う 2 枚で足りる。
            // 以前は「有効エフェクト数 - 1」枚を確保していた
            const size_t intermediateCount = (enabledEffects && enabledEffects->size() > 1)
                ? RenderTargetManager::kPostEffectPingPongCount
                : 0;

            context.renderTargetManager->EnsurePostEffectIntermediateTargets(intermediateCount);
            context.renderTargetManager->EnsurePostEffectFinalTarget();
            context.renderTargetManager->LogAllocationIfChanged();

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

            // TAA 履歴の ping-pong を論理名へ束ねる。
            // 書き込み先の決定基準は TAATechnique::GetWriteHistoryIndex に一本化してあり、
            // ここと TAATechnique::Execute が同じ frameNumber から同じ答えを出す。
            if (IsTAAActive(context)) {
                const uint32_t writeIndex = TAATechnique::GetWriteHistoryIndex(context.frameNumber);

                const struct {
                    const char* logicalName;
                    uint32_t historyIndex;
                } taaTargets[] = {
                    { FrameBlackboard::TAAOutput,  writeIndex },
                    { FrameBlackboard::TAAHistory, 1u - writeIndex },
                };

                for (const auto& entry : taaTargets) {
                    RenderTarget* taaTarget = context.renderTargetManager->GetRenderTarget(
                        TAATechnique::GetHistoryTargetName(entry.historyIndex));
                    if (!taaTarget) {
                        continue;
                    }

                    D3D12_RESOURCE_STATES* taaState = nullptr;
                    if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(taaTarget)) {
                        taaState = &offscreen->GetCurrentState();
                    }

                    context.frameBlackboard->SetResource(
                        entry.logicalName,
                        taaTarget->GetSRVHandle(),
                        taaTarget->GetResource(),
                        taaState);
                }
            }

            if (IsCASActive(context)) {
                if (RenderTarget* casTarget = context.renderTargetManager->GetRenderTarget(RenderTargetNames::CASOutput)) {
                    D3D12_RESOURCE_STATES* casState = nullptr;
                    if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(casTarget)) {
                        casState = &offscreen->GetCurrentState();
                    }

                    context.frameBlackboard->SetResource(
                        FrameBlackboard::CASOutput,
                        casTarget->GetSRVHandle(),
                        casTarget->GetResource(),
                        casState);
                }
            }
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

        // シーン画の受け渡し連鎖を 1 箇所で組み立てる:
        //   SceneColor →(TAA)→ TAAOutput →(CAS)→ CASOutput → ポストエフェクト列 → BackBuffer
        // 各段が無効なら、その段を飛ばして前段の論理名がそのまま次段へ渡る。
        const char* sceneImage = FrameBlackboard::SceneColor;

        if (IsTAAActive(context)) {
            sceneImage = FrameBlackboard::TAAOutput;
        }

        if (IsCASActive(context)) {
            // CAS の入力は前段の結果。宣言（DeclareResources）と実際の読み先を
            // 一致させるため、Graph へ登録する前にここで確定させる。
            if (auto* casPass = GetPass<CASPass>()) {
                casPass->SetInputResourceName(sceneImage);
            }
            sceneImage = FrameBlackboard::CASOutput;
        }

        sceneImageResourceName_ = sceneImage;
        finalDisplayResourceName_ = sceneImage;
        ConfigureBackBufferInput(sceneImage);

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
                }, entry.timingCategoryOverride.value_or(ToGpuTimingCategory(entry.phase)));
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

        // 必須の追加入力が今フレームの Blackboard に無いエフェクトは、パス自体を作らずチェーンから外す。
        // Execute の時点で飛ばすと「出力リソースが書かれないまま次段がそれを読む」ことになり、
        // 画面が丸ごと消える（不変条件「必須入力が欠けてもクラッシュも黒画面も出さない」に反する）。
        struct ChainEntry { PostEffectBase* effect; std::string name; };
        std::vector<ChainEntry> chain;
        chain.reserve(enabledEffects.size());

        std::vector<PostEffectInputBinding> declaredInputs;
        for (size_t index = 0; index < enabledEffects.size(); ++index) {
            PostEffectBase* effect = enabledEffects[index];
            if (!effect) {
                continue;
            }

            const std::string effectName = (index < enabledEffectNames.size())
                ? enabledEffectNames[index]
                : (std::string("PostEffect_") + std::to_string(index));

            declaredInputs.clear();
            effect->DeclareExtraInputs(declaredInputs);

            bool inputsAvailable = true;
            for (const PostEffectInputBinding& binding : declaredInputs) {
                if (!binding.required) {
                    continue;
                }
                const bool resolvable = binding.logicalName && context.frameBlackboard
                    && context.frameBlackboard->HasResource(binding.logicalName);
                if (!resolvable) {
                    // 毎フレーム出すとログが埋まるので組み合わせごとに 1 回だけ警告する
                    static std::unordered_set<std::string> warnedMissingInputs;
                    std::string key = effectName + "/" + (binding.logicalName ? binding.logicalName : "(null)");
                    if (warnedMissingInputs.insert(key).second) {
                        Logger::GetInstance().Warnf(LogCategory::Graphics,
                            "[PostEffect] {} が要求する入力 {} が今フレームに無いため、チェーンから除外します",
                            effectName, binding.logicalName ? binding.logicalName : "(null)");
                    }
                    inputsAvailable = false;
                    break;
                }
            }

            if (inputsAvailable) {
                chain.push_back({ effect, effectName });
            }
        }

        if (chain.empty()) {
            // 全て外れた場合はポストエフェクトを挟まずシーン画像をそのまま最終出力にする
            ConfigureBackBufferInput(sceneImageResourceName_);
            return;
        }

        // 一時ターゲットの払い出しはフレーム単位。ここでカーソルを戻す
        postEffectTransientPool_.BeginFrame();

        // 一時ターゲットの解像度スケールの基準になるフル解像度
        uint32_t baseWidth = 0;
        uint32_t baseHeight = 0;
        if (RenderTarget* sceneColorTarget =
                context.renderTargetManager->GetRenderTarget(context.viewSettings.sceneColorTargetName)) {
            baseWidth  = static_cast<uint32_t>(sceneColorTarget->GetWidth());
            baseHeight = static_cast<uint32_t>(sceneColorTarget->GetHeight());
        }

        std::string currentInput = sceneImageResourceName_;
        size_t effectIndex = 0;

        for (const ChainEntry& entry : chain) {
            const bool isLastEffect = (effectIndex + 1 >= chain.size());
            // 中間は 2 枚を交互に使う。入力と出力が必ず別実体になるので、
            // 同一リソースを読みながら書く事故が起きない
            const std::string outputResource = isLastEffect
                ? std::string(FrameBlackboard::PostEffectFinal)
                : FrameBlackboard::MakePostEffectIntermediateName(
                    effectIndex % RenderTargetManager::kPostEffectPingPongCount);

            // エフェクト自身にパス列を積ませる。単一パスのエフェクトは基底の既定実装が
            // 従来どおり 1 パスだけ積むので、ここでの扱いは多段エフェクトと同じで済む。
            PostEffectGraphBuilder builder(
                context, postEffectTransientPool_, currentInput, outputResource, baseWidth, baseHeight);
            entry.effect->BuildPasses(builder);

            const std::vector<PostEffectStep>& steps = builder.Steps();
            if (steps.empty()) {
                // パスを 1 つも積まなかったエフェクトは出力を書かない。currentInput を進めないので
                // 次段は前段の出力をそのまま読み、画像は途切れない
                Logger::GetInstance().Errorf(LogCategory::Graphics,
                    "[PostEffect] {} が 1 つもパスを積みませんでした。このエフェクトは無視されます", entry.name);
                ++effectIndex;
                continue;
            }

            for (const PostEffectStep& step : steps) {
                // 単一パスのエフェクトは登録名をそのままノード名にする（従来の表示を保つ）。
                // 多段のときは step 名を使い、ノードエディタで個別に識別できるようにする
                const std::string passName = (steps.size() == 1) ? entry.name : step.name;

                auto postEffectPass = std::make_unique<PostEffectPass>();
                postEffectPass->SetStep(entry.effect, entry.name, step);
                PostEffectPass* passPtr = postEffectPass.get();
                postEffectSubpasses_.push_back(std::move(postEffectPass));

                renderGraph_.AddPass(passName, passPtr, [passPtr, &context](RenderGraphBuilder& graphBuilder) {
                    passPtr->DeclareResources(graphBuilder, context);
                    }, GpuTimingCategory::PostProcess);
            }

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

        // 実行後に取る。実行フラグ・発行済みバリア・未解決リソースまで含めるため。
        if (graphCaptureEnabled_ && !graphCapturePaused_) {
            CaptureGraphSnapshot(context);
        }

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
        InvalidateGraphSnapshots();
    }

    void RenderPipeline::CaptureGraphSnapshot(const RenderContext& context)
    {
        // フレームが変わった最初の View で溜め直す。単純に毎回クリアすると
        // 1 フレーム内で順に走る補助 View 分が消え、最後の View しか残らない。
        if (graphSnapshotFrameNumber_ != context.frameNumber) {
            graphSnapshots_.clear();
            graphSnapshotFrameNumber_ = context.frameNumber;
        }

        RenderGraphSnapshot snapshot;
        snapshot.viewType = context.viewSettings.viewType;
        snapshot.viewName = context.viewSettings.viewName;
        snapshot.displayName = snapshot.viewName.empty() ? std::string("GameView") : snapshot.viewName;
        snapshot.frameNumber = context.frameNumber;
        snapshot.executionOrder = renderGraph_.GetExecutionOrder();

        const std::vector<RenderGraphPass>& graphPasses = renderGraph_.GetPasses();
        snapshot.passes.reserve(graphPasses.size());

        for (const RenderGraphPass& graphPass : graphPasses) {
            RenderGraphSnapshotPass snapshotPass;
            static_cast<RenderGraphPass&>(snapshotPass) = graphPass;

            // PostEffect の分解ノードは BuildRenderGraph のたびに作り直されるため、
            // 次フレーム以降に触れると解放済みメモリになる。ポインタを持ち越さない。
            const bool isTransient = std::any_of(
                postEffectSubpasses_.begin(), postEffectSubpasses_.end(),
                [&graphPass](const std::unique_ptr<PostEffectPass>& subpass) {
                    return subpass.get() == graphPass.renderPass;
                });
            if (isTransient) {
                snapshotPass.transient = true;
                snapshotPass.renderPass = nullptr;
            }

            snapshot.passes.push_back(std::move(snapshotPass));
        }

        // 論理リソースは Graph 側の状態（版番号・解決可否）と Blackboard 側の SRV を突き合わせる。
        // SRV はここで拾っておかないと、ポーズ中にプレビューが引けなくなる。
        const std::unordered_map<std::string, RenderGraphResource>& graphResources = renderGraph_.GetResources();
        snapshot.resources.reserve(graphResources.size());

        for (const auto& [resourceName, graphResource] : graphResources) {
            RenderGraphSnapshotResource entry;
            entry.name = resourceName;
            entry.version = graphResource.version;
            entry.resolved = (graphResource.resource != nullptr && graphResource.currentState != nullptr);
            if (graphResource.currentState) {
                entry.stateAtCapture = *graphResource.currentState;
            }
            if (context.frameBlackboard) {
                context.frameBlackboard->TryGetSrvHandle(resourceName, entry.srvHandle);
            }

            for (const RenderGraphSnapshotPass& snapshotPass : snapshot.passes) {
                for (const RenderGraphResourceAccess& access : snapshotPass.reads) {
                    if (access.resourceName == resourceName) { ++entry.readerCount; break; }
                }
                for (const RenderGraphResourceAccess& access : snapshotPass.writes) {
                    if (access.resourceName == resourceName) { ++entry.writerCount; break; }
                }
            }

            snapshot.resources.push_back(std::move(entry));
        }

        std::sort(snapshot.resources.begin(), snapshot.resources.end(),
            [](const RenderGraphSnapshotResource& lhs, const RenderGraphSnapshotResource& rhs) {
                return lhs.name < rhs.name;
            });

        graphSnapshots_.push_back(std::move(snapshot));
    }
}
