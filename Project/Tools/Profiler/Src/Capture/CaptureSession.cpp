#include "Capture/CaptureSession.h"

#include "Text/TextFormat.h"

#include "TracyFileRead.hpp"
#include "TracyFileWrite.hpp"
#include "../public/common/TracyProtocol.hpp"
#include "../public/common/TracyVersion.hpp"

#include <filesystem>
#include <format>

namespace CoreEngine::Profiler
{
    namespace
    {
        /// @brief Tracy の版の番号（0xMMmmpp）を "M.m.p" にする
        std::string VersionText(int version)
        {
            return std::format("{}.{}.{}", version >> 16, (version >> 8) & 0xFF, version & 0xFF);
        }

        /// @brief .tracy を開いて受け取り役を作る。失敗したら error に理由を入れて nullptr を返す
        std::unique_ptr<tracy::Worker> LoadWorker(const std::string& path, std::string& error)
        {
            try {
                std::unique_ptr<tracy::FileRead> file(tracy::FileRead::Open(path.c_str()));
                if (!file) {
                    error = "ファイルを開けませんでした。";
                    return nullptr;
                }
                return std::make_unique<tracy::Worker>(*file);
            } catch (const tracy::UnsupportedVersion& e) {
                error = std::format("新しい版の Tracy（{}）で作られた記録なので開けません。", VersionText(e.version));
            } catch (const tracy::LegacyVersion& e) {
                error = std::format("古い版の Tracy（{}）で作られた記録なので開けません。", VersionText(e.version));
            } catch (const tracy::NotTracyDump&) {
                error = "Tracy の記録のファイルではありません。";
            } catch (const tracy::FileReadError&) {
                error = "ファイルの途中で読めなくなりました。";
            } catch (const tracy::LoadFailure& e) {
                error = "読み込めませんでした（" + e.msg + "）。";
            } catch (const std::exception& e) {
                error = std::string("読み込めませんでした（") + e.what() + "）。";
            }
            return nullptr;
        }
    }

    CaptureSession::~CaptureSession()
    {
        if (loadThread_.joinable()) {
            loadThread_.join();
        }
        if (saveThread_.joinable()) {
            saveThread_.join();
        }
        worker_.reset();
    }

    void CaptureSession::ReleaseWorker()
    {
        worker_.reset();
        state_ = SessionState::Empty;
        unsaved_ = false;
        stopRequested_ = false;
        filePath_.clear();
        ++generation_;
    }

    void CaptureSession::SetNotice(NoticeLevel level, std::string text)
    {
        noticeLevel_ = level;
        notice_ = std::move(text);
    }

    void CaptureSession::Connect(const std::string& address, uint16_t port)
    {
        if (IsBusy()) {
            return;
        }
        ReleaseWorker();
        address_ = address;
        port_ = port;
        worker_ = std::make_unique<tracy::Worker>(address.c_str(), port, -1);
        state_ = SessionState::Connecting;
        ClearNotice();
    }

    void CaptureSession::Open(const std::string& path)
    {
        if (IsBusy()) {
            return;
        }
        ReleaseWorker();
        ClearNotice();
        loading_ = true;
        loadDone_.store(false, std::memory_order_relaxed);
        loadPath_ = path;
        state_ = SessionState::Loading;
        loadThread_ = std::thread([this, path]() {
            std::string error;
            std::unique_ptr<tracy::Worker> worker = LoadWorker(path, error);
            loadedWorker_ = std::move(worker);
            loadError_ = std::move(error);
            loadDone_.store(true, std::memory_order_release);
        });
    }

    void CaptureSession::FinishLoad()
    {
        loadThread_.join();
        loading_ = false;
        if (loadedWorker_) {
            worker_ = std::move(loadedWorker_);
            state_ = SessionState::File;
            filePath_ = loadPath_;
            ++generation_;
        } else {
            state_ = SessionState::Empty;
            SetNotice(NoticeLevel::Error, loadError_);
        }
    }

    void CaptureSession::Stop()
    {
        if (!worker_ || IsBusy()) {
            return;
        }
        if (state_ == SessionState::Connecting) {
            ReleaseWorker();
            SetNotice(NoticeLevel::Info, "相手を探すのをやめました。");
            return;
        }
        if (state_ == SessionState::Live) {
            stopRequested_ = true;
            worker_->Disconnect();
        }
    }

    bool CaptureSession::CanSave() const
    {
        return worker_ && !IsBusy() && (state_ == SessionState::Stopped || state_ == SessionState::File);
    }

    bool CaptureSession::Save(const std::string& path)
    {
        if (!CanSave()) {
            if (state_ == SessionState::Live) {
                SetNotice(NoticeLevel::Warning, "記録を止めてから保存してください。");
            }
            return false;
        }
        // 隣の .tmp へ書いて閉じてから、本来の名前へ付け替える（失敗したら .tmp を消す）
        const std::string tempPath = path + ".tmp";
        std::unique_ptr<tracy::FileWrite> file(tracy::FileWrite::Open(tempPath.c_str(), tracy::FileCompression::Zstd, 3, 4));
        if (!file) {
            SetNotice(NoticeLevel::Error, "保存するファイルを作れませんでした: " + path);
            return false;
        }

        saving_ = true;
        saveDone_.store(false, std::memory_order_relaxed);
        savePath_ = path;
        tracy::Worker* worker = worker_.get();
        saveThread_ = std::thread([this, worker, file = std::move(file), tempPath, path]() mutable {
            {
                auto lock = worker->ObtainLockForMainThread();
                worker->Write(*file, false);
            }
            file.reset();

            const std::filesystem::path from(Text::ToWide(tempPath));
            std::error_code error;
            std::filesystem::rename(from, std::filesystem::path(Text::ToWide(path)), error);
            if (error) {
                std::error_code ignored;
                std::filesystem::remove(from, ignored);
            }
            saveSucceeded_ = !error;
            saveDone_.store(true, std::memory_order_release);
        });
        return true;
    }

    void CaptureSession::FinishSave()
    {
        saveThread_.join();
        saving_ = false;
        if (saveSucceeded_) {
            unsaved_ = false;
            if (state_ == SessionState::File) {
                filePath_ = savePath_;
            }
            SetNotice(NoticeLevel::Info, "保存しました: " + savePath_);
        } else {
            SetNotice(NoticeLevel::Error, "保存できませんでした: " + savePath_);
        }
    }

    void CaptureSession::Update()
    {
        if (loading_ && loadDone_.load(std::memory_order_acquire)) {
            FinishLoad();
        }
        if (saving_ && saveDone_.load(std::memory_order_acquire)) {
            FinishSave();
        }
        if (!worker_ || IsBusy()) {
            return;
        }

        if (state_ == SessionState::Connecting) {
            switch (static_cast<tracy::HandshakeStatus>(worker_->GetHandshakeStatus())) {
            case tracy::HandshakeProtocolMismatch:
                ReleaseWorker();
                SetNotice(NoticeLevel::Error, std::format("相手の Tracy の版が違うのでつなげません（こちらは {}.{}.{}）。",
                    tracy::Version::Major, tracy::Version::Minor, tracy::Version::Patch));
                return;
            case tracy::HandshakeNotAvailable:
                ReleaseWorker();
                SetNotice(NoticeLevel::Error, "相手はもうほかの画面とつながっています。元の Tracy などを閉じてから、つなぎ直してください。");
                return;
            case tracy::HandshakeDropped:
                ReleaseWorker();
                SetNotice(NoticeLevel::Error, "つなぐ途中で相手が切れました。");
                return;
            default:
                break;
            }
            if (worker_->HasData()) {
                state_ = SessionState::Live;
                unsaved_ = true;
            }
        }

        if (state_ == SessionState::Live && !worker_->IsConnected()) {
            state_ = SessionState::Stopped;
            if (!stopRequested_) {
                SetNotice(NoticeLevel::Info, "相手が終わったので受け取りを止めました。受け取った分は残っています。");
            }
        }

        if (state_ == SessionState::Live || state_ == SessionState::Stopped) {
            auto lock = worker_->ObtainLockForMainThread();
            const tracy::Worker::Failure failure = worker_->GetFailureType();
            if (failure != tracy::Worker::Failure::None) {
                SetNotice(NoticeLevel::Warning, std::string("受け取った記録に食い違いがありました（")
                    + tracy::Worker::GetFailureString(failure) + "）。");
                worker_->ClearFailure();
            }
        }
    }

    bool CaptureSession::ReadSummary(SessionSummary& out)
    {
        if (!worker_ || !worker_->HasData()) {
            return false;
        }
        return Read([&out](const tracy::Worker& worker) {
            out.program = worker.GetCaptureProgram();
            out.captureName = worker.GetCaptureName();
            out.pid = worker.GetPid();
            const tracy::FrameData* frames = worker.GetFramesBase();
            out.frameCount = frames ? worker.GetFrameCount(*frames) : 0;
            out.span = worker.GetLastTime() - worker.GetFirstTime();
            out.zoneCount = worker.GetZoneCount();
        });
    }
}
