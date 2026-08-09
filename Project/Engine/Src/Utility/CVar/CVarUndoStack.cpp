#include "pch.h"
#include "CVarUndoStack.h"
#include "CVar.h"
#include "CVarRegistry.h"
#include <cstring>

namespace CoreEngine
{
    namespace
    {
        /// @brief 型ごとの値サイズ [byte]
        size_t ValueSizeOf(CVarType type)
        {
            switch (type)
            {
            case CVarType::Bool:    return sizeof(bool);
            case CVarType::Int:     return sizeof(int);
            case CVarType::Float:   return sizeof(float);
            case CVarType::Vector2: return sizeof(Vector2);
            case CVarType::Vector3: return sizeof(Vector3);
            case CVarType::Color:   return sizeof(Vector4);
            }
            return 0;
        }

        /// @brief 型消去された読み取り（const ポインタ）
        const void* ReadPointerOf(const ICVar* cvar)
        {
            switch (cvar->GetType())
            {
            case CVarType::Bool:    return cvar->AsBool();
            case CVarType::Int:     return cvar->AsInt();
            case CVarType::Float:   return cvar->AsFloat();
            case CVarType::Vector2: return cvar->AsVector2();
            case CVarType::Vector3: return cvar->AsVector3();
            case CVarType::Color:   return cvar->AsColor();
            }
            return nullptr;
        }
    }

    CVarUndoStack& CVarUndoStack::Get()
    {
        static CVarUndoStack instance;
        return instance;
    }

    void CVarUndoStack::CopyValue(const ICVar* cvar, unsigned char* out)
    {
        std::memset(out, 0, kValueSize);
        if (const void* p = ReadPointerOf(cvar)) {
            std::memcpy(out, p, ValueSizeOf(cvar->GetType()));
        }
    }

    bool CVarUndoStack::ValuesEqual(const ICVar* cvar,
                                    const unsigned char* a, const unsigned char* b)
    {
        // バッファは常にゼロ初期化してからコピーしているため memcmp でよい
        return std::memcmp(a, b, ValueSizeOf(cvar->GetType())) == 0;
    }

    void CVarUndoStack::BeginEdit(ICVar* cvar)
    {
        if (!cvar || HasFlag(cvar->GetFlags(), CVarFlags::Mirrored)) {
            return;
        }
        if (pendingCVar_ == cvar) {
            return;  // 同じ編集セッションの継続（ドラッグ中の毎フレーム呼び出し）
        }
        pendingCVar_ = cvar;
        CopyValue(cvar, pendingOldValue_);
    }

    void CVarUndoStack::CommitEdit(ICVar* cvar)
    {
        if (!cvar || pendingCVar_ != cvar) {
            return;
        }

        Record record;
        record.cvar = cvar;
        std::memcpy(record.oldValue, pendingOldValue_, kValueSize);
        CopyValue(cvar, record.newValue);
        pendingCVar_ = nullptr;

        // 往復して元に戻った編集（ドラッグして元の位置で離した等）は積まない
        if (ValuesEqual(cvar, record.oldValue, record.newValue)) {
            return;
        }
        Push(std::move(record));
    }

    void CVarUndoStack::BeginBatch()
    {
        if (batchDepth_++ == 0) {
            currentBatchId_ = nextBatchId_++;
        }
    }

    void CVarUndoStack::EndBatch()
    {
        if (batchDepth_ > 0 && --batchDepth_ == 0) {
            currentBatchId_ = 0;
        }
    }

    void CVarUndoStack::Push(Record&& record)
    {
        // バッチ中は共通 ID、単独編集は都度新しい ID（＝1 レコード 1 Undo）
        record.batchId = (currentBatchId_ != 0) ? currentBatchId_ : nextBatchId_++;

        undo_.push_back(record);
        redo_.clear();  // 新しい編集で Redo 履歴は無効になる

        if (undo_.size() > kMaxRecords) {
            undo_.erase(undo_.begin());
        }
    }

    void CVarUndoStack::ApplyTop(std::vector<Record>& from, std::vector<Record>& to, bool useOldValue)
    {
        if (from.empty()) {
            return;
        }

        // 末尾と同じ batchId のレコードをまとめて適用する（一括リセット等を 1 回で戻す）
        const uint32_t batchId = from.back().batchId;
        while (!from.empty() && from.back().batchId == batchId) {
            Record record = from.back();
            from.pop_back();
            record.cvar->SetFromPointer(useOldValue ? record.oldValue : record.newValue);
            to.push_back(std::move(record));
        }

        // Undo/Redo は「確定」操作なので、デバウンスを待たず即時保存させる
        CVarRegistry::Get().NotifyCommit();
    }

    void CVarUndoStack::Undo()
    {
        ApplyTop(undo_, redo_, /*useOldValue=*/true);
    }

    void CVarUndoStack::Redo()
    {
        ApplyTop(redo_, undo_, /*useOldValue=*/false);
    }
}
