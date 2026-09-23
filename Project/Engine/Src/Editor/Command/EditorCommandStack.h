#pragma once

#include "Editor/Command/EditorCommand.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief エディタ全体で 1 本の Undo / Redo 履歴
    /// @details オブジェクトの移動・CVar の編集・カメラのキー操作など、
    ///          種類を問わず同じスタックへ積む。Ctrl+Z はここだけを見る。
    class EditorCommandStack
    {
    public:
        /// @brief 保持する最大操作数
        static constexpr size_t kMaxCommands = 128;

        static EditorCommandStack& Get();

        /// @brief 操作を積む（Redo 履歴は捨てられる）
        /// @note バッチ中は EndBatch までまとめて 1 操作として扱われる
        void Push(std::unique_ptr<IEditorCommand> command);

        /// @brief 直前の操作を取り消す
        /// @return 取り消すものがあれば true
        bool Undo();

        /// @brief 取り消した操作をやり直す
        /// @return やり直すものがあれば true
        bool Redo();

        bool CanUndo() const noexcept { return !undo_.empty(); }
        bool CanRedo() const noexcept { return !redo_.empty(); }

        size_t GetUndoCount() const noexcept { return undo_.size(); }
        size_t GetRedoCount() const noexcept { return redo_.size(); }

        /// @brief シーンの変更の通し番号（シーンを変える操作のたびに 1 つ増える）
        /// @note 「最後に保存したときの番号」と比べて、未保存の変更があるかを判定する。
        ///       CVar のように自分の保存先を持つ操作は数えない。
        uint64_t GetSceneRevision() const noexcept { return sceneRevision_; }

        /// @brief 次に取り消される操作の名前（無ければ空）
        std::string PeekUndoLabel() const;

        /// @brief 次にやり直される操作の名前（無ければ空）
        std::string PeekRedoLabel() const;

        /// @brief 履歴をすべて捨てる（シーン切り替えなど、対象が消える場面で呼ぶ）
        /// @note 再生中は再生中の履歴だけを捨て、再生前の履歴は残す。
        void Clear();

        /// @brief 指定した実体を参照している操作を履歴から取り除く
        /// @param target 破棄される実体（コンポーネントなど）
        /// @note 生ポインタで対象を握るコマンドが解放済みメモリを触らないようにする
        void RemoveCommandsReferencing(const void* target);

        /// @brief 取り出した履歴
        struct History
        {
            std::vector<std::unique_ptr<IEditorCommand>> undo;
            std::vector<std::unique_ptr<IEditorCommand>> redo;
        };

        /// @brief 再生中の履歴を始める（それまでの履歴は脇へ置く）
        void BeginPlaySession();

        /// @brief 再生中の履歴を終え、残す履歴を取り出す（スタックは空になる）
        /// @return 再生前の履歴に、再生中の操作のうちシーンを変えないものを続けたもの。
        ///         どちらもシーンを組み直した後も使える操作だけを残す。
        History EndPlaySession();

        /// @brief 取り出した履歴を戻す（その間に積まれた操作は後ろへ続ける）
        void RestoreHistory(History history);

        /// @brief 一括操作の開始（EndBatch までに積んだ分が 1 回の Undo になる）
        void BeginBatch(std::string label);

        /// @brief 一括操作の終了
        void EndBatch();

        /// @brief Undo / Redo の適用中か
        /// @note 適用によって走る編集処理が、それ自体を履歴へ積み直さないようにするためのフラグ
        bool IsApplying() const noexcept { return applying_; }

    private:
        EditorCommandStack() = default;
        ~EditorCommandStack() = default;
        EditorCommandStack(const EditorCommandStack&) = delete;
        EditorCommandStack& operator=(const EditorCommandStack&) = delete;

        void PushToUndo(std::unique_ptr<IEditorCommand> command);

        /// @brief 取り消せる数の上限を超えた古い操作を捨てる
        void TrimUndo();

        std::vector<std::unique_ptr<IEditorCommand>> undo_;
        std::vector<std::unique_ptr<IEditorCommand>> redo_;

        /// 再生中に脇へ置いている再生前の履歴
        std::vector<std::unique_ptr<IEditorCommand>> pausedUndo_;
        std::vector<std::unique_ptr<IEditorCommand>> pausedRedo_;
        bool inPlaySession_ = false;

        /// シーンの変更の通し番号
        uint64_t sceneRevision_ = 0;

        std::unique_ptr<CompositeCommand> batch_;
        int  batchDepth_ = 0;
        bool applying_ = false;
    };

    /// @brief スコープを抜けるまでを 1 回の Undo にまとめる
    class BatchScope
    {
    public:
        explicit BatchScope(std::string label)
        {
            EditorCommandStack::Get().BeginBatch(std::move(label));
        }
        ~BatchScope() { EditorCommandStack::Get().EndBatch(); }

        BatchScope(const BatchScope&) = delete;
        BatchScope& operator=(const BatchScope&) = delete;
    };
}
