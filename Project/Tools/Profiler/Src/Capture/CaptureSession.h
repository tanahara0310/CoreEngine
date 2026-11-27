#pragma once

#include "TracyWorker.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

/// @file CaptureSession.h
/// @brief Tracy の受け取り役（Worker）を 1 つ持ち、つなぐ・止める・保存する・開くを受け持つ

namespace CoreEngine::Profiler
{
    /// @brief 記録の今の状態
    enum class SessionState
    {
        Empty,       ///< 記録が無い
        Connecting,  ///< 相手を探している（相手が起動するまで繰り返す）
        Live,        ///< つながって受け取っている
        Stopped,     ///< 受け取りを止めた（受け取った分は残っている）
        Loading,     ///< ファイルを読み込んでいる
        File,        ///< ファイルから開いた
    };

    /// @brief 接続の帯に出すお知らせの重さ
    enum class NoticeLevel
    {
        Info,
        Warning,
        Error,
    };

    /// @brief 接続の帯に出す、記録の要約（鍵をかけて写したもの）
    struct SessionSummary
    {
        std::string program;     ///< 相手の exe の名前
        std::string captureName; ///< 相手が付けた記録の名前
        uint64_t pid = 0;
        uint64_t frameCount = 0;
        int64_t span = 0;        ///< 記録の長さ（ナノ秒）
        uint64_t zoneCount = 0;
    };

    class CaptureSession
    {
    public:
        CaptureSession() = default;
        ~CaptureSession();

        CaptureSession(const CaptureSession&) = delete;
        CaptureSession& operator=(const CaptureSession&) = delete;

        /// @brief 今の記録を捨てて、address:port の相手を探し始める
        void Connect(const std::string& address, uint16_t port);

        /// @brief 今の記録を捨てて、.tracy を裏で読み込み始める（path は UTF-8）
        void Open(const std::string& path);

        /// @brief 受け取りを止める（受け取った分は残す）
        void Stop();

        /// @brief 記録を .tracy へ裏で書き出し始める（path は UTF-8。受け取りを止めてから）
        /// @return 書き出しを始められなければ false（理由はお知らせに出る）
        bool Save(const std::string& path);

        /// @brief 毎フレーム呼ぶ。つながったか・切れたか・読み込みと保存が終わったかを見る
        void Update();

        SessionState State() const { return state_; }
        bool IsBusy() const { return loading_ || saving_; }
        bool IsSaving() const { return saving_; }
        bool CanSave() const;
        bool HasUnsavedData() const { return unsaved_; }

        /// @brief 記録を差し替えるたびに増える番号（画面はこれが変わったら写した中身を捨てる）
        uint64_t Generation() const { return generation_; }

        const std::string& Address() const { return address_; }
        uint16_t Port() const { return port_; }
        const std::string& FilePath() const { return filePath_; }

        const std::string& Notice() const { return notice_; }
        NoticeLevel NoticeLevelOf() const { return noticeLevel_; }
        void ClearNotice() { notice_.clear(); }

        /// @brief 鍵をかけて受け取り役の記録を読む
        /// @return 記録が無い・読み込み中・保存中のときは読まずに false
        template<class Func>
        bool Read(Func&& func)
        {
            if (!worker_ || loading_ || saving_) {
                return false;
            }
            auto lock = worker_->ObtainLockForMainThread();
            func(static_cast<const tracy::Worker&>(*worker_));
            return true;
        }

        /// @brief 記録の要約を鍵をかけて写す
        /// @return 写せなかったら false
        bool ReadSummary(SessionSummary& out);

    private:
        void ReleaseWorker();
        void SetNotice(NoticeLevel level, std::string text);
        void FinishLoad();
        void FinishSave();

        std::unique_ptr<tracy::Worker> worker_;
        SessionState state_ = SessionState::Empty;
        uint64_t generation_ = 0;
        bool unsaved_ = false;
        bool stopRequested_ = false;

        std::string address_;
        uint16_t port_ = 0;
        std::string filePath_;

        std::string notice_;
        NoticeLevel noticeLevel_ = NoticeLevel::Info;

        // 裏の読み込み
        std::thread loadThread_;
        bool loading_ = false;
        std::atomic<bool> loadDone_ = false;
        std::unique_ptr<tracy::Worker> loadedWorker_;
        std::string loadError_;
        std::string loadPath_;

        // 裏の保存
        std::thread saveThread_;
        bool saving_ = false;
        std::atomic<bool> saveDone_ = false;
        bool saveSucceeded_ = false;
        std::string savePath_;
    };
}
