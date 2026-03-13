#pragma once

#ifdef _DEBUG

#include <string>

namespace CoreEngine
{
    class CameraPresetManager;
    class ICamera;

    /// @brief カメラプリセットのImGui編集UIを担当するクラス
    class CameraPresetEditor {
    public:
        /// @brief プリセットUIを描画
        /// @param presetManager プリセット管理
        /// @param camera 対象カメラ
        void DrawUI(CameraPresetManager& presetManager, ICamera* camera);

    private:
        /// @brief フルパスから表示用プリセット名を取得
        std::string GetPresetDisplayName(const std::string& fullPath) const;

    private:
        // UI状態（保存）
        char saveFileNameBuffer_[256] = "NewCameraPreset";
        char directoryPathBuffer_[512] = "Assets/Presets/Camera/";

        // UI状態（選択）
        int selectedPresetIndex_ = -1;
    };
}

#endif // _DEBUG
