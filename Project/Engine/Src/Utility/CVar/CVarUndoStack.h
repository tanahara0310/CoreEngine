#pragma once

#include <cstdint>
#include <vector>

/// @file
/// @brief CVar 編集の Undo / Redo スタック

namespace CoreEngine
{
    class ICVar;

    /// @brief CVar 編集の Undo / Redo（Ctrl+Z / Ctrl+Y）
    /// @details 商用エンジンの「調整した瞬間に保存」は Undo とセットで初めて安全になる
    class CVarUndoStack
    {
    public:
        static CVarUndoStack& Get();

        /// @brief 編集セッションを開始する（最初の変更の直前に呼ぶ）
        /// @details cvar の現在値を旧値として控える。既に同じ CVar のセッション中なら何もしない
        void BeginEdit(ICVar* cvar);

        /// @brief 編集セッションを確定する（値が実際に変わっていればレコードを積む）
        void CommitEdit(ICVar* cvar);

        /// @brief 一括操作（ResetTree 等）の開始。EndBatch までの記録が 1 回の Undo になる
        void BeginBatch();
        void EndBatch();

        bool CanUndo() const noexcept { return !undo_.empty(); }
        bool CanRedo() const noexcept { return !redo_.empty(); }

        /// @brief 直前の編集（バッチなら一括で）を取り消す。適用は確定扱い＝即時保存される
        void Undo();

        /// @brief Undo した編集をやり直す
        void Redo();

    private:
        CVarUndoStack() = default;

        /// 値は最大 16 バイト（Vector4）の固定バッファへコピーする（json 等への依存を避ける）
        static constexpr size_t kValueSize = 16;

        /// @brief Undo 1 件分（対象 CVar と変更前後の値）
        struct Record {
            ICVar*   cvar = nullptr;
            uint32_t batchId = 0;                    ///< 同じ ID のレコードはまとめて Undo/Redo される
            unsigned char oldValue[kValueSize]{};
            unsigned char newValue[kValueSize]{};
        };

        /// @brief cvar の現在値を out へコピーする
        static void CopyValue(const ICVar* cvar, unsigned char* out);
        static bool ValuesEqual(const ICVar* cvar,
                                const unsigned char* a, const unsigned char* b);

        void Push(Record&& record);
        /// @brief スタック末尾のバッチをもう片方のスタックへ移しながら値を適用する
        void ApplyTop(std::vector<Record>& from, std::vector<Record>& to, bool useOldValue);

        static constexpr size_t kMaxRecords = 64;

        std::vector<Record> undo_;
        std::vector<Record> redo_;

        // 編集セッションの控え（ImGui のアクティブ項目は同時に 1 つなので単一でよい）
        ICVar*        pendingCVar_ = nullptr;
        unsigned char pendingOldValue_[kValueSize]{};

        uint32_t nextBatchId_ = 1;
        int      batchDepth_ = 0;      ///< BeginBatch のネスト深度
        uint32_t currentBatchId_ = 0;  ///< バッチ中に発行する共通 ID（0 = バッチ外）
    };
}
