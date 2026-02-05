#pragma once

#include <string>
#include <vector>
#include "Engine/Utility/JsonManager/JsonManager.h"
#include "MathCore.h"

namespace CoreEngine
{
// 前方宣言
class ParticleSystem;

/// @brief パーティクルプリセット管理クラス
class ParticlePresetManager {
public:
    ParticlePresetManager() = default;
    ~ParticlePresetManager() = default;

    /// @brief パーティクルシステムの設定をファイルに保存
    /// @param particleSystem 保存するパーティクルシステム
    /// @param filePath 保存先ファイルパス
    /// @return 保存に成功した場合true
    bool SavePreset(const ParticleSystem* particleSystem, const std::string& filePath);

    /// @brief ファイルからパーティクルシステムの設定を読み込み
    /// @param particleSystem 読み込み先パーティクルシステム
    /// @param filePath 読み込むファイルパス
    /// @return 読み込みに成功した場合true
    bool LoadPreset(ParticleSystem* particleSystem, const std::string& filePath);

    /// @brief 指定ディレクトリ内のプリセットファイル一覧を取得
    /// @param directory ディレクトリパス
    /// @return プリセットファイル名のリスト
    std::vector<std::string> GetPresetList(const std::string& directory);

    /// @brief ImGuiでファイル操作UIを表示
    /// @param particleSystem 対象のパーティクルシステム
    void ShowImGui(ParticleSystem* particleSystem);

    /// @brief 現在読み込まれているプリセットファイルのパスを取得
    /// @return ファイルパス（読み込まれていない場合は空文字列）
    std::string GetCurrentPresetPath() const { return currentPresetPath_; }

    /// @brief 現在のプリセットを上書き保存
    /// @param particleSystem 対象のパーティクルシステム
    /// @return 保存に成功した場合true
    bool SaveCurrentPreset(ParticleSystem* particleSystem);

private:
    // UI関連の状態
    char saveFileNameBuffer_[256] = "NewPreset";
    char directoryPathBuffer_[512] = "Assets/Presets/Particle/";
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
