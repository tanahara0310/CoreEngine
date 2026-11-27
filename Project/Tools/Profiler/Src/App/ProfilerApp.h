#pragma once

#include "App/ViewState.h"
#include "Capture/CaptureSession.h"
#include "Capture/FrameSnapshot.h"
#include "Capture/SourceInfoCache.h"
#include "View/FrameStripView.h"
#include "View/TimelineView.h"
#include "View/ZoneDetailPanel.h"
#include "View/ZoneStatsView.h"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

/// @file ProfilerApp.h
/// @brief 窓の中身（メニュー・接続の帯・フレームの帯・タブ）と、起動の引数・ファイルの選択・終わる前の確認

namespace CoreEngine::Profiler
{
    class ProfilerApp
    {
    public:
        /// @param arguments 起動の引数（exe の名前は含めない）。--connect [アドレス[:ポート]]・--open <ファイル>・.tracy のパス
        explicit ProfilerApp(const std::vector<std::wstring>& arguments);

        /// @brief ImGui のフレームの中で、窓の中身を描く
        void Frame();

        /// @brief フレームを描き終えてから呼ぶ。ファイルの選択の窓はここで開く
        void AfterFrame(HWND owner);

        /// @brief 窓の閉じるボタン。保存していない記録があれば確認を出す
        void RequestQuit();

        /// @brief 窓へ落とされたファイルを開く（path は UTF-8）
        void RequestOpen(const std::string& path);

        bool ShouldQuit() const { return quit_; }

        /// @brief 窓の題（記録を開いていればファイル名を足す）
        std::wstring WindowTitle() const;

    private:
        /// @brief 保存していない記録を捨てる前に確認が要る操作
        enum class PendingAction
        {
            None,
            Connect,
            OpenDialog,
            OpenPath,
            Quit,
        };

        void Request(PendingAction action);
        void Run(PendingAction action);

        void DrawMenuBar();
        void DrawConnectionStrip();
        void DrawNotice();
        void DrawEmptyState();
        void DrawWaiting();
        void DrawConnectPopup();
        void DrawDiscardPopup();
        void HandleShortcuts();
        void DrawCapture();
        void DrawTimelineTab();

        /// @brief 記録を差し替えたときに、選んだものと見ている範囲を初めに戻す（目標は残す）
        void ResetView();

        /// @brief 最初に選ぶフレームと、受け取りの最中に最新のフレームを選び続けるのを決める
        void PrepareView(bool live);

        /// @brief 下の段のタブを切り替える
        void RequestTab(ViewTab tab);

        CaptureSession session_;
        ZoneStatsView statsView_;
        SourceInfoCache sources_;
        FrameSnapshot frames_;
        ViewState view_;
        FrameStripView frameStrip_;
        TimelineView timeline_;
        ZoneDetailPanel detail_;
        uint64_t seenGeneration_ = 0;
        SessionSummary summary_;
        bool hasSummary_ = false;

        std::string connectAddress_ = "127.0.0.1";
        int connectPort_ = 8086;
        char addressBuffer_[128] = {};
        bool openConnectPopup_ = false;

        PendingAction pending_ = PendingAction::None;
        bool openDiscardPopup_ = false;
        std::string pendingPath_;

        bool openDialogRequested_ = false;
        bool saveDialogRequested_ = false;
        bool quit_ = false;
    };
}
