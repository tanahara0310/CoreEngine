#pragma once

#include <string>
#include <vector>
#include "Engine/Utility/JsonManager/JsonManager.h"

// 前方宣言
namespace CoreEngine {
	class PostEffectManager;
}

/// @brief ポストエフェクトプリセット管理クラス

namespace CoreEngine
{
class PostEffectPresetManager {
public:
	PostEffectPresetManager() = default;
	~PostEffectPresetManager() = default;

	/// @brief ポストエフェクトの設定をファイルに保存
	/// @param postEffectManager 保存するポストエフェクトマネージャー
	/// @param filePath 保存先ファイルパス
	/// @return 保存に成功した場合true
	bool SavePreset(const CoreEngine::PostEffectManager* postEffectManager, const std::string& filePath);

	/// @brief ファイルからポストエフェクトの設定を読み込み
	/// @param postEffectManager 読み込み先ポストエフェクトマネージャー
	/// @param filePath 読み込むファイルパス
	/// @return 読み込みに成功した場合true
	bool LoadPreset(CoreEngine::PostEffectManager* postEffectManager, const std::string& filePath);

	/// @brief 指定ディレクトリ内のプリセットファイル一覧を取得
	/// @param directory ディレクトリパス
	/// @return プリセットファイル名のリスト
	std::vector<std::string> GetPresetList(const std::string& directory);

	/// @brief ImGuiでファイル操作UIを表示
	/// @param postEffectManager 対象のポストエフェクトマネージャー
	void ShowImGui(CoreEngine::PostEffectManager* postEffectManager);

	/// @brief 現在読み込まれているプリセットファイルのパスを取得
	/// @return ファイルパス（読み込まれていない場合は空文字列）
	std::string GetCurrentPresetPath() const { return currentPresetPath_; }

	/// @brief 現在のプリセットを上書き保存
	/// @param postEffectManager 対象のポストエフェクトマネージャー
	/// @return 保存に成功した場合true
	bool SaveCurrentPreset(CoreEngine::PostEffectManager* postEffectManager);

private:
	// UI関連の状態
	char saveFileNameBuffer_[256] = "NewPreset";
	char directoryPathBuffer_[512] = "Assets/Presets/PostEffect/";
	std::vector<std::string> presetFileList_;
	int selectedPresetIndex_ = -1;
	bool needUpdateFileList_ = true;
	std::string currentPresetPath_;  // 現在読み込まれているプリセットのパス
	std::string currentPresetName_;  // 現在読み込まれているプリセット名（表示用）

	/// @brief プリセットファイルリストを更新
	void UpdatePresetFileList();

	/// @brief ファイル名から拡張子を除いた名前を取得
	/// @param filename ファイル名
	/// @return 拡張子を除いたファイル名
	std::string GetFileNameWithoutExtension(const std::string& filename);
};
}
