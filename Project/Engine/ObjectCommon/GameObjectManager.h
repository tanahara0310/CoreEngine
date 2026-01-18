#pragma once

#include "GameObject.h"
#include <memory>
#include <vector>

// Forward declaration
namespace CoreEngine {
	class RenderManager;
	class ICamera;
}

/// @brief すべてのGameObjectを一元管理するマネージャー
/// @note 更新、描画、削除を自動化し、使用者は登録とDestroyのみを意識する

namespace CoreEngine
{
class GameObjectManager {
public:
	/// @brief オブジェクトを登録（所有権を移動）
	/// @tparam T GameObjectの派生クラス
	/// @param obj 登録するオブジェクトのユニークポインタ
	/// @return 登録されたオブジェクトへの生ポインタ（操作用）
	template<typename T>
	T* AddObject(std::unique_ptr<T> obj) {
		static_assert(std::is_base_of_v<GameObject, T>, "T must derive from GameObject");
		T* ptr = obj.get();
		objects_.push_back(std::move(obj));
		return ptr;
	}

	/// @brief 全オブジェクトの更新処理
	void UpdateAll();

	/// @brief 全オブジェクトをRenderManagerに登録して描画
	/// @param renderManager レンダーマネージャー
	void RegisterAllToRender(CoreEngine::RenderManager* renderManager);

	/// @brief フレーム終了時に削除マークされたオブジェクトを破棄
	/// @note 削除キューを使用して安全に破棄（GPU処理完了を考慮）
	void CleanupDestroyed();

	/// @brief 全オブジェクトをクリア（シーン終了時など）
	void Clear();

	/// @brief 登録されているオブジェクト数を取得
	/// @return オブジェクト数
	size_t GetObjectCount() const { return objects_.size(); }

	/// @brief 全オブジェクトのリストを取得（読み取り専用）
	/// @return オブジェクトリストの const 参照
	const std::vector<std::unique_ptr<GameObject>>& GetAllObjects() const { return objects_; }

#ifdef _DEBUG
	/// @brief 全オブジェクトのImGuiデバッグUI表示
	void DrawAllImGui();
#endif

private:
	/// @brief 管理中のオブジェクトリスト
	std::vector<std::unique_ptr<GameObject>> objects_;

	/// @brief 削除待ちキュー（フレーム終了後に破棄）
	std::vector<std::unique_ptr<GameObject>> destroyQueue_;
};
}
