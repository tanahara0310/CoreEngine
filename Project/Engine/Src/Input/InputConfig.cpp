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

    // 既定の割り当てはプロジェクト設定（InputActions.json）が持つ。
    // 綴りは keybindings.json と同じ（"Key:W" / "Gamepad:A" / "Axis:LeftStickY+" など）
    const std::vector<InputActionDef>& defs = InputActions::All();
    for (std::size_t i = 0; i < defs.size(); ++i) {
        std::vector<InputBinding> list;
        list.reserve(defs[i].defaults.size());
        for (const std::string& text : defs[i].defaults) {
            list.push_back(InputBinding::Deserialize(text));
        }
        SetBindings(static_cast<InputAction>(i), std::move(list));
    }
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
        if (action == InputAction::Invalid) continue;

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
