#include "pch.h"
#include "GraphicsComponentFactory.h"
#include "../EngineSystem.h"
#include "WinApp/WinApp.h"
#include "Threading/ThreadPool.h"

#include "Utility/Logger/Logger.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Texture/TextureManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/Render.h"
#include "Graphics/Render/RenderManager.h"
#include "Graphics/Render/Model/ModelRenderer.h"
#include "Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Shadow/ShadowMapRenderer.h"
#include "Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Graphics/Render/Sprite/SpriteRenderer.h"
#include "Graphics/Render/UI/UIRenderer.h"
#include "Graphics/Render/Particle/ParticleRenderer.h"
#include "Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelRenderContext.h"
#include "Graphics/IBL/IBLGenerator.h"
#include "Graphics/IBL/IBLSystem.h"
#include "Graphics/Shader/ShaderCompiler.h"

namespace CoreEngine
{
    void GraphicsComponentFactory::Setup(EngineSystem& engine, const EngineConfig& config)
    {
        // DirectXCommonの作成と初期化
        auto directXCommon = std::make_unique<DirectXCommon>();
        directXCommon->Initialize(engine.GetWinApp(), config);
        DirectXCommon* dxPtr = directXCommon.get();
        engine.RegisterComponent(std::move(directXCommon));

        // TextureManagerの初期化（シングルトン）
        TextureManager::GetInstance().Initialize(dxPtr);

        // ResourceFactoryの作成（コンストラクタで初期化済み）
        auto resourceFactory = std::make_unique<ResourceFactory>();
        ResourceFactory* resourcePtr = resourceFactory.get();
        engine.RegisterComponent(std::move(resourceFactory));

        // Renderの作成と初期化（DSVヒープが必要）
        auto render = std::make_unique<Render>();
        render->Initialize(dxPtr, dxPtr->GetDSVHeap());
        Render* renderPtr = render.get();
        engine.RegisterComponent(std::move(render));

        // RenderManagerの作成と初期化
        auto renderManager = std::make_unique<RenderManager>();
        renderManager->Initialize(dxPtr->GetDevice());
        RenderManager* renderManagerPtr = renderManager.get();

        // ShadowMapManagerを設定
        renderManager->SetShadowMapManager(dxPtr->GetShadowMapManager());

        // ShadowMapRendererの作成と登録（最優先）
        auto shadowMapRenderer = std::make_unique<ShadowMapRenderer>();
        shadowMapRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::ShadowMap, std::move(shadowMapRenderer));

        // ModelRendererの作成と登録
        auto modelRenderer = std::make_unique<ModelRenderer>();
        modelRenderer->Initialize(dxPtr->GetDevice());
        modelRenderer->SetShadowMap(dxPtr->GetShadowMapSRVHandle());
        renderManager->RegisterRenderer(RenderPassType::Model, std::move(modelRenderer));

        // SkinnedModelRendererの作成と登録
        auto skinnedRenderer = std::make_unique<SkinnedModelRenderer>();
        skinnedRenderer->Initialize(dxPtr->GetDevice());
        skinnedRenderer->SetShadowMap(dxPtr->GetShadowMapSRVHandle());
        renderManager->RegisterRenderer(RenderPassType::SkinnedModel, std::move(skinnedRenderer));

        // SkyBoxRendererの作成と登録
        auto skyBoxRenderer = std::make_unique<SkyBoxRenderer>();
        skyBoxRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::SkyBox, std::move(skyBoxRenderer));

        // SpriteRendererの作成と登録
        auto spriteRenderer = std::make_unique<SpriteRenderer>();
        spriteRenderer->Initialize(dxPtr, resourcePtr);
        renderManager->RegisterRenderer(RenderPassType::Sprite, std::move(spriteRenderer));

        // UIRendererの作成と登録（UIパスは最前面・スクリーン固定座標）
        auto uiRenderer = std::make_unique<UIRenderer>();
        uiRenderer->Initialize(dxPtr, resourcePtr);
        renderManager->RegisterRenderer(RenderPassType::UI, std::move(uiRenderer));

        // ParticleRendererの作成と登録
        auto particleRenderer = std::make_unique<ParticleRenderer>();
        particleRenderer->SetResourceFactory(resourcePtr);
        particleRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::Particle, std::move(particleRenderer));

        // ModelParticleRendererの作成と登録
        auto modelParticleRenderer = std::make_unique<ModelParticleRenderer>();
        modelParticleRenderer->SetResourceFactory(resourcePtr);
        modelParticleRenderer->Initialize(dxPtr->GetDevice());
        renderManager->RegisterRenderer(RenderPassType::ModelParticle, std::move(modelParticleRenderer));

        // LineRendererPipelineの作成と登録
        auto lineRendererPipeline = std::make_unique<LineRendererPipeline>();
        lineRendererPipeline->Initialize(dxPtr, resourcePtr);
        LineRendererPipeline* lineRendererPtr = lineRendererPipeline.get();
        renderManager->RegisterRenderer(RenderPassType::Line, std::move(lineRendererPipeline));

        // RenderManagerを登録
        engine.RegisterComponent(std::move(renderManager));

        // LineManagerの初期化（シングルトン、RenderManager登録後に実行）
        LineManager::GetInstance().Initialize(lineRendererPtr);

        // PostEffectManagerの作成と初期化
        auto postEffectManager = std::make_unique<PostEffectManager>();
        postEffectManager->Initialize(dxPtr, renderPtr);
        engine.RegisterComponent(std::move(postEffectManager));

        // RenderingTechniqueManagerの作成と初期化
        auto renderingTechniqueManager = std::make_unique<RenderingTechniqueManager>();
        renderingTechniqueManager->Initialize(dxPtr);
        engine.RegisterComponent(std::move(renderingTechniqueManager));

        // ModelManagerの作成と初期化
        auto modelManager = std::make_unique<ModelManager>();
        modelManager->Initialize(dxPtr, resourcePtr);
        engine.RegisterComponent(std::move(modelManager));

        // 全レンダラー登録完了後、ModelManager に描画依存コンテキストを設定
        // （Model インスタンス生成時に各 Model へ注入される）
        ModelRenderContext modelCtx;
        modelCtx.dxCommon = dxPtr;
        modelCtx.shadowMapManager = dxPtr->GetShadowMapManager();
        modelCtx.modelRenderer = dynamic_cast<BaseModelRenderer*>(renderManagerPtr->GetRenderer(RenderPassType::Model));
        modelCtx.skinnedRenderer = dynamic_cast<BaseModelRenderer*>(renderManagerPtr->GetRenderer(RenderPassType::SkinnedModel));
        modelCtx.shadowRenderer = static_cast<ShadowMapRenderer*>(renderManagerPtr->GetRenderer(RenderPassType::ShadowMap));
        engine.GetComponent<ModelManager>()->SetRenderContext(modelCtx);

        // IBLGeneratorの作成と初期化
        auto iblGenerator = std::make_unique<IBLGenerator>();
        IBLGenerator* iblGeneratorPtr = iblGenerator.get();
        auto shaderCompiler = std::make_unique<ShaderCompiler>();
        shaderCompiler->Initialize();
        iblGenerator->Initialize(dxPtr, shaderCompiler.get());
        engine.RegisterComponent(std::move(iblGenerator));
        engine.RegisterComponent(std::move(shaderCompiler));

        auto iblSystem = std::make_unique<IBLSystem>();
        if (!iblSystem->Initialize(dxPtr, iblGeneratorPtr, renderManagerPtr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to initialize IBLSystem");
        }
        engine.RegisterComponent(std::move(iblSystem));
    }
}
