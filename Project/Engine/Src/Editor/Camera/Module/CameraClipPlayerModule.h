#pragma once

#ifdef USE_IMGUI

#include "ICameraEditorModule.h"
#include "Camera/Sequence/CameraSequenceTypes.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief 保存済みカメラシーケンスを再生するモジュール
    /// @details 評価（時刻 → カメラ姿勢）は CameraSequenceEvaluator に任せ、
    ///          ここは読み込み・再生ヘッドの進行・UI だけを持つ。
    class CameraClipPlayerModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "シーケンス再生"; }

        /// @brief 毎フレーム更新（再生状態の更新）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        /// @brief シーケンス一覧を更新
        void RefreshClipFileList();

        /// @brief シーケンスファイルを読み込む
        bool LoadClipFromFile(const std::string& filePath);

        /// @brief スナップショットをアクティブ3Dカメラへ適用
        bool ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot) const;

    private:
        std::string clipDirectoryPath_ = CameraSequencePaths::kDirectory;
        std::vector<std::string> clipFileList_;
        int selectedClipFileIndex_ = -1;
        bool needRefreshClipFileList_ = true;

        // 読み込んだシーケンス本体（タイムライン長・イージング・ショットもここに含まれる）
        CameraSequenceAsset clip_;
        std::string loadedClipName_;

        // 再生状態（シーケンスには保存しない、この画面だけの状態）
        float playhead_ = 0.0f;
        bool isPlaying_ = false;
        bool loopPlayback_ = true;
        float playbackSpeed_ = 1.0f;

        std::string statusMessage_;
    };
}

#endif // USE_IMGUI
