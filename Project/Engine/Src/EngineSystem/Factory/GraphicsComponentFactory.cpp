#include "pch.h"
#include "GraphicsComponentFactory.h"
#include "../EngineSystem.h"
#include "../Startup/StartupSequence.h"
#include "WinApp/WinApp.h"
#include "Threading/ThreadPool.h"

#include "Utility/Logger/Logger.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Graphics/Render/Culling/HiZOcclusionSystem.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/ModelRenderer.h"
#include "Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/Render/UI/UIRenderer.h"
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

#include <memory>

namespace CoreEngine
{
    /// @brief ステップ間で受け渡す中間ポインタ
    /// @details 元は 1 関数のローカル変数だったもの。ステップに切り分けると
    ///          関数をまたぐので、shared_ptr で全ステップに持たせる。
    ///          所有権はあくまで EngineSystem 側（RegisterComponent 済み）にあり、
    ///          ここが持つのは生ポインタだけ。
    ///          ヘッダでは前方宣言のみ（呼び出し側は Foundation の戻り値を
    ///          Renderer へ渡すだけで、中身に触らない）。
    struct GraphicsSetupState {
        DirectXCommon* dx = nullptr;
        ResourceFactory* resourceFactory = nullptr;
        Render* render = nullptr;
        RenderManager* renderManager = nullptr;
        LineRendererPipeline* lineRenderer = nullptr;
        IBLGenerator* iblGenerator = nullptr;
        ShaderCompiler* shaderCompiler = nullptr;

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
            auto directXCommon = std::make_unique<DirectXCommon>();
            directXCommon->Initialize(enginePtr->GetWinApp(), config);
            state->dx = directXCommon.get();
            enginePtr->RegisterComponent(std::move(directXCommon));
        });

        // ──────────────────────────────────────────────────────────
        // アセットロードの土台（デバイス直後・シェーダコンパイルより前）
        // ──────────────────────────────────────────────────────────
        // TextureManager / ResourceFactory / ModelManager はどれも中身がほぼ空の
        // 初期化しかしないが、**アセット先読みを始めるにはこの 3 つが揃っている必要がある**。
        // 以前はレンダラー群の後（＝シェーダコンパイルを全部終えた後）に置いていたため、
        // 先読みを仕掛けても裏に隠せる時間が 1 秒しか残らなかった。
        // ここへ前倒しすることで、シェーダコンパイル約 6 秒の裏にモデルロードを隠せる。
        // RenderDomainContext はこの 3 つに一切触らないので、順序を入れ替えても安全。
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
        });

        return state;
    }

    void GraphicsComponentFactory::BuildRendererTasks(
        StartupSequence& sequence,
        EngineSystem& engine,
        std::shared_ptr<GraphicsSetupState> state)
    {
        EngineSystem* enginePtr = &engine;

        sequence.Add("レンダードメイン（GBuffer / シャドウ / RT）", [enginePtr, state] {
            enginePtr->renderDomainContext_ = std::make_unique<RenderDomainContext>();
            enginePtr->renderDomainContext_->Initialize(
                state->dx,
                enginePtr->GetWinApp()->GetClientWidth(),
                enginePtr->GetWinApp()->GetClientHeight());

            state->dx->RegisterResizable(enginePtr->renderDomainContext_.get());

            // Hi-Z オクルージョンカリングシステムの作成
            //（GPU リソースは初回 ExecuteCulling で遅延生成。
            //  解放タイミングは EngineSystem::Finalize 参照）
            enginePtr->hiZOcclusionSystem_ = std::make_unique<HiZOcclusionSystem>();
        });

        sequence.Add("Render（RTV / DSV）", [enginePtr, state] {
            // Render の作成と初期化（DSV ヒープが必要）
            auto render = std::make_unique<Render>();
            render->Initialize(state->dx, state->dx->GetDSVHeap());
            state->render = render.get();
            enginePtr->RegisterComponent(std::move(render));

            state->dx->RegisterResizable(state->render);
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
            postEffectManager->Initialize(state->dx, state->render);
            enginePtr->RegisterComponent(std::move(postEffectManager));
        });

        sequence.Add("レンダリング技術", [enginePtr, state] {
            auto renderingTechniqueManager = std::make_unique<RenderingTechniqueManager>();
            renderingTechniqueManager->Initialize(state->dx);
            enginePtr->RegisterComponent(std::move(renderingTechniqueManager));
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
