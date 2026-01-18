#include "GameObjectManager.h"
#include "Engine/Graphics/Render/RenderManager.h"
#include <algorithm>

#ifdef _DEBUG
#include <imgui.h>
#endif


namespace CoreEngine
{
void GameObjectManager::UpdateAll() {
	// アクティブで削除マークされていないオブジェクトのみ更新
	// 削除はCleanupDestroyed()で行われるため、直接ループで問題ない
	for (auto& obj : objects_) {
		if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
			obj->Update();
		}
	}
}

void GameObjectManager::RegisterAllToRender(RenderManager* renderManager) {
	if (!renderManager) return;

	// アクティブで削除マークされていないオブジェクトのみ登録
	// 事前フィルタリング済みのため、RenderManager側では簡易チェックのみ
	for (auto& obj : objects_) {
		if (obj && obj->IsActive() && !obj->IsMarkedForDestroy()) {
			renderManager->AddDrawable(obj.get());
		}
	}
}

void GameObjectManager::CleanupDestroyed() {
	// ===== ダブルバッファリング同期による安全な削除 =====
	// 
	// 削除の流れ:
	// 1. 前フレームの削除キューをクリア（GPU使用完了を保証）
	//    → BackBufferPostDraw()でWaitForFrame()により、前フレームのGPU処理完了を待機済み
	//    → 安全にデストラクタを呼び出せる
	// 
	// 2. 現在フレームで削除マークされたオブジェクトを削除キューに移動
	//    → 次フレームのCleanupDestroyedで実際に破棄
	//    → GPU処理中のリソースを誤って破棄することを防ぐ
	
	// 前フレームの削除キューをクリア（デストラクタ呼び出し）
	destroyQueue_.clear();

	// 削除マークされたオブジェクトを削除キューに移動
	// std::remove_ifとeraseのイディオムを使用して効率的に削除
	objects_.erase(
		std::remove_if(objects_.begin(), objects_.end(),
			[this](auto& obj) {
				// unique_ptrの有効性チェック
				if (!obj) {
					return true;
				}
				
				// 削除マークされている場合は削除キューに移動
				if (obj->IsMarkedForDestroy()) {
					destroyQueue_.push_back(std::move(obj));
					return true;
				}
				
				return false;
			}),
		objects_.end()
	);
}

void GameObjectManager::Clear() {
	objects_.clear();
	destroyQueue_.clear();
}

#ifdef _DEBUG
void GameObjectManager::DrawAllImGui() {
	ImGui::Text("Total Objects: %zu", objects_.size());
	ImGui::Text("Destroy Queue: %zu", destroyQueue_.size());
	ImGui::Separator();

	// アクティブなオブジェクトのImGuiを表示
	for (auto& obj : objects_) {
		if (obj) {
			// 削除マークされているオブジェクトは赤で表示
			if (obj->IsMarkedForDestroy()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
			}

			obj->DrawImGui();

			if (obj->IsMarkedForDestroy()) {
				ImGui::PopStyleColor();
			}
		}
	}
}
#endif
}
