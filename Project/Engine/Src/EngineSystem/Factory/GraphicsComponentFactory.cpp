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
#include "Graphics/Render/Text3D/Text3DRenderer.h"
#include "Text/FontManager.h"
#include "Graphics/Render/Particle/ParticleRenderer.h"
#include "Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Graphics/Render/Particle/GpuParticleRenderer.h"
#include "Graphics/Render/Line/LineRendererPipeline.h"
#include "Graphics/Render/Line/GridRenderer.h"
#include "Graphics/Line/LineManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/Render/RenderingTechnique/RenderingTechniqueManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelRenderContext.h"
#include "Graphics/Shader/ShaderProgram.h"

#include <memory>
#include <utility>

namespace CoreEngine
{
    namespace
    {
        /// @brief レンダラーを作って Initialize を呼び、描画パスへ登録する
        /// @param initArgs Initialize へ渡す引数
        /// @return 登録したレンダラー（所有は RenderManager）
        template <typename T, typename... Args>
        T* CreateAndRegisterRenderer(RenderManager& renderManager, RenderPassType type, Args&&... initArgs)
        {
            auto renderer = std::make_unique<T>();
            renderer->Initialize(std::forward<Args>(initArgs)...);
            T* const ptr = renderer.get();
            renderManager.RegisterRenderer(type, std::move(renderer));
            return ptr;
        }
    }

    /// @brief ステップ間で受け渡す中間ポインタ
    /// @note 所有権は EngineSystem 側（RegisterComponent 済み）。ここが持つのは生ポインタだけ。
    struct GraphicsSetupState {
        GraphicsCore* dx = nullptr;
        Render* render = nullptr;
        RenderManager* renderManager = nullptr;
        LineRendererPipeline* lineRenderer = nullptr;
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

            state->dx = enginePtr->CreateAndRegisterComponent<GraphicsCore>(EngineSystem::FactoryKey{}, desc);

            // ウィンドウリサイズ → 基盤の再作成。配線は上位（ここ）の責務
            winApp->SetResizeCallback([dx = state->dx](int32_t width, int32_t height) {
                dx->OnWindowResize(width, height);
            });
        });

        // ──────────────────────────────────────────────────────────
        // アセットロードの土台（デバイス直後・シェーダコンパイルより前）
        // ──────────────────────────────────────────────────────────
        // この 3 つが揃わないとアセット先読みを始められない。ここへ前倒しすることで、
        // シェーダコンパイル約 6 秒の裏にモデルロードを隠せる。
        sequence.Add("テクスチャ管理 / モデル管理", [enginePtr, state] {
            // TextureManager の初期化（シングルトン）
            TextureManager::GetInstance().Initialize(state->dx);

            // ModelManager の生成。描画依存コンテキスト（SetRenderContext）は
            // 全レンダラーの登録後でないと作れないので、後段の別ステップで行う。
            // リソースのロード自体はここまでで足りる
            enginePtr->CreateAndRegisterComponent<ModelManager>(EngineSystem::FactoryKey{}, state->dx);

            // MSDF フォントの所有・共有・キャッシュはここが一元管理する。
            // シーンが MsdfFont を直接持つと、シーンをまたぐたびに焼き直しになる
            enginePtr->CreateAndRegisterComponent<FontManager>(EngineSystem::FactoryKey{}, state->dx);
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
            state->shaderProgramCache =
                enginePtr->CreateAndRegisterComponent<ShaderProgramCache>(EngineSystem::FactoryKey{});
        });

        sequence.Add("レンダードメイン（GBuffer / シャドウ / RT）", [enginePtr, state] {
            auto domain = std::make_unique<RenderDomainContext>();
            // RenderDomainContext は自分で RegisterResizable / UnregisterResizable する
            domain->Initialize(
                state->dx,
                enginePtr->GetWinApp()->GetClientWidth(),
                enginePtr->GetWinApp()->GetClientHeight(),
                state->shaderProgramCache);

            // Hi-Z オクルージョンカリングシステムの作成
            //（GPU リソースは初回 ExecuteCulling で遅延生成。
            //  解放タイミングは EngineSystem::Finalize 参照）
            enginePtr->AdoptRenderDomain(EngineSystem::FactoryKey{}, std::move(domain),
                std::make_unique<HiZOcclusionSystem>());
        });

        sequence.Add("Render（RTV / DSV）", [enginePtr, state] {
            // Render の作成と初期化（オフスクリーンターゲットが共有するシーン深度が必要）。
            // Render は自分で RegisterResizable / UnregisterResizable する
            state->render = enginePtr->CreateAndRegisterComponent<Render>(EngineSystem::FactoryKey{},
                state->dx, enginePtr->GetRenderDomainContext()->GetSceneDepth());
        });

        // ──────────────────────────────────────────────────────────
        // 描画キューとレンダラー群
        // ──────────────────────────────────────────────────────────
        sequence.Add("RenderManager", [enginePtr, state] {
            // 元は全レンダラー登録後にまとめて登録していたが、ステップをまたいで
            // unique_ptr を持ち回すのを避けるためここで先に登録する。
            // 間で RenderManager を読む処理は無いので順序上の影響はない
            state->renderManager = enginePtr->CreateAndRegisterComponent<RenderManager>(
                EngineSystem::FactoryKey{}, state->dx->GetDevice());

            // フォワード受影用 RT シャドウマスクの初期値: white1x1（= 影なし）。
            // 実マスクは毎フレーム DeferredLightingPass::Setup が供給する。
            // t6 が未バインドのままシェーダの GetDimensions が走るのを防ぐフォールバック
            state->whiteFallback = TextureManager::GetInstance().Load("white1x1.png").gpuHandle;
        });

        sequence.Add("レンダラー: モデル / スキンモデル", [enginePtr, state] {
            // 画面の深度を読むマテリアルが、写しの無いフレームに読む「いちばん遠い」（白 = 1）
            SceneDepth* const sceneDepth = enginePtr->GetRenderDomainContext()->GetSceneDepth();
            if (sceneDepth) {
                sceneDepth->SetFarTexture(state->whiteFallback);
            }

            auto* const modelRenderer = CreateAndRegisterRenderer<ModelRenderer>(
                *state->renderManager, RenderPassType::Model, state->dx->GetDevice());
            modelRenderer->SetRTShadowMask(state->whiteFallback);
            modelRenderer->SetSceneDepth(sceneDepth);

            auto* const skinnedRenderer = CreateAndRegisterRenderer<SkinnedModelRenderer>(
                *state->renderManager, RenderPassType::SkinnedModel, state->dx->GetDevice());
            skinnedRenderer->SetRTShadowMask(state->whiteFallback);
            skinnedRenderer->SetSceneDepth(sceneDepth);
        });

        sequence.Add("レンダラー: スカイボックス / スプライト / UI", [enginePtr, state] {
            SceneDepth* const sceneDepth = enginePtr->GetRenderDomainContext()->GetSceneDepth();

            CreateAndRegisterRenderer<SkyBoxRenderer>(
                *state->renderManager, RenderPassType::SkyBox, state->dx->GetDevice());

            auto* const spriteRenderer = CreateAndRegisterRenderer<SpriteRenderer>(
                *state->renderManager, RenderPassType::Sprite, state->dx);
            spriteRenderer->SetSceneDepth(sceneDepth);

            // UI パスは最前面・スクリーン固定座標
            auto* const uiRenderer = CreateAndRegisterRenderer<UIRenderer>(
                *state->renderManager, RenderPassType::UI, state->dx);
            uiRenderer->SetSceneDepth(sceneDepth);

            // MSDF テキストは UI と同じ座標系だが PSO が別なので独立パスにする
            CreateAndRegisterRenderer<TextRenderer>(*state->renderManager, RenderPassType::UIText, state->dx);

            // 同じ MSDF フォントをワールド空間へ描くパス。
            // アトラスと距離場は UI 版と共有し、変換と深度の扱いだけが違う
            CreateAndRegisterRenderer<Text3DRenderer>(*state->renderManager, RenderPassType::Text3D, state->dx);
        });

        sequence.Add("レンダラー: パーティクル", [state] {
            CreateAndRegisterRenderer<ParticleRenderer>(
                *state->renderManager, RenderPassType::Particle, state->dx->GetDevice());
            CreateAndRegisterRenderer<ModelParticleRenderer>(
                *state->renderManager, RenderPassType::ModelParticle, state->dx->GetDevice());
            CreateAndRegisterRenderer<GpuParticleRenderer>(
                *state->renderManager, RenderPassType::GpuParticle, state->dx->GetDevice());
        });

        sequence.Add("レンダラー: ライン", [state] {
            state->lineRenderer = CreateAndRegisterRenderer<LineRendererPipeline>(
                *state->renderManager, RenderPassType::Line, state->dx);

            // LineManager の初期化（シングルトン、RenderManager 登録後に実行）
            LineManager::GetInstance().Initialize(state->lineRenderer);
        });

        sequence.Add("レンダラー: グリッド", [state] {
            // 解析グリッド（フルスクリーン三角形 1 枚をピクセルシェーダーで評価する床グリッド）。
            // 既定は非表示で、エディタの GridFeature が表示を切り替える。
            CreateAndRegisterRenderer<GridRenderer>(*state->renderManager, RenderPassType::Grid, state->dx);
        });

        // ──────────────────────────────────────────────────────────
        // ポストエフェクト・レンダリング技術・モデル
        // ──────────────────────────────────────────────────────────
        sequence.Add("ポストエフェクト", [enginePtr, state] {
            enginePtr->CreateAndRegisterComponent<PostEffectManager>(EngineSystem::FactoryKey{},
                state->dx, state->render, state->shaderProgramCache);
        });

        sequence.Add("レンダリング技術", [enginePtr, state] {
            enginePtr->CreateAndRegisterComponent<RenderingTechniqueManager>(EngineSystem::FactoryKey{},
                state->dx, state->shaderProgramCache);

            // ポストエフェクトとレンダリング技術を作り終えた時点での効き具合を残す
            state->shaderProgramCache->LogSummary();
        });

        sequence.Add("モデル描画コンテキスト", [enginePtr, state] {
            // ModelManager の生成自体はデバイス直後に済ませてある（先読みのため）。
            // ここでは全レンダラー登録完了後にしか作れない描画依存コンテキストを設定する
            //（Model インスタンス生成時に各 Model へ注入される）
            ModelRenderContext modelCtx;
            modelCtx.dxCommon = state->dx;
            modelCtx.modelRenderer = state->renderManager->GetRendererAs<BaseModelRenderer>(RenderPassType::Model);
            modelCtx.skinnedRenderer =
                state->renderManager->GetRendererAs<BaseModelRenderer>(RenderPassType::SkinnedModel);
            modelCtx.hiZOcclusion = enginePtr->GetHiZOcclusionSystem();
            enginePtr->GetService<ModelManager>()->SetRenderContext(modelCtx);
        });
    }
}
