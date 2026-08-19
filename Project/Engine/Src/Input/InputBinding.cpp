#include "pch.h"
#include "InputBinding.h"
#include <charconv>
#include <format>

namespace CoreEngine {

    namespace {

        // ─── キーボード名テーブル ─────────────────────────────────────
        struct KeyEntry { uint8_t code; const char* name; };
        static const KeyEntry kKeyTable[] = {
            { DIK_A, "A" }, { DIK_B, "B" }, { DIK_C, "C" }, { DIK_D, "D" },
            { DIK_E, "E" }, { DIK_F, "F" }, { DIK_G, "G" }, { DIK_H, "H" },
            { DIK_I, "I" }, { DIK_J, "J" }, { DIK_K, "K" }, { DIK_L, "L" },
            { DIK_M, "M" }, { DIK_N, "N" }, { DIK_O, "O" }, { DIK_P, "P" },
            { DIK_Q, "Q" }, { DIK_R, "R" }, { DIK_S, "S" }, { DIK_T, "T" },
            { DIK_U, "U" }, { DIK_V, "V" }, { DIK_W, "W" }, { DIK_X, "X" },
            { DIK_Y, "Y" }, { DIK_Z, "Z" },
            { DIK_1, "1" }, { DIK_2, "2" }, { DIK_3, "3" }, { DIK_4, "4" },
            { DIK_5, "5" }, { DIK_6, "6" }, { DIK_7, "7" }, { DIK_8, "8" },
            { DIK_9, "9" }, { DIK_0, "0" },
            { DIK_F1,  "F1"  }, { DIK_F2,  "F2"  }, { DIK_F3,  "F3"  }, { DIK_F4,  "F4"  },
            { DIK_F5,  "F5"  }, { DIK_F6,  "F6"  }, { DIK_F7,  "F7"  }, { DIK_F8,  "F8"  },
            { DIK_F9,  "F9"  }, { DIK_F10, "F10" }, { DIK_F11, "F11" }, { DIK_F12, "F12" },
            { DIK_SPACE,    "Space"     },
            { DIK_RETURN,   "Enter"     },
            { DIK_ESCAPE,   "Escape"    },
            { DIK_BACK,     "Backspace" },
            { DIK_TAB,      "Tab"       },
            { DIK_LSHIFT,   "LShift"    }, { DIK_RSHIFT,   "RShift"   },
            { DIK_LCONTROL, "LCtrl"     }, { DIK_RCONTROL, "RCtrl"    },
            { DIK_LALT,     "LAlt"      }, { DIK_RALT,     "RAlt"     },
            { DIK_UP,       "Up"        }, { DIK_DOWN,     "Down"     },
            { DIK_LEFT,     "Left"      }, { DIK_RIGHT,    "Right"    },
            { DIK_DELETE,   "Delete"    }, { DIK_INSERT,   "Insert"   },
            { DIK_HOME,     "Home"      }, { DIK_END,      "End"      },
            { DIK_PRIOR,    "PageUp"    }, { DIK_NEXT,     "PageDown" },
        };

        /// @brief DirectInput のキーコードを保存用の文字列へ
        const char* KeyCodeToName(uint8_t code) {
            for (const auto& e : kKeyTable) {
                if (e.code == code) return e.name;
            }
            return nullptr;
        }

        /// @brief 文字列から DirectInput のキーコードへ
        uint8_t KeyNameToCode(std::string_view name) {
            for (const auto& e : kKeyTable) {
                if (name == e.name) return e.code;
            }
            return 0;
        }

        // ─── マウスボタン名テーブル ────────────────────────────────────
        struct MouseButtonEntry { MouseButton btn; const char* name; };
        static const MouseButtonEntry kMouseButtonTable[] = {
            { MouseButton::Left,     "Left"     },
            { MouseButton::Right,    "Right"    },
            { MouseButton::Middle,   "Middle"   },
            { MouseButton::XButton1, "XButton1" },
            { MouseButton::XButton2, "XButton2" },
        };

        /// @brief マウスボタンを保存用の文字列へ
        const char* MouseButtonToName(MouseButton btn) {
            for (const auto& e : kMouseButtonTable) {
                if (e.btn == btn) return e.name;
            }
            return nullptr;
        }

        /// @brief 文字列からマウスボタンへ
        MouseButton MouseButtonFromName(std::string_view name) {
            for (const auto& e : kMouseButtonTable) {
                if (name == e.name) return e.btn;
            }
            return MouseButton::Left;
        }

        // ─── ゲームパッドボタン名テーブル ─────────────────────────────
        struct GamepadButtonEntry { GamepadButton btn; const char* name; };
        static const GamepadButtonEntry kGamepadButtonTable[] = {
            { GamepadButton::A,             "A"             },
            { GamepadButton::B,             "B"             },
            { GamepadButton::X,             "X"             },
            { GamepadButton::Y,             "Y"             },
            { GamepadButton::DPadUp,        "DPadUp"        },
            { GamepadButton::DPadDown,      "DPadDown"      },
            { GamepadButton::DPadLeft,      "DPadLeft"      },
            { GamepadButton::DPadRight,     "DPadRight"     },
            { GamepadButton::Start,         "Start"         },
            { GamepadButton::Back,          "Back"          },
            { GamepadButton::LeftThumb,     "LeftThumb"     },
            { GamepadButton::RightThumb,    "RightThumb"    },
            { GamepadButton::LeftShoulder,  "LeftShoulder"  },
            { GamepadButton::RightShoulder, "RightShoulder" },
        };

        /// @brief ゲームパッドボタンを保存用の文字列へ
        const char* GamepadButtonToName(GamepadButton btn) {
            for (const auto& e : kGamepadButtonTable) {
                if (e.btn == btn) return e.name;
            }
            return nullptr;
        }

        /// @brief 文字列からゲームパッドボタンへ
        GamepadButton GamepadButtonFromName(std::string_view name) {
            for (const auto& e : kGamepadButtonTable) {
                if (name == e.name) return e.btn;
            }
            return GamepadButton::A;
        }

        // ─── ゲームパッド軸名テーブル ──────────────────────────────────
        struct GamepadAxisEntry { GamepadAxis axis; const char* name; };
        static const GamepadAxisEntry kGamepadAxisTable[] = {
            { GamepadAxis::LeftStickX,  "LeftStickX"  },
            { GamepadAxis::LeftStickY,  "LeftStickY"  },
            { GamepadAxis::RightStickX, "RightStickX" },
            { GamepadAxis::RightStickY, "RightStickY" },
            { GamepadAxis::LeftTrigger, "LeftTrigger" },
            { GamepadAxis::RightTrigger,"RightTrigger"},
        };

        /// @brief ゲームパッド軸を保存用の文字列へ
        const char* GamepadAxisToName(GamepadAxis axis) {
            for (const auto& e : kGamepadAxisTable) {
                if (e.axis == axis) return e.name;
            }
            return nullptr;
        }

        /// @brief 文字列からゲームパッド軸へ
        GamepadAxis GamepadAxisFromName(std::string_view name) {
            for (const auto& e : kGamepadAxisTable) {
                if (name == e.name) return e.axis;
            }
            return GamepadAxis::LeftStickX;
        }

    } // namespace

    InputBinding InputBinding::FromKey(uint8_t dikCode) {
        InputBinding b;
        b.type = BindingType::Keyboard;
        b.code = dikCode;
        b.axisSign = 1.0f;
        return b;
    }

    InputBinding InputBinding::FromMouseButton(MouseButton button) {
        InputBinding b;
        b.type = BindingType::MouseButton;
        b.code = static_cast<uint16_t>(button);
        b.axisSign = 1.0f;
        return b;
    }

    InputBinding InputBinding::FromGamepadButton(GamepadButton button) {
        InputBinding b;
        b.type = BindingType::GamepadButton;
        b.code = static_cast<uint16_t>(button);
        b.axisSign = 1.0f;
        return b;
    }

    InputBinding InputBinding::FromGamepadAxis(GamepadAxis axis, bool positive) {
        InputBinding b;
        b.type = BindingType::GamepadAxis;
        b.code = static_cast<uint16_t>(axis);
        b.axisSign = positive ? 1.0f : -1.0f;
        return b;
    }

    std::string InputBinding::Serialize() const {
        switch (type) {
        case BindingType::Keyboard: {
            if (const char* name = KeyCodeToName(static_cast<uint8_t>(code))) {
                return std::format("Key:{}", name);
            }
            return std::format("Key:0x{:02X}", code);
        }
        case BindingType::MouseButton: {
            const char* name = MouseButtonToName(static_cast<MouseButton>(code));
            return std::format("Mouse:{}", name ? name : "Left");
        }
        case BindingType::GamepadButton: {
            const char* name = GamepadButtonToName(static_cast<GamepadButton>(code));
            return std::format("Gamepad:{}", name ? name : "A");
        }
        case BindingType::GamepadAxis: {
            const char* name = GamepadAxisToName(static_cast<GamepadAxis>(code));
            const char* sign = (axisSign >= 0.0f) ? "+" : "-";
            return std::format("Axis:{}{}", name ? name : "LeftStickX", sign);
        }
        }
        return "Key:A";
    }

    InputBinding InputBinding::Deserialize(const std::string& str) {
        const auto colonPos = str.find(':');
        if (colonPos == std::string::npos) return FromKey(DIK_A);

        const std::string prefix = str.substr(0, colonPos);
        const std::string value = str.substr(colonPos + 1);

        if (prefix == "Key") {
            if (value.size() >= 3 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
                unsigned int code = 0;
                std::from_chars(value.data() + 2, value.data() + value.size(), code, 16);
                return FromKey(static_cast<uint8_t>(code));
            }
            return FromKey(KeyNameToCode(value));
        }
        if (prefix == "Mouse") {
            return FromMouseButton(MouseButtonFromName(value));
        }
        if (prefix == "Gamepad") {
            return FromGamepadButton(GamepadButtonFromName(value));
        }
        if (prefix == "Axis" && !value.empty()) {
            const char sign = value.back();
            const bool positive = (sign != '-');
            return FromGamepadAxis(GamepadAxisFromName(value.substr(0, value.size() - 1)), positive);
        }
        return FromKey(DIK_A);
    }

} // namespace CoreEngine
