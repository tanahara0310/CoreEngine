#include "EngineSystem.h"


// ユーティリティ
#include "Engine/Utility/Random/RandomGenerator.h"
#include "Engine/Utility/Logger/Logger.h"
#include "Engine/Graphics/TextureManager.h"
#include "Engine/Graphics/Font/FontManager.h"

// レンダリング関連
#include "Engine/Graphics/Render/Render.h"
#include "Engine/Graphics/Resource/ResourceFactory.h"
#include "Engine/Graphics/PostEffect/PostEffectManager.h"
#include "Engine/Graphics/Render/Line/LineRendererPipeline.h"
#include "Engine/Graphics/Line/LineManager.h"
#include "Engine/Graphics/Render/Model/ModelRenderer.h"
#include "Engine/Graphics/Render/Model/SkinnedModelRenderer.h"
#include "Engine/Graphics/Render/SkyBox/SkyBoxRenderer.h"
#include "Engine/Graphics/Render/Sprite/SpriteRenderer.h"
#include "Engine/Graphics/Render/Particle/ParticleRenderer.h"
#include "Engine/Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Engine/Graphics/Font/TextRenderer.h"

// 入力管理
#include "Engine/Input/InputManager.h"

// フレームレート制御
#include "Engine/Utility/FrameRate/FrameRateController.h"

#include "ObjectCommon/GameObject.h"


namespace CoreEngine
{
void EngineSystem::Initialize(WinApp* winApp)
{

	// COMの初期化
	CoInitializeEx(0, COINIT_MULTITHREADED);

	// ログシステムの初期化（最初に実行）
	Logger::GetInstance().Initialize();

	// WinAppのインスタンスを保持
	winApp_ = winApp;

	// ===== コンポーネントの作成と初期化 =====

	// フレームレート制御（最初に初期化）
	CreateFrameRateController();

	// グラフィックス関連
	CreateGraphicsComponents();

	// 入力関連
	CreateInputComponents();

	// オーディオ関連
	CreateAudioComponents();

	// ライト関連（GraphicsComponents 後に初期化）
	CreateLightComponents();

	// 統一乱数生成器の初期化
	RandomGenerator::GetInstance().Initialize();

#ifdef _DEBUG
	// ImGuiマネージャークラスの初期化
	imGui_->Initialize(winApp_->GetHwnd(), GetComponent<DirectXCommon>());

	// ゲームデバッグUIの初期化（DockingUIを渡す）
	gameDebugUI_->Initialize(this, imGui_->GetDockingUI());

	// その他の固定ウィンドウをドッキングシステムに登録
	DockingUI* dockingUI = imGui_->GetDockingUI();
	if (dockingUI) {
		// SceneViewportが作成するウィンドウを中央に配置
		dockingUI->RegisterWindow("Scene", DockArea::Center);

		// オブジェクト制御ウィンドウを右側に配置（インスペクター的な役割）
		dockingUI->RegisterWindow("オブジェクト制御", DockArea::Right);

		// ポストエフェクトウィンドウを右側に配置
		dockingUI->RegisterWindow("Post Effects", DockArea::Right);

		// パーティクルシステムデバッグを下部に配置
		dockingUI->RegisterWindow("Particle System Debug", DockArea::Right);

		// ラインデバッグウィンドウを右側に配置
		dockingUI->RegisterWindow("ラインデバッグ", DockArea::Right);
	}
#endif // _DEBUG

	GameObject::Initialize(this);
}

void EngineSystem::Finalize()
{
#ifdef _DEBUG
	// ImGuiの終了処理
	imGui_->Finalize();
#endif // _DEBUG

	// TextureManagerのキャッシュをクリア
	TextureManager::GetInstance().Clear();

	// FontManagerの終了処理
	FontManager::GetInstance().Finalize();

	componentOwners_.clear();

	// COMの解放
	CoUninitialize();
}

void EngineSystem::BeginFrame()
{
	// フレームレート制御の開始
	if (auto* frameRate = GetComponent<FrameRateController>()) {
		frameRate->BeginFrame();
	}

	// RenderManagerの描画キューをクリア（前フレームのコマンドを削除）
	if (auto* renderManager = GetComponent<RenderManager>()) {
		renderManager->ClearQueue();
	}

	// 入力の更新
	if (auto* inputManager = GetComponent<InputManager>()) {
		inputManager->Update();
	}
	if (auto* keyboard = GetComponent<KeyboardInput>()) {
		keyboard->Update();
	}
	if (auto* mouse = GetComponent<MouseInput>()) {
		mouse->Update();
	}
	if (auto* gamepad = GetComponent<GamepadInput>()) {
		gamepad->Update();
	}

	// ポストエフェクトの更新（フレームレートコントローラーからデルタタイムを取得）
	if (auto* postEffect = GetComponent<PostEffectManager>()) {
		if (auto* frameRate = GetComponent<FrameRateController>()) {
			postEffect->Update(frameRate->GetDeltaTime());
		}
	}

#ifdef _DEBUG
	// ImGuiの開始（PostEffectManagerとGameDebugUIを渡す）
	if (auto* postEffect = GetComponent<PostEffectManager>()) {
		imGui_->Begin(postEffect, gameDebugUI_.get());
	}

	//メニューバーを最初に描画（ドッキングスペースより前）
	gameDebugUI_->ShowMainMenuBar();

	// その他のデバッグUIの更新（メニューバー以外）
	gameDebugUI_->UpdateDebugPanels();

	// ポストエフェクトのImGui描画
	if (auto* postEffect = GetComponent<PostEffectManager>()) {
		postEffect->DrawImGui();
	}
#endif // _DEBUG
}

void EngineSystem::EndFrame()
{
#ifdef _DEBUG
	imGui_->End();
#endif // _DEBUG

	// VSync有効時はフレームレート制御の終了処理は不要
	// Present(1, 0)が自動的に60Hzに同期してくれる
}

void EngineSystem::ExecuteRenderPipeline(std::function<void()> renderCallback)
{
	auto* render = GetComponent<Render>();
	auto* postEffect = GetComponent<PostEffectManager>();
	auto* dx = GetComponent<DirectXCommon>();

	if (!render || !postEffect || !dx) {
		return;
	}

	// レンダリングの開始（1枚目のオフスクリーン）
	render->OffscreenPreDraw(0);

	// シーン固有の描画処理を実行
	if (renderCallback) {
		renderCallback();
	}

	// 1枚目のオフスクリーン描画の終了
	render->OffscreenPostDraw(0);

	// ポストエフェクトチェーンの適用
	D3D12_GPU_DESCRIPTOR_HANDLE outputHandle = postEffect->ExecuteEffectChain(
		dx->GetOffScreenSrvHandle());

	render->BackBufferPreDraw();

#ifdef _DEBUG
	// 最終結果をバックバッファに描画
	postEffect->ExecuteEffect("FullScreen", outputHandle);

	// ImGuiの描画コマンドを積む
	imGui_->Draw();
#else
	// 最終結果をバックバッファに描画
	postEffect->ExecuteEffect("FullScreen", outputHandle);
#endif // _DEBUG

	// バックバッファの描画終了
	render->BackBufferPostDraw();
}

// ──────────────────────────────────────────────────────────
// コンポーネント作成ヘルパーメソッド
// ──────────────────────────────────────────────────────────

#pragma region コンポーネントヘルパーメソッド

void EngineSystem::CreateFrameRateController()
{
	// フレームレート制御を作成・初期化
	auto frameRate = std::make_unique<FrameRateController>();
	frameRate->Initialize(); // 60FPS固定

	// ComponentManagerに登録（所有権を移譲）
	RegisterComponent(std::move(frameRate));
}

void EngineSystem::CreateGraphicsComponents()
{
	// DirectXCommonの作成と初期化
	auto directXCommon = std::make_unique<DirectXCommon>();
	directXCommon->Initialize(winApp_);
	DirectXCommon* dxPtr = directXCommon.get();
 RegisterComponent(std::move(directXCommon));

	// TextureManagerの初期化（シングルトン）
	TextureManager::GetInstance().Initialize(dxPtr);

	// FontManagerの初期化（シングルトン）
	FontManager::GetInstance().Initialize(dxPtr);

	// ResourceFactoryの作成（コンストラクタで初期化済み）
	auto resourceFactory = std::make_unique<ResourceFactory>();
	ResourceFactory* resourcePtr = resourceFactory.get();
	RegisterComponent(std::move(resourceFactory));

	// Renderの作成と初期化（DSVヒープが必要）
	auto render = std::make_unique<Render>();
	render->Initialize(dxPtr, dxPtr->GetDSVHeap());
	Render* renderPtr = render.get();
	RegisterComponent(std::move(render));

	// RenderManagerの作成と初期化
	auto renderManager = std::make_unique<RenderManager>();
	renderManager->Initialize(dxPtr->GetDevice());
	
	// ModelRendererの作成と登録
	auto modelRenderer = std::make_unique<ModelRenderer>();
	modelRenderer->Initialize(dxPtr->GetDevice());
	renderManager->RegisterRenderer(RenderPassType::Model, std::move(modelRenderer));
	
	// SkinnedModelRendererの作成と登録
	auto skinnedRenderer = std::make_unique<SkinnedModelRenderer>();
	skinnedRenderer->Initialize(dxPtr->GetDevice());
	renderManager->RegisterRenderer(RenderPassType::SkinnedModel, std::move(skinnedRenderer));
	
	// SkyBoxRendererの作成と登録
	auto skyBoxRenderer = std::make_unique<SkyBoxRenderer>();
	skyBoxRenderer->Initialize(dxPtr->GetDevice());
	renderManager->RegisterRenderer(RenderPassType::SkyBox, std::move(skyBoxRenderer));
	
	// SpriteRendererの作成と登録
	auto spriteRenderer = std::make_unique<SpriteRenderer>();
	spriteRenderer->Initialize(dxPtr, resourcePtr);
	renderManager->RegisterRenderer(RenderPassType::Sprite, std::move(spriteRenderer));
	
	// TextRendererの作成と登録
	auto textRenderer = std::make_unique<TextRenderer>();
	textRenderer->Initialize(dxPtr, resourcePtr);
	renderManager->RegisterRenderer(RenderPassType::Text, std::move(textRenderer));
	
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
	RegisterComponent(std::move(renderManager));
	
	// LineManagerの初期化（シングルトン、RenderManager登録後に実行）
	LineManager::GetInstance().Initialize(lineRendererPtr);

	// PostEffectManagerの作成と初期化
	auto postEffectManager = std::make_unique<PostEffectManager>();
	postEffectManager->Initialize(dxPtr, renderPtr);
	RegisterComponent(std::move(postEffectManager));

	// ModelManagerの作成と初期化
	auto modelManager = std::make_unique<ModelManager>();
	modelManager->Initialize(dxPtr, resourcePtr);
	RegisterComponent(std::move(modelManager));
}

void EngineSystem::CreateInputComponents()
{
	// InputManagerの作成と初期化
	auto inputManager = std::make_unique<InputManager>();
	inputManager->Initialize(winApp_->GetInstance(), winApp_->GetHwnd());

	// InputManagerからIDirectInput8を取得
	IDirectInput8* directInput = inputManager->GetDirectInput();
	HWND hwnd = winApp_->GetHwnd();

	// InputManager自身を登録
	RegisterComponent(std::move(inputManager));

	// KeyboardInputの作成と初期化
	auto keyboard = std::make_unique<KeyboardInput>();
	keyboard->Initialize(directInput, hwnd);
	RegisterComponent(std::move(keyboard));

	// MouseInputの作成と初期化
	auto mouse = std::make_unique<MouseInput>();
	mouse->Initialize(directInput, hwnd);
	RegisterComponent(std::move(mouse));

	// GamepadInputの作成と初期化
	auto gamepad = std::make_unique<GamepadInput>();
	gamepad->Initialize(directInput, hwnd);
	RegisterComponent(std::move(gamepad));
}

void EngineSystem::CreateAudioComponents()
{
	// SoundManagerの作成と初期化
	auto soundManager = std::make_unique<SoundManager>();
	soundManager->Initialize();
	RegisterComponent(std::move(soundManager));
}

void EngineSystem::CreateLightComponents()
{
	auto lightManager = std::make_unique<LightManager>();
	auto* dxCommon = GetComponent<DirectXCommon>();
	auto* resourceFactory = GetComponent<ResourceFactory>();
	auto* descriptorManager = dxCommon->GetDescriptorManager();
	
	lightManager->Initialize(
		dxCommon->GetDevice(),
		resourceFactory,
		descriptorManager
	);
	
	// デフォルトライトは作成しない（各シーンで個別に作成する）

	LightManager* lightManagerPtr = lightManager.get();
	RegisterComponent(std::move(lightManager));

	// RenderManagerにLightManagerを設定
	auto* renderManager = GetComponent<RenderManager>();
	if (renderManager) {
		auto* modelRenderer = dynamic_cast<ModelRenderer*>(renderManager->GetRenderer(RenderPassType::Model));
		if (modelRenderer) {
			modelRenderer->SetLightManager(lightManagerPtr);
		}

		auto* skinnedRenderer = dynamic_cast<SkinnedModelRenderer*>(renderManager->GetRenderer(RenderPassType::SkinnedModel));
		if (skinnedRenderer) {
			skinnedRenderer->SetLightManager(lightManagerPtr);
		}
	}
}

#pragma endregion
}
