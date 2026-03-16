#pragma once

#ifdef _DEBUG

#include "ICameraEditorModule.h"
#include "Camera/CameraStructs.h"
#include "Math/Easing/EasingUtil.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief 保存済みカメラシーケンスを再生するモジュール
    class CameraClipPlayerModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "シーケンス再生"; }

        /// @brief 毎フレーム更新（再生状態の更新）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        struct ClipKeyframe {
            float time = 0.0f;
            CameraSnapshot snapshot{};
        };

        /// @brief クリップ内ショットの遷移方式
        enum class ShotTransitionType {
            Cut = 0,
            Blend = 1
        };

        /// @brief クリップ内ショット情報
        struct ClipShot {
            std::string name;
            float startTime = 0.0f;
            float endTime = 1.0f;
            bool enabled = true;
            ShotTransitionType transitionType = ShotTransitionType::Cut;
            float blendDuration = 0.2f;
        };

        /// @brief シーケンス一覧を更新
        void RefreshClipFileList();

        /// @brief シーケンスファイルを読み込む
        bool LoadClipFromFile(const std::string& filePath);

        /// @brief 指定時刻のスナップショットを評価
        bool EvaluateSnapshotAt(float time, CameraSnapshot& outSnapshot) const;

        /// @brief 生タイムライン時刻でスナップショットを評価
        bool EvaluateSnapshotRaw(float time, CameraSnapshot& outSnapshot) const;

        /// @brief 2つのスナップショットを補間
        CameraSnapshot InterpolateSnapshot(const CameraSnapshot& from, const CameraSnapshot& to, float t) const;

        /// @brief UI選択インデックスからイージングタイプへ変換
        EasingUtil::Type GetSelectedEasingType() const;

        /// @brief 指定時刻のショットインデックスを検索
        int FindShotIndexAt(float time) const;

        /// @brief スナップショットをアクティブ3Dカメラへ適用
        bool ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot) const;

    private:
        std::string clipDirectoryPath_ = "Application/Assets/Presets/CameraClips/";
        std::vector<std::string> clipFileList_;
        int selectedClipFileIndex_ = -1;
        bool needRefreshClipFileList_ = true;

        std::vector<ClipKeyframe> clipKeyframes_;
        std::vector<ClipShot> clipShots_;
        std::string loadedClipName_;
        float timelineLength_ = 10.0f;
        float playhead_ = 0.0f;
        bool isPlaying_ = false;
        bool shotsEnabled_ = true;
        bool loopPlayback_ = true;
        float playbackSpeed_ = 1.0f;
        int easingTypeIndex_ = 0;

        std::string statusMessage_;
    };
}

#endif // _DEBUG
