#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief Undo / Redo できる 1 操作
    class IEditorCommand
    {
    public:
        virtual ~IEditorCommand() = default;

        /// @brief 操作を取り消す
        virtual void Undo() = 0;

        /// @brief 取り消した操作をやり直す
        virtual void Redo() = 0;

        /// @brief 履歴一覧に出す名前
        virtual std::string GetLabel() const = 0;

        /// @brief 指定した実体を参照しているか
        /// @param target 破棄されようとしている実体
        /// @note 生ポインタで対象を握るコマンドは true を返すこと。対象の破棄時に
        ///       履歴から取り除かれ、Undo が解放済みメモリを触らなくなる。
        virtual bool References(const void* target) const { (void)target; return false; }
    };

    /// @brief 「戻す」「やり直す」を関数 2 本で表すコマンド
    class FunctionCommand final : public IEditorCommand
    {
    public:
        FunctionCommand(std::string label, std::function<void()> undo, std::function<void()> redo)
            : label_(std::move(label)), undo_(std::move(undo)), redo_(std::move(redo)) {}

        void Undo() override { if (undo_) { undo_(); } }
        void Redo() override { if (redo_) { redo_(); } }
        std::string GetLabel() const override { return label_; }

    private:
        std::string label_;
        std::function<void()> undo_;
        std::function<void()> redo_;
    };

    /// @brief 状態まるごとのスナップショットで戻すコマンド
    /// @tparam TState 状態の型（コピー可能であること）
    /// @details 積む時点では編集後の状態がまだ確定していない編集（ドラッグ中の値など）を
    ///          扱うため、編集後は最初の Undo が呼ばれたときに控える。
    template <class TState>
    class SnapshotCommand final : public IEditorCommand
    {
    public:
        using Capture = std::function<TState()>;
        using Restore = std::function<void(const TState&)>;

        /// @param owner 省略可。状態を持つ実体。破棄時に履歴から外すための目印
        SnapshotCommand(std::string label, TState before, Capture capture, Restore restore,
                        const void* owner = nullptr)
            : label_(std::move(label)), before_(std::move(before)), owner_(owner),
              capture_(std::move(capture)), restore_(std::move(restore)) {}

        void Undo() override
        {
            if (!capture_ || !restore_) { return; }
            after_ = capture_();
            hasAfter_ = true;
            restore_(before_);
        }

        void Redo() override
        {
            if (!hasAfter_ || !restore_) { return; }
            restore_(after_);
        }

        std::string GetLabel() const override { return label_; }

        bool References(const void* target) const override
        {
            return target != nullptr && target == owner_;
        }

    private:
        std::string label_;
        TState  before_{};
        TState  after_{};
        const void* owner_ = nullptr;
        bool    hasAfter_ = false;
        Capture capture_;
        Restore restore_;
    };

    /// @brief 複数のコマンドを 1 回の Undo にまとめる
    class CompositeCommand final : public IEditorCommand
    {
    public:
        explicit CompositeCommand(std::string label) : label_(std::move(label)) {}

        void Add(std::unique_ptr<IEditorCommand> command)
        {
            if (command) { children_.push_back(std::move(command)); }
        }

        bool IsEmpty() const noexcept { return children_.empty(); }
        size_t GetCount() const noexcept { return children_.size(); }

        /// @brief 子が 1 つだけならそれを取り出す（まとめる意味が無いので包みを外す）
        std::unique_ptr<IEditorCommand> ExtractSingle()
        {
            if (children_.size() != 1) { return nullptr; }
            return std::move(children_.front());
        }

        void Undo() override
        {
            // 積んだ逆順で戻す
            for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
                (*it)->Undo();
            }
        }

        void Redo() override
        {
            for (auto& child : children_) {
                child->Redo();
            }
        }

        std::string GetLabel() const override { return label_; }

        bool References(const void* target) const override
        {
            for (const auto& child : children_) {
                if (child->References(target)) { return true; }
            }
            return false;
        }

    private:
        std::string label_;
        std::vector<std::unique_ptr<IEditorCommand>> children_;
    };
}
