#pragma once

#ifdef USE_IMGUI

#include "Camera/Sequence/CameraSequenceTypes.h"

namespace CoreEngine
{
    /// @brief キー・ショット・イベントを 1 本の時間軸に重ねて描くタイムライン
    ///
    /// @details
    /// 3 つのレーンを同じ時間軸で縦に並べる。以前は高さ 34px の線 1 本にキーの点を
    /// 打つだけで、ショットもイベントも見えず、キーは時刻を数値入力しないと動かせなかった。
    ///
    /// 操作:
    ///  - キーをドラッグ  … 時刻を動かす（スナップあり）
    ///  - 目盛りをドラッグ … 再生ヘッドを動かす
    ///  - ホイール        … カーソル位置を軸にズーム
    ///  - 中ドラッグ      … 横スクロール
    ///  - ダブルクリック  … 全体表示へ戻す
    ///
    /// @note ウィジェットは選択やデータの所有者ではない。入力の結果を Result で返し、
    ///       実際の書き換え（Undo を含む）は呼び出し側が行う。
    class CameraTimelineWidget {
    public:
        /// @brief 1 フレームの操作結果
        struct Result {
            /// @brief 再生ヘッドが動いた
            bool playheadChanged = false;

            /// @brief キーのドラッグを開始した（この瞬間に Undo を積む）
            bool keyDragStarted = false;

            /// @brief キーの時刻が変わった（ドラッグ中は毎フレーム true）
            bool keyTimeChanged = false;

            /// @brief 選択が変わった
            bool selectionChanged = false;

            /// @brief 選択されたキー（-1 = 変更なし/未選択）
            int selectedKeyframe = -1;

            /// @brief 選択されたショット（-1 = 変更なし/未選択）
            int selectedShot = -1;

            /// @brief 選択されたイベント（-1 = 変更なし/未選択）
            int selectedEvent = -1;
        };

        /// @brief タイムラインを描画し、操作を処理する
        /// @param sequence 編集対象（キーの時刻はドラッグで書き換わる）
        /// @param playhead 再生ヘッド [秒]（ドラッグで書き換わる）
        /// @param selectedKeyframe 現在選択中のキー添字
        /// @param selectedShot 現在選択中のショット添字
        /// @param selectedEvent 現在選択中のイベント添字
        /// @param snapSeconds スナップ間隔 [秒]（0 以下でスナップなし）
        Result Draw(CameraSequenceAsset& sequence, float& playhead,
            int selectedKeyframe, int selectedShot, int selectedEvent,
            float snapSeconds);

        /// @brief 表示範囲を全体へ戻す
        void ResetView();

    private:
        /// @brief 表示範囲を有効な範囲へ収める
        void ClampView(float duration);

        // 表示範囲 [秒]。viewDuration_ が 0 以下なら「全体表示」を意味する
        float viewStart_ = 0.0f;
        float viewDuration_ = 0.0f;

        // ドラッグ中の対象（-1 = なし）
        int draggingKeyframe_ = -1;
        bool draggingPlayhead_ = false;
    };
}

#endif // USE_IMGUI
