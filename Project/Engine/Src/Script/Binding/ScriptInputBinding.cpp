#include "pch.h"
#include "Script/Binding/ScriptInputBinding.h"

#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Script/Binding/BindingRegistrar.h"

#include <string>

namespace CoreEngine::Script
{
    namespace
    {
        /// 問い合わせ先の入力（登録時に受け取る）
        InputManager* sInput = nullptr;

        /// @brief 問い合わせ先と、スクリプトから渡されたアクションの番号を確かめる
        /// @return 問い合わせてよければ問い合わせ先
        const InputQuery* QueryFor(int action)
        {
            if (!sInput || action < 0 || action >= static_cast<int>(InputAction::Count)) {
                return nullptr;
            }
            return &sInput->GetQuery();
        }

        bool IsActionPressed(int action)
        {
            const InputQuery* const query = QueryFor(action);
            return query && query->IsActionPressed(static_cast<InputAction>(action));
        }

        bool IsActionTriggered(int action)
        {
            const InputQuery* const query = QueryFor(action);
            return query && query->IsActionTriggered(static_cast<InputAction>(action));
        }

        bool IsActionReleased(int action)
        {
            const InputQuery* const query = QueryFor(action);
            return query && query->IsActionReleased(static_cast<InputAction>(action));
        }

        float GetAxisValue(int action)
        {
            const InputQuery* const query = QueryFor(action);
            return query ? query->GetAxisValue(static_cast<InputAction>(action)) : 0.0f;
        }

        bool IsGamepadConnected()
        {
            return sInput && sInput->GetQuery().IsGamepadConnected();
        }
    }

    bool RegisterInputBinding(asIScriptEngine* engine, InputManager* input)
    {
        if (!engine) {
            return false;
        }
        sInput = input;

        BindingRegistrar r(engine);
        r.Enum("InputAction");
        for (int action = 0; action < static_cast<int>(InputAction::Count); ++action) {
            const std::string name(InputActionToString(static_cast<InputAction>(action)));
            r.EnumValue("InputAction", name.c_str(), action);
        }

        r.Namespace("Input");
        r.Function("bool IsActionPressed(InputAction)", asFUNCTION(IsActionPressed));
        r.Function("bool IsActionTriggered(InputAction)", asFUNCTION(IsActionTriggered));
        r.Function("bool IsActionReleased(InputAction)", asFUNCTION(IsActionReleased));
        r.Function("float GetAxisValue(InputAction)", asFUNCTION(GetAxisValue));
        r.Function("bool IsGamepadConnected()", asFUNCTION(IsGamepadConnected));
        r.Namespace("");
        return r.Succeeded();
    }
}
