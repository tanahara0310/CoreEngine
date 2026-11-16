#include "pch.h"
#include "CVarUndoStack.h"
#include "CVar.h"
#include "CVarRegistry.h"

#ifdef CORE_EDITOR
#include "Editor/Command/EditorCommandStack.h"
#endif

#include <array>
#include <cstring>
#include <memory>

namespace CoreEngine
{
    namespace
    {
        /// @brief 型ごとの値サイズ [byte]
        size_t ValueSizeOf(CVarType type)
        {
            return VisitCVarType(type, [](auto* tag) { return sizeof(*tag); });
        }

#ifdef CORE_EDITOR
        /// @brief 値を戻してから即時保存させる
        /// @note Undo / Redo は「確定」操作なので、デバウンスを待たない
        void ApplyValue(ICVar* cvar, const std::array<unsigned char, 16>& value)
        {
            cvar->SetFromPointer(value.data());
            CVarRegistry::Get().NotifyCommit();
        }
#endif
    }

    CVarUndoStack& CVarUndoStack::Get()
    {
        static CVarUndoStack instance;
        return instance;
    }

    void CVarUndoStack::CopyValue(const ICVar* cvar, unsigned char* out)
    {
        std::memset(out, 0, kValueSize);
        VisitCVarType(cvar->GetType(), [cvar, out](auto* tag) {
            using T = std::remove_pointer_t<decltype(tag)>;
            static_assert(sizeof(T) <= kValueSize);
            if (const CVar<T>* const typed = cvar->As<T>()) {
                std::memcpy(out, &typed->Get(), sizeof(T));
            }
        });
    }

    bool CVarUndoStack::ValuesEqual(const ICVar* cvar,
                                    const unsigned char* a, const unsigned char* b)
    {
        // バッファは常にゼロ初期化してからコピーしているため memcmp でよい
        return std::memcmp(a, b, ValueSizeOf(cvar->GetType())) == 0;
    }

    void CVarUndoStack::BeginEdit(ICVar* cvar)
    {
        if (!cvar) {
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

        std::array<unsigned char, kValueSize> oldValue{};
        std::array<unsigned char, kValueSize> newValue{};
        std::memcpy(oldValue.data(), pendingOldValue_, kValueSize);
        CopyValue(cvar, newValue.data());
        pendingCVar_ = nullptr;

        // 往復して元に戻った編集（ドラッグして元の位置で離した等）は積まない
        if (ValuesEqual(cvar, oldValue.data(), newValue.data())) {
            return;
        }

#ifdef CORE_EDITOR
        // CVar は自分の JSON へ保存されるので、シーンの未保存には数えない
        Editor::EditorCommandStack::Get().Push(
            std::make_unique<Editor::FunctionCommand>(
                cvar->GetName(),
                [cvar, oldValue] { ApplyValue(cvar, oldValue); },
                [cvar, newValue] { ApplyValue(cvar, newValue); },
                false, true));
#endif
    }

    void AssignWithUndoFromPointer(ICVar& cvar, const void* value)
    {
        CVarUndoStack& undoStack = CVarUndoStack::Get();
        undoStack.BeginEdit(&cvar);
        cvar.SetFromPointer(value);
        undoStack.CommitEdit(&cvar);
        CVarRegistry::Get().NotifyCommit();
    }

#ifdef CORE_EDITOR
    void CVarUndoStack::BeginBatch()
    {
        Editor::EditorCommandStack::Get().BeginBatch("CVar の一括変更");
    }

    void CVarUndoStack::EndBatch()
    {
        Editor::EditorCommandStack::Get().EndBatch();
    }

    bool CVarUndoStack::CanUndo() const noexcept
    {
        return Editor::EditorCommandStack::Get().CanUndo();
    }

    bool CVarUndoStack::CanRedo() const noexcept
    {
        return Editor::EditorCommandStack::Get().CanRedo();
    }

    void CVarUndoStack::Undo()
    {
        Editor::EditorCommandStack::Get().Undo();
    }

    void CVarUndoStack::Redo()
    {
        Editor::EditorCommandStack::Get().Redo();
    }
#else
    void CVarUndoStack::BeginBatch() {}
    void CVarUndoStack::EndBatch() {}
    bool CVarUndoStack::CanUndo() const noexcept { return false; }
    bool CVarUndoStack::CanRedo() const noexcept { return false; }
    void CVarUndoStack::Undo() {}
    void CVarUndoStack::Redo() {}
#endif
}
