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

        /// @brief スクリプトへ出すキーの名前と DirectInput の番号
        /// @details 割り当てを決めた操作（InputAction）で足りる場面はそちらを使う。
        ///          こちらは「このキーを押したらこれ」を試すときの入口。
        struct KeyEntry {
            const char* name;
            int code;
        };

        constexpr KeyEntry kKeys[] = {
            { "Num0", DIK_0 }, { "Num1", DIK_1 }, { "Num2", DIK_2 }, { "Num3", DIK_3 },
            { "Num4", DIK_4 }, { "Num5", DIK_5 }, { "Num6", DIK_6 }, { "Num7", DIK_7 },
            { "Num8", DIK_8 }, { "Num9", DIK_9 },
            { "A", DIK_A }, { "B", DIK_B }, { "C", DIK_C }, { "D", DIK_D }, { "E", DIK_E },
            { "F", DIK_F }, { "G", DIK_G }, { "H", DIK_H }, { "I", DIK_I }, { "J", DIK_J },
            { "K", DIK_K }, { "L", DIK_L }, { "M", DIK_M }, { "N", DIK_N }, { "O", DIK_O },
            { "P", DIK_P }, { "Q", DIK_Q }, { "R", DIK_R }, { "S", DIK_S }, { "T", DIK_T },
            { "U", DIK_U }, { "V", DIK_V }, { "W", DIK_W }, { "X", DIK_X }, { "Y", DIK_Y },
            { "Z", DIK_Z },
            { "Space", DIK_SPACE }, { "Enter", DIK_RETURN }, { "Escape", DIK_ESCAPE },
            { "Tab", DIK_TAB }, { "Shift", DIK_LSHIFT }, { "Ctrl", DIK_LCONTROL },
            { "Left", DIK_LEFT }, { "Right", DIK_RIGHT }, { "Up", DIK_UP }, { "Down", DIK_DOWN },
        };

        /// @brief キーの番号を確かめてから問い合わせ先を返す
        const InputQuery* QueryForKey(int key)
        {
            if (!sInput || key < 0 || key > 0xFF) {
                return nullptr;
            }
            return &sInput->GetQuery();
        }

        bool IsKeyPressed(int key)
        {
            const InputQuery* const query = QueryForKey(key);
            return query && query->IsKeyPressed(static_cast<uint8_t>(key));
        }

        bool IsKeyTriggered(int key)
        {
            const InputQuery* const query = QueryForKey(key);
            return query && query->IsKeyTriggered(static_cast<uint8_t>(key));
        }

        bool IsKeyReleased(int key)
        {
            const InputQuery* const query = QueryForKey(key);
            return query && query->IsKeyReleased(static_cast<uint8_t>(key));
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

        r.Enum("Key");
        for (const KeyEntry& key : kKeys) {
            r.EnumValue("Key", key.name, key.code);
        }

        r.Namespace("Input");
        r.Function("bool IsActionPressed(InputAction)", asFUNCTION(IsActionPressed));
        r.Function("bool IsActionTriggered(InputAction)", asFUNCTION(IsActionTriggered));
        r.Function("bool IsActionReleased(InputAction)", asFUNCTION(IsActionReleased));
        r.Function("float GetAxisValue(InputAction)", asFUNCTION(GetAxisValue));
        r.Function("bool IsGamepadConnected()", asFUNCTION(IsGamepadConnected));
        r.Function("bool IsKeyPressed(Key)", asFUNCTION(IsKeyPressed));
        r.Function("bool IsKeyTriggered(Key)", asFUNCTION(IsKeyTriggered));
        r.Function("bool IsKeyReleased(Key)", asFUNCTION(IsKeyReleased));
        r.Namespace("");
        return r.Succeeded();
    }
}
