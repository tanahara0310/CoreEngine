#pragma once

#include <string>
#include <vector>
#include "Utility/JsonManager/JsonManager.h"
#include "Math/MathCore.h"

namespace CoreEngine
{
// 前方宣言（CPU/GPU共通インターフェース。両バックエンドで同じプリセットを使える）
class IParticleSystem;

/// @brief パーティクルプリセット管理クラス
class ParticlePresetManager {
public:
    ParticlePresetManager() = default;
    ~ParticlePresetManager() = default;

    /// @brief パーティクルシステムの設定をファイルに保存
    /// @param particleSystem 保存するパーティクルシステム（CPU/GPUどちらでも可）
    /// @param filePath 保存先ファイルパス
    /// @return 保存に成功した場合true
    bool SavePreset(IParticleSystem* particleSystem, const std::string& filePath);

    /// @brief ファイルからパーティクルシステムの設定を読み込み
    /// @param particleSystem 読み込み先パーティクルシステム（CPU/GPUどちらでも可）
    /// @param filePath 読み込むファイルパス
    /// @return 読み込みに成功した場合true
    bool LoadPreset(IParticleSystem* particleSystem, const std::string& filePath);

    /// @brief 指定ディレクトリ内のプリセットファイル一覧を取得
    /// @param directory ディレクトリパス
    /// @return プリセットファイル名のリスト
    std::vector<std::string> GetPresetList(const std::string& directory);

    /// @brief ImGuiでファイル操作UIを表示
    /// @param particleSystem 対象のパーティクルシステム
    void ShowImGui(IParticleSystem* particleSystem);

    /// @brief 現在読み込まれているプリセットファイルのパスを取得
    /// @return ファイルパス（読み込まれていない場合は空文字列）
    std::string GetCurrentPresetPath() const { return currentPresetPath_; }

    /// @brief 現在のプリセットを上書き保存
    /// @param particleSystem 対象のパーティクルシステム
    /// @return 保存に成功した場合true
    bool SaveCurrentPreset(IParticleSystem* particleSystem);

private:
    // UI関連の状態
    char saveFileNameBuffer_[256] = "NewPreset";
    char directoryPathBuffer_[512] = "Assets/Presets/Particle/";
    std::vector<std::string> presetFileList_;
    int selectedPresetIndex_ = -1;
    bool needUpdateFileList_ = true;
    std::string currentPresetPath_;  // 現在読み込まれているプリセットのパス
    std::string currentPresetName_;  // 現在読み込まれているプリセット名（表示用）

    // 操作結果の一時表示（モーダルの代わりに数秒表示するステータス行）
    std::string statusMessage_;
    bool statusIsError_ = false;
    double statusExpireTime_ = 0.0;

    /// @brief 操作結果メッセージを数秒間表示する
    void SetStatus(const std::string& message, bool isError);

    /// @brief ディレクトリパスとファイル名を区切り文字を補って連結する
    std::string BuildPresetPath(const std::string& fileName) const;

    /// @brief プリセットファイルリストを更新
    void UpdatePresetFileList();

    /// @brief ファイル名から拡張子を除いた名前を取得
    /// @param filename ファイル名
    /// @return 拡張子を除いたファイル名
    std::string GetFileNameWithoutExtension(const std::string& filename);
};
}
