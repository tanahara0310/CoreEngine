#pragma once

#include <string>
#include <vector>
#include "Engine/Utility/JsonManager/JsonManager.h"
#include "Engine/Camera/CameraStructs.h"

namespace CoreEngine
{
// 前方宣言
class ICamera;
class DebugCamera;
class Camera;

/// @brief カメラプリセット管理クラス
class CameraPresetManager {
public:
    CameraPresetManager() = default;
    ~CameraPresetManager() = default;

    /// @brief カメラ設定をファイルに保存
    /// @param camera 保存するカメラ
    /// @param filePath 保存先ファイルパス
    /// @return 保存に成功した場合true
    bool SavePreset(ICamera* camera, const std::string& filePath);

    /// @brief ファイルからカメラ設定を読み込み
    /// @param camera 読み込み先カメラ
    /// @param filePath 読み込むファイルパス
    /// @return 読み込みに成功した場合true
    bool LoadPreset(ICamera* camera, const std::string& filePath);

    /// @brief 指定ディレクトリ内のプリセットファイル一覧を取得
    /// @param directory ディレクトリパス
    /// @return プリセットファイル名のリスト
    std::vector<std::string> GetPresetList(const std::string& directory);

    /// @brief ImGuiでファイル操作UIを表示
    /// @param camera 対象のカメラ
    void ShowImGui(ICamera* camera);

    /// @brief 現在読み込まれているプリセットファイルのパスを取得
    /// @return ファイルパス（読み込まれていない場合は空文字列）
    std::string GetCurrentPresetPath() const { return currentPresetPath_; }

    /// @brief 現在のプリセットを上書き保存
    /// @param camera 対象のカメラ
    /// @return 保存に成功した場合true
    bool SaveCurrentPreset(ICamera* camera);

    /// @brief プリセットファイルを削除
    /// @param filePath 削除するファイルパス
    /// @return 削除に成功した場合true
    bool DeletePreset(const std::string& filePath);

    /// @brief プリセットファイル一覧を取得（更新）
    /// @return プリセットファイル名のリスト
    const std::vector<std::string>& GetCurrentPresetList();

private:
    // UI関連の状態
    char saveFileNameBuffer_[256] = "NewCameraPreset";
    char directoryPathBuffer_[512] = "Assets/Presets/Camera/";
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

    /// @brief CameraSnapshotをJSONに変換
    /// @param snapshot スナップショット
    /// @return JSONオブジェクト
    json SnapshotToJson(const CameraSnapshot& snapshot);

    /// @brief JSONからCameraSnapshotに変換
    /// @param jsonData JSONオブジェクト
    /// @return スナップショット
    CameraSnapshot JsonToSnapshot(const json& jsonData);
};
}
