#pragma once

#ifdef _DEBUG

#include "ICameraEditorModule.h"
#include "Camera/CameraStructs.h"
#include "Math/Easing/EasingUtil.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief カメラワーク用のキーフレーム編集モジュール
    class CameraKeyframeEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "キーフレーム"; }

        /// @brief 毎フレーム更新（現時点では状態更新なし）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

    private:
        struct Keyframe {
            float time = 0.0f;
            CameraSnapshot snapshot{};
        };

        /// @brief ショット間の遷移方式
        enum class ShotTransitionType {
            Cut = 0,
            Blend = 1
        };

        /// @brief タイムライン上のショット定義
        struct Shot {
            std::string name;
            float startTime = 0.0f;
            float endTime = 1.0f;
            bool enabled = true;
            ShotTransitionType transitionType = ShotTransitionType::Cut;
            float blendDuration = 0.2f;
        };

        struct EditorState {
            std::vector<Keyframe> keyframes;
            std::vector<Shot> shots;
            float timelineLength = 10.0f;
            float playhead = 0.0f;
            int selectedIndex = -1;
            int selectedShotIndex = -1;
            bool isPlaying = false;
            bool shotsEnabled = true;
            bool loopPlayback = true;
            float playbackSpeed = 1.0f;
            int easingTypeIndex = 0;
        };

        /// @brief UI選択インデックスからイージングタイプへ変換
        EasingUtil::Type GetSelectedEasingType() const;

        /// @brief 指定時刻の補間済みスナップショットを評価
        bool EvaluateSnapshotAt(float time, CameraSnapshot& outSnapshot) const;

        /// @brief 生タイムライン時刻で補間済みスナップショットを評価
        bool EvaluateSnapshotRaw(float time, CameraSnapshot& outSnapshot) const;

        /// @brief 2つのスナップショットを線形補間
        CameraSnapshot InterpolateSnapshot(const CameraSnapshot& from, const CameraSnapshot& to, float t) const;

        /// @brief アクティブ3Dカメラからスナップショットを取得
        bool CaptureFromActiveCamera(const CameraEditorContext& context, CameraSnapshot& outSnapshot) const;

        /// @brief スナップショットをアクティブ3Dカメラへ適用
        bool ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot);

        /// @brief スナップショットが同一かを誤差込みで判定
        bool IsSameSnapshot(const CameraSnapshot& lhs, const CameraSnapshot& rhs) const;

        /// @brief Auto Key更新を実行
        void UpdateAutoKey(const CameraEditorContext& context);

        /// @brief Sceneビュー向けのカメラワーク可視化を描画
        void DrawViewportVisualization();

        /// @brief スナップショットからワールド座標のカメラ位置を取得
        Vector3 GetSnapshotWorldPosition(const CameraSnapshot& snapshot) const;

        /// @brief 指定時刻に最も近いキーフレームを検索
        int FindNearestKeyframeIndex(float time) const;

        /// @brief 指定時刻より前の最も近いキーフレームを検索
        int FindPreviousKeyframeIndex(float time) const;

        /// @brief 指定時刻より後の最も近いキーフレームを検索
        int FindNextKeyframeIndex(float time) const;

        /// @brief 指定時刻のショットインデックスを検索
        int FindShotIndexAt(float time) const;

        /// @brief シーケンスファイル一覧を更新
        void RefreshClipFileList();

        /// @brief 現在の編集状態をシーケンスとしてファイル保存
        bool SaveCurrentClipToFile(const std::string& filePath) const;

        /// @brief ファイルからシーケンスを読み込み
        bool LoadClipFromFile(const std::string& filePath);

        /// @brief 現在の編集状態を取得
        EditorState CaptureEditorState() const;

        /// @brief 編集状態を適用
        void ApplyEditorState(const EditorState& state);

        /// @brief Undo用に現在状態を保存
        void PushUndoState();

        /// @brief Undoを実行
        void Undo();

        /// @brief Redoを実行
        void Redo();

    private:
        std::vector<Keyframe> keyframes_;
        std::vector<Shot> shots_;
        float timelineLength_ = 10.0f;
        float playhead_ = 0.0f;
        int selectedIndex_ = -1;
        int selectedShotIndex_ = -1;
        float updateThreshold_ = 0.01f;
        bool isPlaying_ = false;
        bool shotsEnabled_ = true;
        bool loopPlayback_ = true;
        float playbackSpeed_ = 1.0f;
        int easingTypeIndex_ = 0;

        int editingShotNameIndex_ = -1;
        char shotNameBuffer_[128] = "";

        // シーケンス保存/読み込み
        char clipFileNameBuffer_[128] = "新規カメラシーケンス";
        std::string clipDirectoryPath_ = "Application/Assets/Presets/CameraClips/";
        std::vector<std::string> clipFileList_;
        int selectedClipFileIndex_ = -1;
        bool needRefreshClipFileList_ = true;

        // Undo/Redo
        std::vector<EditorState> undoStack_;
        std::vector<EditorState> redoStack_;
        size_t maxHistoryCount_ = 64;

        // Auto Key
        bool autoKeyEnabled_ = false;
        bool ignoreNextAutoKey_ = false;
        bool hasObservedSnapshot_ = false;
        bool autoKeyEditing_ = false;
        CameraSnapshot observedSnapshot_{};

        // ビューポート可視化
        bool viewportVisualizationEnabled_ = true;
        bool viewportShowTrajectory_ = true;
        bool viewportShowKeyMarkers_ = true;
        bool viewportShowDebugTarget_ = true;
        int viewportTrajectorySamplesPerSegment_ = 12;
        float viewportMarkerSize_ = 0.2f;
        Vector3 viewportTrajectoryColor_ = { 1.0f, 0.8f, 0.2f };
        Vector3 viewportKeyMarkerColor_ = { 0.2f, 0.8f, 1.0f };
        Vector3 viewportSelectedKeyColor_ = { 1.0f, 0.4f, 0.2f };
        Vector3 viewportDebugTargetColor_ = { 0.2f, 1.0f, 0.3f };
        float viewportTrajectoryAlpha_ = 0.9f;
    };
}

#endif // _DEBUG
