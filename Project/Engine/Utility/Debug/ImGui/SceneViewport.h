#pragma once
#include <imgui.h>

/// @brief シーンビューポートクラス

namespace CoreEngine
{

class DirectXCommon;
class PostEffectManager;

class SceneViewport {
public: // メンバ関数
	/// @brief シーンビューポートの描画
	/// @param dxCommon DirectXCommonへのポインタ
	/// @param postEffectManager PostEffectManagerへのポインタ（オプション）
	void DrawSceneViewport(DirectXCommon* dxCommon, PostEffectManager* postEffectManager = nullptr);

	// ギズモ用にビューポートの矩形情報を取得
	ImVec2 GetViewportPos() const { return viewportPos_; }
	ImVec2 GetViewportSize() const { return viewportSize_; }
	
	/// @brief ビューポートがアクティブ（マウスがその上にあるか）
	/// @return ビューポートがアクティブならtrue
	bool IsViewportHovered() const { return isViewportHovered_; }

private: // メンバ変数
	ImVec2 viewportPos_ {};
	ImVec2 viewportSize_ {};
	bool isViewportHovered_ = false;
};
}
