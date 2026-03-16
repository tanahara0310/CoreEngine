#pragma once

#ifdef _DEBUG

#include "ICameraEditorModule.h"
#include "Camera/CameraStructs.h"
#include "Math/Easing/EasingUtil.h"
#include "Utility/JsonManager/JsonManager.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief 保存済みカメラクリップを再生するモジュール
    class CameraClipPlayerModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "クリップ再生"; }

        /// @brief 毎フレーム更新（再生状態の更新）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        struct ClipKeyframe {
            float time = 0.0f;
            CameraSnapshot snapshot{};
        };

        /// @brief クリップ一覧を更新
        void RefreshClipFileList();

        /// @brief クリップファイルを読み込む
        bool LoadClipFromFile(const std::string& filePath);

        /// @brief 指定時刻のスナップショットを評価
        bool EvaluateSnapshotAt(float time, CameraSnapshot& outSnapshot) const;

        /// @brief 2つのスナップショットを補間
        CameraSnapshot InterpolateSnapshot(const CameraSnapshot& from, const CameraSnapshot& to, float t) const;

        /// @brief UI選択インデックスからイージングタイプへ変換
        EasingUtil::Type GetSelectedEasingType() const;

        /// @brief JSONからスナップショットを復元
        CameraSnapshot JsonToSnapshot(const json& jsonData) const;

        /// @brief スナップショットをアクティブ3Dカメラへ適用
        bool ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot) const;

    private:
        std::string clipDirectoryPath_ = "Application/Assets/Presets/CameraClips/";
        std::vector<std::string> clipFileList_;
        int selectedClipFileIndex_ = -1;
        bool needRefreshClipFileList_ = true;

        std::vector<ClipKeyframe> clipKeyframes_;
        std::string loadedClipName_;
        float timelineLength_ = 10.0f;
        float playhead_ = 0.0f;
        bool isPlaying_ = false;
        bool loopPlayback_ = true;
        float playbackSpeed_ = 1.0f;
        int easingTypeIndex_ = 0;

        std::string statusMessage_;
    };
}

#endif // _DEBUG
