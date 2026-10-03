#pragma once
#include <functional>
#include <string>
#include <utility>

namespace CoreEngine
{
    /// @brief ステップを 1 回呼んだ結果
    enum class StartupTaskResult {
        Done,    ///< 済んだ（次のステップへ進む）
        Pending, ///< 待っている（次の Step でもう一度呼ばれ、そのあいだ列は進まない）
    };

    /// @brief 起動シーケンスを構成する 1 ステップ。
    /// @details 1 ステップの実行時間がそのまま「スプラッシュが固まる時間」になるので、
    ///          目安として 1 秒以内に収まる粒度で切ること。
    ///          それより長くなる処理（シェーダ 119 本のコンパイルなど）は、
    ///          内側から StartupProgress::Tick を呼んで刻む。
    class IStartupTask {
    public:
        virtual ~IStartupTask() = default;

        /// @brief スプラッシュとログに出す表示名
        /// @note 実行直前に問い合わせるので、前のステップの結果で決まる名前も返せる
        ///       （サブシステム名など）。呼び出し回数は 1 ステップにつき数回。
        virtual std::string GetLabel() const = 0;

        /// @brief ステップ本体
        /// @return Pending を返すと、次の Step でもう一度呼ばれる（ワーカーの完了待ち・数フレームに分ける処理）
        virtual StartupTaskResult Execute() = 0;

        /// @brief Pending のあいだの進み具合（0.0〜1.0。ローディング画面のゲージに使う）
        virtual float GetProgress() const { return 0.0f; }
    };

    /// @brief ラムダを IStartupTask として扱うアダプタ
    class FunctionStartupTask final : public IStartupTask {
    public:
        FunctionStartupTask(std::string label, std::function<void()> action)
            : label_(std::move(label))
            , action_(std::move(action))
        {
        }

        /// @brief 表示名を実行直前に生成する版
        FunctionStartupTask(std::function<std::string()> labelProvider, std::function<void()> action)
            : labelProvider_(std::move(labelProvider))
            , action_(std::move(action))
        {
        }

        std::string GetLabel() const override
        {
            return labelProvider_ ? labelProvider_() : label_;
        }

        StartupTaskResult Execute() override
        {
            if (action_) {
                action_();
            }
            return StartupTaskResult::Done;
        }

    private:
        std::string label_;
        std::function<std::string()> labelProvider_;
        std::function<void()> action_;
    };

    /// @brief 始める処理と、1 回分進める処理で表すステップ（読み込みの待ちなど）
    /// @details 最初の Execute で start を呼ぶ。start が true（待つものがある）を返したら、
    ///          次の Execute からは advance が true を返すまで Pending を返す。
    class PollingStartupTask final : public IStartupTask {
    public:
        /// @param start    始める処理。待つものがあれば true
        /// @param advance  1 回分進める処理。済んだら true
        /// @param progress 待っているあいだの進み具合（0.0〜1.0。省略可）
        PollingStartupTask(std::string label, std::function<bool()> start,
                           std::function<bool()> advance, std::function<float()> progress = {})
            : label_(std::move(label))
            , start_(std::move(start))
            , advance_(std::move(advance))
            , progress_(std::move(progress))
        {
        }

        std::string GetLabel() const override { return label_; }

        StartupTaskResult Execute() override
        {
            if (!started_) {
                started_ = true;
                waiting_ = start_ && start_();
                return waiting_ ? StartupTaskResult::Pending : StartupTaskResult::Done;
            }
            if (waiting_ && advance_ && !advance_()) {
                return StartupTaskResult::Pending;
            }
            waiting_ = false;
            return StartupTaskResult::Done;
        }

        float GetProgress() const override
        {
            return (waiting_ && progress_) ? progress_() : 0.0f;
        }

    private:
        std::string label_;
        std::function<bool()> start_;
        std::function<bool()> advance_;
        std::function<float()> progress_;
        bool started_ = false;
        bool waiting_ = false;
    };
}
