#include "pch.h"
#include "Script/Binding/ScriptInputBinding.h"

#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Input/InputQuery.h"
#include "Script/Binding/BindingRegistrar.h"
#include "UI/UIPointer.h"

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
            if (!sInput || action < 0 || action >= static_cast<int>(InputActions::Count())) {
                return nullptr;
            }
            return &sInput->GetQuery();
        }

        bool IsActionPressed(int action, int player)
        {
            const InputQuery* const query = QueryFor(action);
            return query && query->IsActionPressed(static_cast<InputAction>(action), player);
        }

        bool IsActionTriggered(int action, int player)
        {
            const InputQuery* const query = QueryFor(action);
            return query && query->IsActionTriggered(static_cast<InputAction>(action), player);
        }

        bool IsActionReleased(int action, int player)
        {
            const InputQuery* const query = QueryFor(action);
            return query && query->IsActionReleased(static_cast<InputAction>(action), player);
        }

        float GetAxisValue(int action, int player)
        {
            const InputQuery* const query = QueryFor(action);
            return query ? query->GetAxisValue(static_cast<InputAction>(action), player) : 0.0f;
        }

        /// @brief 2 つの操作の差（-1〜1）。左右・前後をひとまとめに取る
        float GetAxis(int negative, int positive, int player)
        {
            const InputQuery* const query = QueryFor(negative);
            if (!query || !QueryFor(positive)) {
                return 0.0f;
            }
            return query->GetAxis(static_cast<InputAction>(negative),
                                  static_cast<InputAction>(positive), player);
        }

        /// @brief 4 つの操作から 2 次元の入力を作る（斜めは丸める）
        Vector2 GetAxis2D(int negativeX, int positiveX, int negativeY, int positiveY, int player)
        {
            const InputQuery* const query = QueryFor(negativeX);
            if (!query || !QueryFor(positiveX) || !QueryFor(negativeY) || !QueryFor(positiveY)) {
                return Vector2{ 0.0f, 0.0f };
            }
            return query->GetAxis2D(static_cast<InputAction>(negativeX),
                                    static_cast<InputAction>(positiveX),
                                    static_cast<InputAction>(negativeY),
                                    static_cast<InputAction>(positiveY), player);
        }

        /// @brief マウスのボタン番号を確かめる
        const InputQuery* QueryForMouse(int button)
        {
            if (!sInput || button < 0 || button > static_cast<int>(MouseButton::XButton2)) {
                return nullptr;
            }
            return &sInput->GetQuery();
        }

        bool IsMouseButtonPressed(int button)
        {
            const InputQuery* const query = QueryForMouse(button);
            return query && query->IsMouseButtonPressed(static_cast<MouseButton>(button));
        }

        bool IsMouseButtonTriggered(int button)
        {
            const InputQuery* const query = QueryForMouse(button);
            return query && query->IsMouseButtonTriggered(static_cast<MouseButton>(button));
        }

        bool IsMouseButtonReleased(int button)
        {
            const InputQuery* const query = QueryForMouse(button);
            return query && query->IsMouseButtonReleased(static_cast<MouseButton>(button));
        }

        /// @brief 前のフレームからのマウスの移動量［px］
        Vector2 GetMouseDelta()
        {
            if (!sInput) {
                return Vector2{ 0.0f, 0.0f };
            }
            const InputQuery& query = sInput->GetQuery();
            return Vector2{ static_cast<float>(query.GetMouseDragX()),
                            static_cast<float>(query.GetMouseDragY()) };
        }

        /// @brief ホイールの回転（1 ノッチ = 1.0）
        float GetWheelDelta()
        {
            constexpr float kPerNotch = 120.0f;
            return sInput ? static_cast<float>(sInput->GetQuery().GetWheelDelta()) / kPerNotch : 0.0f;
        }

        /// @brief ポインタがゲーム画面の上にあるか
        /// @details エディタでは、Game ビューの外にあるマウスで視点を回さないために使う。
        bool IsPointerOverGame()
        {
            return UIPointer::Get().IsOver();
        }

        void SetVibration(float leftMotorRatio, float rightMotorRatio, int player)
        {
            if (sInput) {
                sInput->GetQuery().SetVibration(leftMotorRatio, rightMotorRatio, player);
            }
        }

        bool IsGamepadConnected(int player)
        {
            return sInput && sInput->GetQuery().IsGamepadConnected(player);
        }

        /// @brief 何台つながっているか（何人で遊べるかの判断に使う）
        int GetConnectedGamepadCount()
        {
            return sInput ? sInput->GetQuery().GetConnectedGamepadCount() : 0;
        }

        Vector2 GetLeftStick(int player)
        {
            if (!sInput) {
                return Vector2{ 0.0f, 0.0f };
            }
            const Stick stick = sInput->GetQuery().GetLeftStick(player);
            return Vector2{ stick.x, stick.y };
        }

        Vector2 GetRightStick(int player)
        {
            if (!sInput) {
                return Vector2{ 0.0f, 0.0f };
            }
            const Stick stick = sInput->GetQuery().GetRightStick(player);
            return Vector2{ stick.x, stick.y };
        }

        float GetLeftTrigger(int player)
        {
            return sInput ? sInput->GetQuery().GetLeftTrigger(player) : 0.0f;
        }

        float GetRightTrigger(int player)
        {
            return sInput ? sInput->GetQuery().GetRightTrigger(player) : 0.0f;
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
        for (int action = 0; action < static_cast<int>(InputActions::Count()); ++action) {
            const std::string name(InputActionToString(static_cast<InputAction>(action)));
            r.EnumValue("InputAction", name.c_str(), action);
        }

        r.Enum("Key");
        for (const KeyEntry& key : kKeys) {
            r.EnumValue("Key", key.name, key.code);
        }

        r.Enum("MouseButton");
        r.EnumValue("MouseButton", "Left", static_cast<int>(MouseButton::Left));
        r.EnumValue("MouseButton", "Right", static_cast<int>(MouseButton::Right));
        r.EnumValue("MouseButton", "Middle", static_cast<int>(MouseButton::Middle));
        r.EnumValue("MouseButton", "XButton1", static_cast<int>(MouseButton::XButton1));
        r.EnumValue("MouseButton", "XButton2", static_cast<int>(MouseButton::XButton2));

        r.Namespace("Input");
        // player を省くとキーボードと全パッドを見る。2 人目以降は 0〜3 を渡して分ける
        r.Function("bool IsActionPressed(InputAction, int player = -1)", asFUNCTION(IsActionPressed));
        r.Function("bool IsActionTriggered(InputAction, int player = -1)", asFUNCTION(IsActionTriggered));
        r.Function("bool IsActionReleased(InputAction, int player = -1)", asFUNCTION(IsActionReleased));
        r.Function("float GetAxisValue(InputAction, int player = -1)", asFUNCTION(GetAxisValue));
        r.Function("float GetAxis(InputAction negative, InputAction positive, int player = -1)",
            asFUNCTION(GetAxis));
        r.Function("Vector2 GetAxis2D(InputAction negativeX, InputAction positiveX, "
            "InputAction negativeY, InputAction positiveY, int player = -1)", asFUNCTION(GetAxis2D));
        r.Function("bool IsGamepadConnected(int player = 0)", asFUNCTION(IsGamepadConnected));
        r.Function("int GetConnectedGamepadCount()", asFUNCTION(GetConnectedGamepadCount));
        r.Function("Vector2 GetLeftStick(int player = 0)", asFUNCTION(GetLeftStick));
        r.Function("Vector2 GetRightStick(int player = 0)", asFUNCTION(GetRightStick));
        r.Function("float GetLeftTrigger(int player = 0)", asFUNCTION(GetLeftTrigger));
        r.Function("float GetRightTrigger(int player = 0)", asFUNCTION(GetRightTrigger));
        r.Function("void SetVibration(float left, float right, int player = 0)", asFUNCTION(SetVibration));
        r.Function("bool IsMouseButtonPressed(MouseButton)", asFUNCTION(IsMouseButtonPressed));
        r.Function("bool IsMouseButtonTriggered(MouseButton)", asFUNCTION(IsMouseButtonTriggered));
        r.Function("bool IsMouseButtonReleased(MouseButton)", asFUNCTION(IsMouseButtonReleased));
        r.Function("Vector2 GetMouseDelta()", asFUNCTION(GetMouseDelta));
        r.Function("float GetWheelDelta()", asFUNCTION(GetWheelDelta));
        r.Function("bool IsPointerOverGame()", asFUNCTION(IsPointerOverGame));
        r.Function("bool IsKeyPressed(Key)", asFUNCTION(IsKeyPressed));
        r.Function("bool IsKeyTriggered(Key)", asFUNCTION(IsKeyTriggered));
        r.Function("bool IsKeyReleased(Key)", asFUNCTION(IsKeyReleased));
        r.Namespace("");
        return r.Succeeded();
    }
}
