#pragma once

#include <string>

class TextureManager;

/// @brief テクスチャビューアークラス

namespace CoreEngine
{
class TextureViewer {
public:
	/// @brief テクスチャビューアーウィンドウを描画
	/// @param isOpen 表示状態（外部から制御）
	void DrawTextureViewer(bool isOpen);

private:
	float thumbnailSize_ = 128.0f;
	int selectedTextureIndex_ = -1;
	std::string selectedTexturePath_;
};
}
