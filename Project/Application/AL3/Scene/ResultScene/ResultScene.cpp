#include "ResultScene.h"
#include "Engine/Input/KeyboardInput.h"
#include "Scene/SceneManager.h"

#ifdef _DEBUG
#include "Engine/Camera/Debug/CameraDebugUI.h"
#endif

void ResultScene::Initialize(CoreEngine::EngineSystem* engine)
{
	// 基底クラスの初期化（カメラ、ライト、グリッド等のセットアップ）
	CoreEngine::BaseScene::Initialize(engine);
}

void ResultScene::OnUpdate()
{
	// KeyboardInput を直接取得
	auto keyboard = engine_->GetComponent<CoreEngine::KeyboardInput>();
	if (!keyboard) {
		return;
	}

	// Tキーでタイトルシーンへ遷移
	if (keyboard->IsKeyTriggered(DIK_T)) {
		if (sceneManager_) {
			sceneManager_->ChangeScene("TitleScene");
		}
	}
}

void ResultScene::Draw()
{
	// 基底クラスの描画
	CoreEngine::BaseScene::Draw();
}

void ResultScene::Finalize()
{
	// 基底クラスの終了処理
	CoreEngine::BaseScene::Finalize();
}
