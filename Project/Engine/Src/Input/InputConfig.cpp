#include "pch.h"
#include "InputConfig.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine {

const std::vector<InputBinding> InputConfig::kEmpty_;

void InputConfig::SetBindings(InputAction action, std::vector<InputBinding> bindings) {
    bindings_[static_cast<uint32_t>(action)] = std::move(bindings);
}

void InputConfig::AddBinding(InputAction action, const InputBinding& binding) {
    bindings_[static_cast<uint32_t>(action)].push_back(binding);
}

void InputConfig::ClearBindings(InputAction action) {
    bindings_[static_cast<uint32_t>(action)].clear();
}

const std::vector<InputBinding>& InputConfig::GetBindings(InputAction action) const {
    auto it = bindings_.find(static_cast<uint32_t>(action));
    if (it == bindings_.end()) return kEmpty_;
    return it->second;
}

void InputConfig::ResetToDefault() {
    bindings_.clear();

    // 移動
    SetBindings(InputAction::MoveForward, {
        InputBinding::FromKey(DIK_W),
        InputBinding::FromKey(DIK_UP),
        InputBinding::FromGamepadAxis(GamepadAxis::LeftStickY, true),
        InputBinding::FromGamepadButton(GamepadButton::DPadUp),
    });
    SetBindings(InputAction::MoveBack, {
        InputBinding::FromKey(DIK_S),
        InputBinding::FromKey(DIK_DOWN),
        InputBinding::FromGamepadAxis(GamepadAxis::LeftStickY, false),
        InputBinding::FromGamepadButton(GamepadButton::DPadDown),
    });
    SetBindings(InputAction::MoveLeft, {
        InputBinding::FromKey(DIK_A),
        InputBinding::FromKey(DIK_LEFT),
        InputBinding::FromGamepadAxis(GamepadAxis::LeftStickX, false),
        InputBinding::FromGamepadButton(GamepadButton::DPadLeft),
    });
    SetBindings(InputAction::MoveRight, {
        InputBinding::FromKey(DIK_D),
        InputBinding::FromKey(DIK_RIGHT),
        InputBinding::FromGamepadAxis(GamepadAxis::LeftStickX, true),
        InputBinding::FromGamepadButton(GamepadButton::DPadRight),
    });

    // アクション
    SetBindings(InputAction::Jump, {
        InputBinding::FromKey(DIK_SPACE),
        InputBinding::FromGamepadButton(GamepadButton::A),
    });
    SetBindings(InputAction::Attack, {
        InputBinding::FromMouseButton(MouseButton::Left),
        InputBinding::FromGamepadButton(GamepadButton::X),
    });
    SetBindings(InputAction::Interact, {
        InputBinding::FromKey(DIK_E),
        InputBinding::FromGamepadButton(GamepadButton::B),
    });

    // UI
    SetBindings(InputAction::UIConfirm, {
        InputBinding::FromKey(DIK_RETURN),
        InputBinding::FromGamepadButton(GamepadButton::A),
    });
    SetBindings(InputAction::UICancel, {
        InputBinding::FromKey(DIK_ESCAPE),
        InputBinding::FromGamepadButton(GamepadButton::B),
    });

    // エディタ専用
    SetBindings(InputAction::EditorGizmoTranslate, {
        InputBinding::FromKey(DIK_W),
    });
    SetBindings(InputAction::EditorGizmoRotate, {
        InputBinding::FromKey(DIK_E),
    });
    SetBindings(InputAction::EditorGizmoScale, {
        InputBinding::FromKey(DIK_R),
    });
}

bool InputConfig::LoadFromFile(const std::string& filePath) {
    auto& jsonManager = JsonManager::GetInstance();
    if (!jsonManager.FileExists(filePath)) {
        return false;
    }

    json j = jsonManager.LoadJson(filePath);
    if (j.is_null() || !j.contains("bindings") || !j["bindings"].is_object()) {
        return false;
    }

    // まず既定値を敷いてからファイルの内容で上書きする。
    // 単純に bindings_ を空にすると、ファイルに書かれていないアクション
    // （enum に後から追加したもの・旧バージョンのファイル）が
    // 「バインディング 0 件 = 何を押しても反応しない」状態になってしまう。
    // ファイル側にキーがあれば空配列もそのまま反映されるので、
    // ユーザーが意図的に全解除したアクションは解除のまま残る
    ResetToDefault();

    const auto& bindingsJson = j["bindings"];
    for (auto it = bindingsJson.begin(); it != bindingsJson.end(); ++it) {
        const InputAction action = InputActionFromString(it.key());
        if (action == InputAction::Count) continue;

        std::vector<InputBinding> list;
        for (const auto& entry : it.value()) {
            if (entry.is_string()) {
                list.push_back(InputBinding::Deserialize(entry.get<std::string>()));
            }
        }
        SetBindings(action, std::move(list));
    }

    return true;
}

bool InputConfig::SaveToFile(const std::string& filePath) const {
    json bindingsJson = json::object();

    for (const auto& [actionId, bindings] : bindings_) {
        const InputAction action = static_cast<InputAction>(actionId);
        const std::string_view actionName = InputActionToString(action);
        if (actionName == "Unknown") continue;

        json bindingArray = json::array();
        for (const auto& binding : bindings) {
            bindingArray.push_back(binding.Serialize());
        }
        bindingsJson[std::string(actionName)] = bindingArray;
    }

    json j;
    j["bindings"] = bindingsJson;
    return JsonManager::GetInstance().SaveJson(filePath, j);
}

} // namespace CoreEngine
