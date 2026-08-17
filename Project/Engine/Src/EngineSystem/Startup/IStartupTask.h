#pragma once
#include <functional>
#include <string>
#include <utility>

namespace CoreEngine
{
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
        virtual void Execute() = 0;
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

        void Execute() override
        {
            if (action_) {
                action_();
            }
        }

    private:
        std::string label_;
        std::function<std::string()> labelProvider_;
        std::function<void()> action_;
    };
}
