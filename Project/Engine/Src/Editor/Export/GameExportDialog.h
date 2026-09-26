#pragma once

#ifdef CORE_EDITOR

#include "Editor/Export/GameExporter.h"

#include <future>
#include <memory>

namespace CoreEngine::Editor
{
    /// @brief 「ゲームを書き出す」の窓
    /// @details 何をどこへ写すかを見せ、書き出しを別のスレッドで行って進み具合を出す。
    class GameExportDialog
    {
    public:
        /// @brief 窓を開く
        /// @param sceneDirty 保存していない変更があるか（書き出しに入らないことを知らせる）
        void Open(bool sceneDirty);

        /// @brief 窓を描く（毎フレーム呼ぶ）
        void Draw();

    private:
        /// @brief 窓の状態
        enum class State { Closed, Ready, Exporting, Done };

        void DrawReady();
        void DrawExporting();
        void DrawDone();

        /// @brief 右寄せでボタンを並べる位置へ寄せる
        static void AlignButtons(float width);

        State state_ = State::Closed;
        bool requestOpen_ = false;
        bool sceneDirty_ = false;
        ExportPlan plan_;
        std::unique_ptr<ExportProgress> progress_;
        std::future<ExportResult> task_;
        ExportResult result_;
    };
}

#endif // CORE_EDITOR
