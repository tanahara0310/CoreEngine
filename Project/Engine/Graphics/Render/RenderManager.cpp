#include "RenderManager.h"
#include "Engine/ObjectCommon/GameObject.h"
#include "Engine/Particle/ParticleSystem.h"
#include "Engine/Graphics/Render/Particle/ParticleRenderer.h"
#include "Engine/Graphics/Render/Particle/ModelParticleRenderer.h"
#include "Engine/Camera/CameraManager.h"
#include "Engine/Camera/ICamera.h"
#include <algorithm>


namespace CoreEngine
{
void RenderManager::Initialize(ID3D12Device* device) {
	// 現時点では特に初期化処理なし
	(void)device; // 未使用警告を回避
}

void RenderManager::RegisterRenderer(RenderPassType type, std::unique_ptr<IRenderer> renderer) {
	renderers_[type] = std::move(renderer);
}

IRenderer* RenderManager::GetRenderer(RenderPassType type) {
	auto it = renderers_.find(type);
	if (it != renderers_.end()) {
		return it->second.get();
	}
	return nullptr;
}

void RenderManager::SetCameraManager(CameraManager* cameraManager) {
	cameraManager_ = cameraManager;
}

void RenderManager::SetCamera(const ICamera* camera) {
	camera_ = camera;
	
	// 各レンダラーにもカメラを設定（互換性維持）
	for (auto& [type, renderer] : renderers_) {
		renderer->SetCamera(camera);
	}
}

void RenderManager::SetCommandList(ID3D12GraphicsCommandList* cmdList) {
	cmdList_ = cmdList;
}

void RenderManager::AddDrawable(GameObject* obj) {
	// GameObjectManagerで事前フィルタリング済み（null/Active/MarkedForDestroyチェック済み）
	DrawCommand cmd;
	cmd.object = obj;
	cmd.passType = obj->GetRenderPassType();
	cmd.blendMode = obj->GetBlendMode();

	drawQueue_.push_back(cmd);
}

const ICamera* RenderManager::GetCameraForPass(RenderPassType passType) {
	// カメラマネージャーがある場合はタイプ別のカメラを取得
	if (cameraManager_) {
		// 2D描画パス（Sprite、Text）は2Dカメラを使用
		if (passType == RenderPassType::Sprite || passType == RenderPassType::Text) {
			return cameraManager_->GetActiveCamera(CameraType::Camera2D);
		}
		// その他は3Dカメラを使用
		else {
			return cameraManager_->GetActiveCamera(CameraType::Camera3D);
		}
	}
	
	// カメラマネージャーがない場合は従来のカメラを使用
	return camera_;
}

void RenderManager::DrawAll() {
	if (drawQueue_.empty()) {
		#ifdef _DEBUG
			// 描画キューが空の場合は警告を出力（通常は問題ないが、意図しない場合に気づくため）
			static bool firstWarning = true;
			if (firstWarning) {
				OutputDebugStringA("INFO: RenderManager draw queue is empty.\n");
				firstWarning = false;
			}
		#endif
		return;
	}
	
	if (!cmdList_) {
		#ifdef _DEBUG
			OutputDebugStringA("ERROR: CommandList is null in RenderManager::DrawAll!\n");
		#endif
		return;
	}

	SortDrawQueue();

	RenderPassType currentPass = RenderPassType::Invalid;
	IRenderer* currentRenderer = nullptr;
	const ICamera* currentCamera = nullptr;

	for (const auto& cmd : drawQueue_) {
		// GameObjectManagerで事前フィルタリング済み
		// 削除マークのみチェック（更新中に削除マークされた可能性があるため）
		if (!cmd.object || cmd.object->IsMarkedForDestroy()) {
			continue;
		}

		// パスが切り替わったら処理
		if (cmd.passType != currentPass) {
			// 前のパスを終了
			if (currentRenderer) {
				currentRenderer->EndPass();
			}

			// 新しいパスを開始
			currentPass = cmd.passType;
			auto it = renderers_.find(currentPass);
			if (it != renderers_.end()) {
				currentRenderer = it->second.get();
				
				// パスに応じたカメラを取得
				currentCamera = GetCameraForPass(currentPass);
				currentRenderer->SetCamera(currentCamera);
				
				currentRenderer->BeginPass(cmdList_, cmd.blendMode);
			} else {
				#ifdef _DEBUG
					OutputDebugStringA("WARNING: Renderer not found for pass type!\n");
				#endif
				currentRenderer = nullptr;
				currentCamera = nullptr;
			}
		}

		// オブジェクトを描画
		if (currentRenderer) {
			cmd.object->Draw(currentCamera);
			
			// パーティクルの場合は、レンダラーに描画コマンド発行を委託
			if (cmd.passType == RenderPassType::Particle) {
				if (auto* particleRenderer = static_cast<ParticleRenderer*>(currentRenderer)) {
					auto* particleSystem = static_cast<ParticleSystem*>(cmd.object);
					particleRenderer->Draw(particleSystem);
				}
			}
			// モデルパーティクルの場合
			else if (cmd.passType == RenderPassType::ModelParticle) {
				if (auto* modelParticleRenderer = static_cast<ModelParticleRenderer*>(currentRenderer)) {
					auto* particleSystem = static_cast<ParticleSystem*>(cmd.object);
					modelParticleRenderer->Draw(particleSystem);
				}
			}
		}
	}

	// 最後のパスを終了
	if (currentRenderer) {
		currentRenderer->EndPass();
	}
}

void RenderManager::ClearQueue() {
	drawQueue_.clear();
}

void RenderManager::SortDrawQueue() {
	// 描画コマンドを最適化してステート変更を最小化
	// 優先順位: 1. パスタイプ > 2. ブレンドモード
	std::sort(drawQueue_.begin(), drawQueue_.end(),
		[](const DrawCommand& a, const DrawCommand& b) {
			// 1. パスタイプでソート（パイプライン切り替え最小化）
			if (a.passType != b.passType) {
				return static_cast<int>(a.passType) < static_cast<int>(b.passType);
			}
			
			// 2. 同一パス内ではブレンドモードでソート（ブレンドステート切り替え最小化）
			if (a.blendMode != b.blendMode) {
				return static_cast<int>(a.blendMode) < static_cast<int>(b.blendMode);
			}
			
			// 3. その他の条件が同じ場合は順序を維持（安定ソート）
			return false;
		});
}
}
