#include "pch.h"
#include "InputBinding.h"
#include "Utility/Enum/EnumNames.h"
#include <cstring>
#include <charconv>
#include <format>

namespace CoreEngine {

    namespace {

        // ─── キーボード名テーブル ─────────────────────────────────────
        constexpr EnumName<uint8_t> kKeyTable[] = {
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

        // ─── マウスボタン名テーブル ────────────────────────────────────
        constexpr EnumName<MouseButton> kMouseButtonTable[] = {
            { MouseButton::Left,     "Left"     },
            { MouseButton::Right,    "Right"    },
            { MouseButton::Middle,   "Middle"   },
            { MouseButton::XButton1, "XButton1" },
            { MouseButton::XButton2, "XButton2" },
        };

        // ─── ゲームパッドボタン名テーブル ─────────────────────────────
        constexpr EnumName<GamepadButton> kGamepadButtonTable[] = {
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

        // ─── ゲームパッド軸名テーブル ──────────────────────────────────
        constexpr EnumName<GamepadAxis> kGamepadAxisTable[] = {
            { GamepadAxis::LeftStickX,  "LeftStickX"  },
            { GamepadAxis::LeftStickY,  "LeftStickY"  },
            { GamepadAxis::RightStickX, "RightStickX" },
            { GamepadAxis::RightStickY, "RightStickY" },
            { GamepadAxis::LeftTrigger, "LeftTrigger" },
            { GamepadAxis::RightTrigger,"RightTrigger"},
        };

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
        std::string prefix;
        if (HasModifier(modifiers, InputModifier::Ctrl))  { prefix += "Ctrl+"; }
        if (HasModifier(modifiers, InputModifier::Shift)) { prefix += "Shift+"; }
        if (HasModifier(modifiers, InputModifier::Alt))   { prefix += "Alt+"; }
        return prefix + SerializeBody();
    }

    std::string InputBinding::SerializeBody() const {
        switch (type) {
        case BindingType::Keyboard: {
            const std::string_view name = NameOf(kKeyTable, static_cast<uint8_t>(code));
            if (!name.empty()) {
                return std::format("Key:{}", name);
            }
            return std::format("Key:0x{:02X}", code);
        }
        case BindingType::MouseButton: {
            const std::string_view name = NameOf(kMouseButtonTable, static_cast<MouseButton>(code), "Left");
            return std::format("Mouse:{}", name);
        }
        case BindingType::GamepadButton: {
            const std::string_view name = NameOf(kGamepadButtonTable, static_cast<GamepadButton>(code), "A");
            return std::format("Gamepad:{}", name);
        }
        case BindingType::GamepadAxis: {
            const std::string_view name = NameOf(kGamepadAxisTable, static_cast<GamepadAxis>(code), "LeftStickX");
            const char* sign = (axisSign >= 0.0f) ? "+" : "-";
            return std::format("Axis:{}{}", name, sign);
        }
        }
        return "Key:A";
    }

    InputBinding InputBinding::Deserialize(const std::string& str) {
        // 前に付いている修飾キーを剥がす
        InputModifier modifiers = InputModifier::None;
        std::string body = str;
        for (bool found = true; found; ) {
            found = false;
            const struct { const char* text; InputModifier bit; } kHeads[] = {
                { "Ctrl+",  InputModifier::Ctrl },
                { "Shift+", InputModifier::Shift },
                { "Alt+",   InputModifier::Alt },
            };
            for (const auto& head : kHeads) {
                const std::size_t length = std::strlen(head.text);
                if (body.compare(0, length, head.text) == 0) {
                    modifiers |= head.bit;
                    body = body.substr(length);
                    found = true;
                    break;
                }
            }
        }

        InputBinding result = DeserializeBody(body);
        result.modifiers = modifiers;
        return result;
    }

    InputBinding InputBinding::DeserializeBody(const std::string& str) {
        const auto colonPos = str.find(':');
        if (colonPos == std::string::npos) return FromKey(0);

        const std::string prefix = str.substr(0, colonPos);
        const std::string value = str.substr(colonPos + 1);

        if (prefix == "Key") {
            if (value.size() >= 3 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
                unsigned int code = 0;
                std::from_chars(value.data() + 2, value.data() + value.size(), code, 16);
                return FromKey(static_cast<uint8_t>(code));
            }
            return FromKey(ParseName(kKeyTable, value, uint8_t{ 0 }));
        }
        if (prefix == "Mouse") {
            return FromMouseButton(ParseName(kMouseButtonTable, value, MouseButton::Left));
        }
        if (prefix == "Gamepad") {
            return FromGamepadButton(ParseName(kGamepadButtonTable, value, GamepadButton::A));
        }
        if (prefix == "Axis" && !value.empty()) {
            const char sign = value.back();
            const bool positive = (sign != '-');
            return FromGamepadAxis(
                ParseName(kGamepadAxisTable, value.substr(0, value.size() - 1), GamepadAxis::LeftStickX), positive);
        }
        return FromKey(0);
    }

} // namespace CoreEngine
