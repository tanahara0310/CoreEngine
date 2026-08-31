#include "pch.h"
#include "GraphicsComponentFactory.h"
#include "../EngineSystem.h"
#include "../Startup/StartupSequence.h"
#include "WinApp/WinApp.h"
#include "Threading/ThreadPool.h"

#include "Utility/Logger/Logger.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/GraphicsCoreDesc.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/RenderTarget/SceneDepth.h"
#include "Graphics/Render/Culling/HiZOcclusionSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/ModelRenderer.h"
#include "Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Render/UI/TextRenderer.h"
#include "Text/FontManager.h"
#include "Graphics/Render/Particle/ParticleRenderer.h"
#include "Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Graphics/Render/Particle/GpuParticleRenderer.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelRenderContext.h"
#include "Graphics/IBL/IBLGenerator.h"
#include "Graphics/IBL/IBLSystem.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderProgram.h"

#include <memory>

namespace CoreEngine
{
    /// @brief ステップ間で受け渡す中間ポインタ
    /// @note 所有権は EngineSystem 側（RegisterComponent 済み）。ここが持つのは生ポインタだけ。
    struct GraphicsSetupState {
        GraphicsCore* dx = nullptr;
        ResourceFactory* resourceFactory = nullptr;
        Render* render = nullptr;
        RenderManager* renderManager = nullptr;
        LineRendererPipeline* lineRenderer = nullptr;
        IBLGenerator* iblGenerator = nullptr;
        ShaderCompiler* shaderCompiler = nullptr;
        /// @brief シェーダーのコンパイルとリフレクションを一元化するキャッシュ
        ShaderProgramCache* shaderProgramCache = nullptr;

        // フォワード受影用 RT シャドウマスクの初期値（white1x1 = 影なし）
        D3D12_GPU_DESCRIPTOR_HANDLE whiteFallback{};
    };

    std::shared_ptr<GraphicsSetupState> GraphicsComponentFactory::BuildFoundationTasks(
        StartupSequence& sequence,
        EngineSystem& engine,
        const EngineConfig& config)
    {
        auto state = std::make_shared<GraphicsSetupState>();
        EngineSystem* enginePtr = &engine;

        // ──────────────────────────────────────────────────────────
        // デバイスとフレーム基盤
        // ──────────────────────────────────────────────────────────
        sequence.Add("DirectX12 デバイス", [enginePtr, state, config] {
            WinApp* winApp = enginePtr->GetWinApp();

            // 基盤層は WinApp / EngineConfig を知らない。必要な値だけをここで詰めて渡す
            GraphicsCoreDesc desc{};
            desc.hwnd = winApp->GetHwnd();
            desc.clientWidth = winApp->GetClientWidth();
            desc.clientHeight = winApp->GetClientHeight();
            desc.enableDebugLayer = config.enableDebugLayer;
            desc.enableGPUBasedValidation = config.enableGPUBasedValidation;
            desc.enableDRED = config.enableDRED;
            desc.framesInFlight = config.frameCount;
            desc.maxSRVDescriptors = config.maxSRVDescriptors;
            desc.maxRTVDescriptors = config.maxRTVDescriptors;
            desc.maxDSVDescriptors = config.maxDSVDescriptors;

            auto graphicsCore = std::make_unique<GraphicsCore>();
            graphicsCore->Initialize(desc);
            state->dx = graphicsCore.get();

            // ウィンドウリサイズ → 基盤の再作成。配線は上位（ここ）の責務
            winApp->SetResizeCallback([dx = state->dx](int32_t width, int32_t height) {
                dx->OnWindowResize(width, height);
            });

            enginePtr->RegisterComponent(std::move(graphicsCore));
        });

        // ──────────────────────────────────────────────────────────
        // アセットロードの土台（デバイス直後・シェーダコンパイルより前）
        // ──────────────────────────────────────────────────────────
        // この 3 つが揃わないとアセット先読みを始められない。ここへ前倒しすることで、
        // シェーダコンパイル約 6 秒の裏にモデルロードを隠せる。
        sequence.Add("テクスチャ管理 / リソースファクトリ / モデル管理", [enginePtr, state] {
            // TextureManager の初期化（シングルトン）
            TextureManager::GetInstance().Initialize(state->dx);

            // ResourceFactory の作成（コンストラクタで初期化済み）
            auto resourceFactory = std::make_unique<ResourceFactory>();
            state->resourceFactory = resourceFactory.get();
            enginePtr->RegisterComponent(std::move(resourceFactory));

            // ModelManager の生成。描画依存コンテキスト（SetRenderContext）は
            // 全レンダラーの登録後でないと作れないので、後段の別ステップで行う。
            // リソースのロード自体はここまでで足りる
            auto modelManager = std::make_unique<ModelManager>();
            modelManager->Initialize(state->dx, state->resourceFactory);
            enginePtr->RegisterComponent(std::move(modelManager));

            // MSDF フォントの所有・共有・キャッシュはここが一元管理する。
            // シーンが MsdfFont を直接持つと、シーンをまたぐたびに焼き直しになる
            auto fontManager = std::make_unique<FontManager>();
            fontManager->Initialize(state->dx);
            enginePtr->RegisterComponent(std::move(fontManager));
        });

        return state;
    }

    void GraphicsComponentFactory::BuildRendererTasks(
        StartupSequence& sequence,
        EngineSystem& engine,
        std::shared_ptr<GraphicsSetupState> state)
    {
        EngineSystem* enginePtr = &engine;

        sequence.Add("シェーダープログラムキャッシュ", [enginePtr, state] {
            // DXC（IDxcUtils / IDxcCompiler3）はここで 1 回だけ作る
            auto cache = std::make_unique<ShaderProgramCache>();
            cache->Initialize();
            state->shaderProgramCache = cache.get();
            enginePtr->RegisterComponent(std::move(cache));
        });

        sequence.Add("レンダードメイン（GBuffer / シャドウ / RT）", [enginePtr, state] {
            enginePtr->renderDomainContext_ = std::make_unique<RenderDomainContext>();
            // RenderDomainContext は自分で RegisterResizable / UnregisterResizable する
            enginePtr->renderDomainContext_->Initialize(
                state->dx,
                enginePtr->GetWinApp()->GetClientWidth(),
                enginePtr->GetWinApp()->GetClientHeight(),
                state->shaderProgramCache);

            // Hi-Z オクルージョンカリングシステムの作成
            //（GPU リソースは初回 ExecuteCulling で遅延生成。
            //  解放タイミングは EngineSystem::Finalize 参照）
            enginePtr->hiZOcclusionSystem_ = std::make_unique<HiZOcclusionSystem>();
        });

        sequence.Add("Render（RTV / DSV）", [enginePtr, state] {
            // Render の作成と初期化（オフスクリーンターゲットが共有するシーン深度が必要）。
            // Render は自分で RegisterResizable / UnregisterResizable する
            auto render = std::make_unique<Render>();
            render->Initialize(state->dx, enginePtr->renderDomainContext_->GetSceneDepth());
            state->render = render.get();
            enginePtr->RegisterComponent(std::move(render));
        });

        // ──────────────────────────────────────────────────────────
        // 描画キューとレンダラー群
        // ──────────────────────────────────────────────────────────
        sequence.Add("RenderManager", [enginePtr, state] {
            auto renderManager = std::make_unique<RenderManager>();
            renderManager->Initialize(state->dx->GetDevice());
            state->renderManager = renderManager.get();
            // 元は全レンダラー登録後にまとめて登録していたが、ステップをまたいで
            // unique_ptr を持ち回すのを避けるためここで先に登録する。
            // 間で RenderManager を読む処理は無いので順序上の影響はない
            enginePtr->RegisterComponent(std::move(renderManager));

            // フォワード受影用 RT シャドウマスクの初期値: white1x1（= 影なし）。
            // 実マスクは毎フレーム DeferredLightingPass::Setup が供給する。
            // t6 が未バインドのままシェーダの GetDimensions が走るのを防ぐフォールバック
            state->whiteFallback = TextureManager::GetInstance().Load("white1x1.png").gpuHandle;
        });

        sequence.Add("レンダラー: モデル / スキンモデル", [state] {
            auto modelRenderer = std::make_unique<ModelRenderer>();
            modelRenderer->Initialize(state->dx->GetDevice());
            modelRenderer->SetRTShadowMask(state->whiteFallback);
            state->renderManager->RegisterRenderer(RenderPassType::Model, std::move(modelRenderer));

            auto skinnedRenderer = std::make_unique<SkinnedModelRenderer>();
            skinnedRenderer->Initialize(state->dx->GetDevice());
            skinnedRenderer->SetRTShadowMask(state->whiteFallback);
            state->renderManager->RegisterRenderer(RenderPassType::SkinnedModel, std::move(skinnedRenderer));
        });

        sequence.Add("レンダラー: スカイボックス / スプライト / UI", [state] {
            auto skyBoxRenderer = std::make_unique<SkyBoxRenderer>();
            skyBoxRenderer->Initialize(state->dx->GetDevice());
            state->renderManager->RegisterRenderer(RenderPassType::SkyBox, std::move(skyBoxRenderer));

            auto spriteRenderer = std::make_unique<SpriteRenderer>();
            spriteRenderer->Initialize(state->dx, state->resourceFactory);
            state->renderManager->RegisterRenderer(RenderPassType::Sprite, std::move(spriteRenderer));

            // UI パスは最前面・スクリーン固定座標
            auto uiRenderer = std::make_unique<UIRenderer>();
            uiRenderer->Initialize(state->dx, state->resourceFactory);
            state->renderManager->RegisterRenderer(RenderPassType::UI, std::move(uiRenderer));

            // MSDF テキストは UI と同じ座標系だが PSO が別なので独立パスにする
            auto textRenderer = std::make_unique<TextRenderer>();
            textRenderer->Initialize(state->dx, state->resourceFactory);
            state->renderManager->RegisterRenderer(RenderPassType::UIText, std::move(textRenderer));
        });

        sequence.Add("レンダラー: パーティクル", [state] {
            auto particleRenderer = std::make_unique<ParticleRenderer>();
            particleRenderer->SetResourceFactory(state->resourceFactory);
            particleRenderer->Initialize(state->dx->GetDevice());
            state->renderManager->RegisterRenderer(RenderPassType::Particle, std::move(particleRenderer));

            auto modelParticleRenderer = std::make_unique<ModelParticleRenderer>();
            modelParticleRenderer->SetResourceFactory(state->resourceFactory);
            modelParticleRenderer->Initialize(state->dx->GetDevice());
            state->renderManager->RegisterRenderer(RenderPassType::ModelParticle, std::move(modelParticleRenderer));

            auto gpuParticleRenderer = std::make_unique<GpuParticleRenderer>();
            gpuParticleRenderer->SetResourceFactory(state->resourceFactory);
            gpuParticleRenderer->Initialize(state->dx->GetDevice());
            state->renderManager->RegisterRenderer(RenderPassType::GpuParticle, std::move(gpuParticleRenderer));
        });

        sequence.Add("レンダラー: ライン", [state] {
            auto lineRendererPipeline = std::make_unique<LineRendererPipeline>();
            lineRendererPipeline->Initialize(state->dx, state->resourceFactory);
            state->lineRenderer = lineRendererPipeline.get();
            state->renderManager->RegisterRenderer(RenderPassType::Line, std::move(lineRendererPipeline));

            // LineManager の初期化（シングルトン、RenderManager 登録後に実行）
            LineManager::GetInstance().Initialize(state->lineRenderer);
        });

        // ──────────────────────────────────────────────────────────
        // ポストエフェクト・レンダリング技術・モデル・IBL
        // ──────────────────────────────────────────────────────────
        sequence.Add("ポストエフェクト", [enginePtr, state] {
            auto postEffectManager = std::make_unique<PostEffectManager>();
            postEffectManager->Initialize(state->dx, state->render, state->shaderProgramCache);
            enginePtr->RegisterComponent(std::move(postEffectManager));
        });

        sequence.Add("レンダリング技術", [enginePtr, state] {
            auto renderingTechniqueManager = std::make_unique<RenderingTechniqueManager>();
            renderingTechniqueManager->Initialize(state->dx, state->shaderProgramCache);
            enginePtr->RegisterComponent(std::move(renderingTechniqueManager));

            // ポストエフェクトとレンダリング技術を作り終えた時点での効き具合を残す
            state->shaderProgramCache->LogSummary();
        });

        sequence.Add("モデル描画コンテキスト", [enginePtr, state] {
            // ModelManager の生成自体はデバイス直後に済ませてある（先読みのため）。
            // ここでは全レンダラー登録完了後にしか作れない描画依存コンテキストを設定する
            //（Model インスタンス生成時に各 Model へ注入される）
            ModelRenderContext modelCtx;
            modelCtx.dxCommon = state->dx;
            modelCtx.modelRenderer =
                dynamic_cast<BaseModelRenderer*>(state->renderManager->GetRenderer(RenderPassType::Model));
            modelCtx.skinnedRenderer =
                dynamic_cast<BaseModelRenderer*>(state->renderManager->GetRenderer(RenderPassType::SkinnedModel));
            modelCtx.hiZOcclusion = enginePtr->hiZOcclusionSystem_.get();
            enginePtr->GetService<ModelManager>()->SetRenderContext(modelCtx);
        });

        sequence.Add("IBL（環境ライティング）", [enginePtr, state] {
            auto iblGenerator = std::make_unique<IBLGenerator>();
            state->iblGenerator = iblGenerator.get();

            auto shaderCompiler = std::make_unique<ShaderCompiler>();
            shaderCompiler->Initialize();
            state->shaderCompiler = shaderCompiler.get();

            iblGenerator->Initialize(state->dx, state->shaderCompiler);
            enginePtr->RegisterComponent(std::move(iblGenerator));
            enginePtr->RegisterComponent(std::move(shaderCompiler));

            auto iblSystem = std::make_unique<IBLSystem>();
            if (!iblSystem->Initialize(state->dx, state->iblGenerator, state->renderManager)) {
                Logger::GetInstance().Logf(
                    LogLevel::Error, LogCategory::Graphics, "{}", "Failed to initialize IBLSystem");
            }
            enginePtr->RegisterComponent(std::move(iblSystem));
        });
    }
}
