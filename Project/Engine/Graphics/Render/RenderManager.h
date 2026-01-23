#pragma once

#include "IRenderer.h"
#include "RenderPassType.h"
#include "Engine/Graphics/PipelineStateManager.h"
#include <d3d12.h>
#include <unordered_map>
#include <vector>
#include <memory>

// 前方宣言
namespace CoreEngine {
	class IDrawable;
	class GameObject;
	class ICamera;
	class CameraManager;
}

/// @brief レンダリング全体を自動管理するマネージャー

namespace CoreEngine
{
class RenderManager {
public:
	/// @brief 初期化
	/// @param device D3D12デバイス
	void Initialize(ID3D12Device* device);
	
	/// @brief レンダラーを登録
	/// @param type 描画パスタイプ
	/// @param renderer レンダラーのユニークポインタ
	void RegisterRenderer(RenderPassType type, std::unique_ptr<IRenderer> renderer);
	
	/// @brief レンダラーを取得
	/// @param type 描画パスタイプ
	/// @return レンダラーポインタ
	IRenderer* GetRenderer(RenderPassType type);
	
	/// @brief カメラマネージャーを設定
	/// @param cameraManager カメラマネージャー
	void SetCameraManager(CoreEngine::CameraManager* cameraManager);
	
	
	
	/// @brief カメラを設定（従来の互換性維持版）
	/// @param camera カメラオブジェクト
	void SetCamera(const CoreEngine::ICamera* camera);
	
	/// @brief コマンドリストを設定（フレームごとに1回）
	/// @param cmdList コマンドリスト
	void SetCommandList(ID3D12GraphicsCommandList* cmdList);
	
	/// @brief 描画対象オブジェクトをキューに追加
	/// @param obj 描画するGameObject
	void AddDrawable(CoreEngine::GameObject* obj);
	
	/// @brief キューに登録された全オブジェクトを描画
	void DrawAll();
	
	/// @brief フレーム終了時にキューをクリア
	void ClearQueue();
	
private:
	struct DrawCommand {
		CoreEngine::GameObject* object;
		RenderPassType passType;
		BlendMode blendMode;
	};
	
	std::vector<DrawCommand> drawQueue_;
	std::unordered_map<RenderPassType, std::unique_ptr<IRenderer>> renderers_;
	
	// フレームごとに設定されるコンテキスト
	ID3D12GraphicsCommandList* cmdList_ = nullptr;
	CoreEngine::CameraManager* cameraManager_ = nullptr;
	const CoreEngine::ICamera* camera_ = nullptr; // 従来の互換性維持用
	
	/// @brief 描画パスごとにソート
	void SortDrawQueue();
	
	/// @brief 描画パスタイプに応じた適切なカメラを取得
	/// @param passType 描画パスタイプ
	/// @return カメラポインタ
	const CoreEngine::ICamera* GetCameraForPass(RenderPassType passType);
};
}
