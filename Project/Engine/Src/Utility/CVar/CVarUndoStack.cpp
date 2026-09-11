#include "pch.h"
#include "CVarUndoStack.h"
#include "CVar.h"
#include "CVarRegistry.h"
#include "Editor/Command/EditorCommandStack.h"

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

        /// @brief 値を戻してから即時保存させる
        /// @note Undo / Redo は「確定」操作なので、デバウンスを待たない
        void ApplyValue(ICVar* cvar, const std::array<unsigned char, 16>& value)
        {
            cvar->SetFromPointer(value.data());
            CVarRegistry::Get().NotifyCommit();
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

        std::array<unsigned char, kValueSize> oldValue{};
        std::array<unsigned char, kValueSize> newValue{};
        std::memcpy(oldValue.data(), pendingOldValue_, kValueSize);
        CopyValue(cvar, newValue.data());
        pendingCVar_ = nullptr;

        // 往復して元に戻った編集（ドラッグして元の位置で離した等）は積まない
        if (ValuesEqual(cvar, oldValue.data(), newValue.data())) {
            return;
        }

        Editor::EditorCommandStack::Get().Push(
            std::make_unique<Editor::FunctionCommand>(
                cvar->GetName(),
                [cvar, oldValue] { ApplyValue(cvar, oldValue); },
                [cvar, newValue] { ApplyValue(cvar, newValue); }));
    }

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
}
