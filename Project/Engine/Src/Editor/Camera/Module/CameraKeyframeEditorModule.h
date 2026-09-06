#pragma once

#ifdef USE_IMGUI

#include "ICameraEditorModule.h"
#include "Camera/Sequence/CameraSequenceEvaluator.h"
#include "Editor/Camera/Widget/CameraIconOverlay.h"
#include "Editor/Camera/Widget/CameraTimelineWidget.h"

#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief カメラワーク用のキーフレーム編集モジュール
    /// @details 編集中のタイムラインは CameraSequenceAsset そのもの。評価（時刻 → カメラ姿勢）は
    ///          CameraSequenceEvaluator に任せ、ここは編集操作と UI だけを持つ。
    class CameraKeyframeEditorModule final : public ICameraEditorModule {
    public:
        /// @brief タブ名を取得
        const char* GetTabName() const override { return "キーフレーム"; }

        /// @brief 毎フレーム更新（再生・オートキー・可視化）
        void Update(const CameraEditorContext& context) override;

        /// @brief タブ内容を描画
        void Draw(const CameraEditorContext& context) override;

        /// @brief ビューポート上へキーのアイコンとギズモを重ねる
        void DrawViewportOverlay(const CameraEditorContext& context, const Camera& viewCamera,
            const CameraEditorViewport& viewport) override;

    private:
        /// @brief Undo/Redo で退避する編集状態
        /// @details 持つのは「シーケンスの中身」と「どこを見ていたか」だけ。再生中か・
        ///          ループするか・速度は画面側の都合なので入れない。入れると Undo で
        ///          勝手に再生が始まったり止まったりする。
        struct EditorState {
            CameraSequenceAsset sequence;
            float playhead = 0.0f;
            int selectedIndex = -1;
            int selectedShotIndex = -1;
        };

        /// @brief アクティブ3Dカメラからスナップショットを取得
        bool CaptureFromActiveCamera(const CameraEditorContext& context, CameraSnapshot& outSnapshot) const;

        /// @brief スナップショットをアクティブ3Dカメラへ適用
        bool ApplyToActiveCamera(const CameraEditorContext& context, const CameraSnapshot& snapshot);

        /// @brief 指定時刻を評価してアクティブ3Dカメラへ反映
        void ApplyEvaluatedAt(const CameraEditorContext& context, float time);

        /// @brief 注視対象をシーンから引く解決口を組み立てる
        CameraSequenceAimContext MakeAimContext(const CameraEditorContext& context) const;

        /// @brief 再生・時刻・履歴の行を描画（常に最上段で位置が動かない）
        void DrawTransport(bool& playheadChanged);

        /// @brief タイムラインを描画し、操作結果を編集状態へ反映
        void DrawTimeline(bool& playheadChanged);

        /// @brief キー一覧と、その出し入れ操作を描画
        void DrawKeyList(const CameraEditorContext& context);

        /// @brief 選択キーの中身（構図 / 向き / 繋ぎ方）を描画
        void DrawKeyInspector(const CameraEditorContext& context, bool& playheadChanged);

        /// @brief 選択キーの構図（位置・回転・視野角）を描画
        void DrawKeyPose(const CameraEditorContext& context, CameraSequenceKeyframe& key, bool& playheadChanged);

        /// @brief 選択キーの向きの決め方を描画
        void DrawKeyAim(const CameraEditorContext& context, CameraSequenceKeyframe& key, bool& playheadChanged);

        /// @brief 選択キーから次のキーへの繋ぎ方を描画
        void DrawKeyTransition(CameraSequenceKeyframe& key, bool& playheadChanged);

        /// @brief ショットの編集 UI を描画
        void DrawShotTrack();

        /// @brief イベントトラックの編集 UI を描画
        void DrawEventTrack();

        /// @brief シーケンスの保存・読み込み UI を描画
        void DrawSequenceAssets(const CameraEditorContext& context);

        /// @brief ビューポート可視化の設定 UI を描画
        void DrawViewSettings();

        /// @brief 件数と保存先を出す最下段
        void DrawStatusBar();

        /// @brief キーの位置へカメラアイコンを描き、クリックで選択できるようにする
        /// @details Unity / Unreal がカメラの居場所に出しているアイコンと同じ役割。
        ///          線と違って距離によらず同じ大きさで見えるので、遠くのキーも掴める。
        void DrawKeyIcons(const CameraEditorContext& context, const Camera& viewCamera,
            const CameraEditorViewport& viewport);

        /// @brief 選択キーの位置・注視点をギズモで動かす
        void DrawKeyGizmo(const CameraEditorContext& context, const Camera& viewCamera);

        /// @brief キーの視錐台を線で描く
        /// @details そのキーが何を画に収めるつもりなのかを、覗き込まずに読めるようにする。
        void DrawKeyFrustum(const CameraSequenceKeyframe& key, const CameraSequenceAimContext& aim,
            const Vector3& color, float alpha) const;

        /// @brief 姿勢から視錐台を線で描く
        /// @details キーではなく「今の再生ヘッド位置の構図」を描くのに使う。
        void DrawFrustumFromPose(const Vector3& position, const Vector3& rotation,
            float fov, float aspectRatio, const Vector3& color, float alpha) const;

        /// @brief スナップショットが同一かを誤差込みで判定
        bool IsSameSnapshot(const CameraSnapshot& lhs, const CameraSnapshot& rhs) const;

        /// @brief Auto Key更新を実行
        void UpdateAutoKey(const CameraEditorContext& context);

        /// @brief Sceneビュー向けのカメラワーク可視化を描画
        void DrawViewportVisualization(const CameraEditorContext& context);

        /// @brief 指定時刻に最も近いキーフレームを検索
        int FindNearestKeyframeIndex(float time) const;

        /// @brief 指定時刻より前の最も近いキーフレームを検索
        int FindPreviousKeyframeIndex(float time) const;

        /// @brief 指定時刻より後の最も近いキーフレームを検索
        int FindNextKeyframeIndex(float time) const;

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

        /// @brief 指定した状態を Undo 履歴へ積む
        /// @details ドラッグを掴んだ時点で控えておいた状態を、値が動いてから積むために使う。
        void PushUndoState(const EditorState& state);

        /// @brief 直前のウィジェットの編集を履歴 1 件へまとめる
        /// @param changed そのウィジェットが値を変えたか
        /// @return changed をそのまま返す（if の条件へ差し込める）
        /// @details Drag / Slider は掴んでいる間ずっと「変更あり」を返すので、そのまま
        ///          PushUndoState を呼ぶと 1 回のドラッグで履歴が埋まり、Ctrl+Z が
        ///          効かなくなる。掴んだ時点の状態を控え、値が動いた最初のフレームだけ
        ///          積むことで、ドラッグ 1 回 = 履歴 1 件にする。
        bool TrackEdit(bool changed);

        /// @brief Undoを実行
        void Undo();

        /// @brief Redoを実行
        void Redo();

    private:
        // 編集中のシーケンス本体（キー・ショット・タイムライン長・イージングを含む）
        CameraSequenceAsset sequence_;

        // 編集・再生の状態（シーケンスには保存しない、この画面だけの状態）
        float playhead_ = 0.0f;
        int selectedIndex_ = -1;
        int selectedShotIndex_ = -1;
        float updateThreshold_ = 0.01f;
        bool isPlaying_ = false;
        bool loopPlayback_ = true;
        float playbackSpeed_ = 1.0f;

        // タイムライン（ズーム・スクロール・ドラッグの状態を持つ）
        CameraTimelineWidget timeline_;

        /// @brief キーをドラッグしたときに丸める間隔 [秒]（0 で無効）
        float snapSeconds_ = 0.1f;

        int editingShotNameIndex_ = -1;
        char shotNameBuffer_[128] = "";

        int editingKeyLabelIndex_ = -1;
        char keyLabelBuffer_[128] = "";

        // イベントトラック
        int selectedEventIndex_ = -1;
        int editingEventNameIndex_ = -1;
        char eventNameBuffer_[128] = "";

        // シーケンス保存/読み込み
        char clipFileNameBuffer_[128] = "新規カメラシーケンス";
        std::string clipDirectoryPath_ = CameraSequencePaths::kDirectory;
        std::vector<std::string> clipFileList_;
        int selectedClipFileIndex_ = -1;
        bool needRefreshClipFileList_ = true;

        // Undo/Redo
        std::vector<EditorState> undoStack_;
        std::vector<EditorState> redoStack_;
        size_t maxHistoryCount_ = 64;

        // 掴んでいるウィジェットの ImGui ID と、掴んだ時点の状態。
        // ImGuiID を持つためだけにヘッダへ imgui.h を引き込みたくないので素の型で持つ。
        unsigned int activeEditItemId_ = 0;
        EditorState pendingEditState_{};
        bool hasPendingEditState_ = false;

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
        bool viewportShowFrustum_ = true;
        bool viewportGizmoEnabled_ = true;
        bool viewportShowIcons_ = true;
        bool viewportShowPlayheadFrustum_ = true;

        /// @brief 非選択キーの視錐台の濃さ
        /// @details 明るく細かい床の上では 0.25 程度では読めない。既定を上げてある。
        float viewportFrustumIdleAlpha_ = 0.5f;

        /// @brief アイコンの大きさ倍率（1.0 で約 22px）
        float viewportIconScale_ = 1.0f;

        /// @brief 視錐台を描く長さ [m]（遠クリップまで描くと画面が埋まる）
        float viewportFrustumLength_ = 6.0f;

        // ギズモを掴んでいる最中か（履歴を 1 回だけ積むための印）
        bool gizmoDragging_ = false;
        int viewportTrajectorySamplesPerSegment_ = 12;
        float viewportMarkerSize_ = 0.2f;
        Vector3 viewportTrajectoryColor_ = { 1.0f, 0.8f, 0.2f };
        Vector3 viewportKeyMarkerColor_ = { 0.2f, 0.8f, 1.0f };
        Vector3 viewportSelectedKeyColor_ = { 1.0f, 0.4f, 0.2f };
        Vector3 viewportDebugTargetColor_ = { 0.2f, 1.0f, 0.3f };
        float viewportTrajectoryAlpha_ = 0.9f;
    };
}

#endif // USE_IMGUI
