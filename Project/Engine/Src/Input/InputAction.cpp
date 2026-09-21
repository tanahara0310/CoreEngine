#include "pch.h"
#include "Input/InputAction.h"

#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"
#include "externals/nlohmann/single_include/nlohmann/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kSettingsPath = "Application/Config/EngineSettings/InputActions.json";
        constexpr const char* kActionsKey = "actions";
        constexpr const char* kVersionKey = "version";
        constexpr const char* kVersion = "1.0";

        /// @brief ファイルが無いときの並び（これまでエンジンが持っていたもの）
        std::vector<InputActionDef> DefaultDefs()
        {
            return {
                { "MoveForward", "前進",
                  { "Key:W", "Key:Up", "Axis:LeftStickY+", "Gamepad:DPadUp" } },
                { "MoveBack", "後退",
                  { "Key:S", "Key:Down", "Axis:LeftStickY-", "Gamepad:DPadDown" } },
                { "MoveLeft", "左移動",
                  { "Key:A", "Key:Left", "Axis:LeftStickX-", "Gamepad:DPadLeft" } },
                { "MoveRight", "右移動",
                  { "Key:D", "Key:Right", "Axis:LeftStickX+", "Gamepad:DPadRight" } },
                { "Jump", "ジャンプ", { "Key:Space", "Gamepad:A" } },
                { "Attack", "攻撃", { "Mouse:Left", "Gamepad:X" } },
                { "Interact", "インタラクト", { "Key:E", "Gamepad:B" } },
                { "UIConfirm", "UI決定", { "Key:Return", "Gamepad:A" } },
                { "UICancel", "UIキャンセル", { "Key:Escape", "Gamepad:B" } },
                // ポーズの開閉。パッドは START を使う。B は Interact と兼用なので、
                // UICancel をそのまま開閉に使うとゲーム中に誤って開いてしまう
                { "Pause", "ポーズ", { "Key:Escape", "Gamepad:Start" } },
                { "EditorGizmoTranslate", "ギズモ：移動", { "Key:W" } },
                { "EditorGizmoRotate", "ギズモ：回転", { "Key:E" } },
                { "EditorGizmoScale", "ギズモ：拡縮", { "Key:R" } },
            };
        }

        bool Validate(const std::vector<InputActionDef>& defs, std::string* outError)
        {
            const auto fail = [outError](const char* reason) {
                if (outError) { *outError = reason; }
                return false;
                };

            if (defs.empty()) {
                return fail("アクションが 1 つもありません");
            }
            if (defs.size() > kMaxInputActions) {
                return fail("アクションが多すぎます（64 まで）");
            }
            for (const InputActionDef& def : defs) {
                if (def.id.empty()) {
                    return fail("空の識別名は使えません");
                }
            }
            std::vector<std::string> ids;
            ids.reserve(defs.size());
            for (const InputActionDef& def : defs) {
                ids.push_back(def.id);
            }
            std::sort(ids.begin(), ids.end());
            if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
                return fail("同じ識別名が 2 つあります");
            }
            if (outError) { outError->clear(); }
            return true;
        }

        /// @brief ファイルを読む（読めなければ空を返す）
        std::vector<InputActionDef> ReadFile()
        {
            std::vector<InputActionDef> loaded;
            const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return loaded;
            }

            const nlohmann::json root = nlohmann::json::parse(in, nullptr, /*allow_exceptions=*/false);
            if (root.is_discarded() || !root.is_object()) {
                return loaded;
            }
            const auto found = root.find(kActionsKey);
            if (found == root.end() || !found->is_array()) {
                return loaded;
            }

            for (const auto& entry : *found) {
                if (!entry.is_object()) {
                    continue;
                }
                InputActionDef def;
                if (const auto id = entry.find("id"); id != entry.end() && id->is_string()) {
                    def.id = id->get<std::string>();
                }
                if (const auto name = entry.find("display"); name != entry.end() && name->is_string()) {
                    def.displayName = name->get<std::string>();
                }
                if (def.displayName.empty()) {
                    def.displayName = def.id;
                }
                if (const auto list = entry.find("defaults"); list != entry.end() && list->is_array()) {
                    for (const auto& binding : *list) {
                        if (binding.is_string()) {
                            def.defaults.push_back(binding.get<std::string>());
                        }
                    }
                }
                loaded.push_back(std::move(def));
            }
            return loaded;
        }

        std::vector<InputActionDef>& Table()
        {
            static std::vector<InputActionDef> defs = [] {
                std::vector<InputActionDef> loaded = ReadFile();
                // 壊れていたら既定へ倒す（ここはログの初期化より前に走りうる）
                return Validate(loaded, nullptr) ? loaded : DefaultDefs();
            }();
            return defs;
        }

        bool Write(const std::vector<InputActionDef>& defs)
        {
            const std::filesystem::path path = ProjectPaths::Resolve(kSettingsPath);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                return false;
            }

            nlohmann::json actions = nlohmann::json::array();
            for (const InputActionDef& def : defs) {
                nlohmann::json entry;
                entry["id"] = def.id;
                entry["display"] = def.displayName;
                entry["defaults"] = def.defaults;
                actions.push_back(std::move(entry));
            }

            nlohmann::json root;
            root[kVersionKey] = kVersion;
            root[kActionsKey] = std::move(actions);

            std::ofstream out(path, std::ios::binary);
            if (!out) {
                return false;
            }
            out << root.dump(4) << '\n';
            return static_cast<bool>(out);
        }
    }

    std::size_t InputActions::Count()
    {
        return Table().size();
    }

    const std::vector<InputActionDef>& InputActions::All()
    {
        return Table();
    }

    bool InputActions::SetAll(std::vector<InputActionDef> defs, std::string* outError)
    {
        if (!Validate(defs, outError)) {
            return false;
        }
        if (!Write(defs)) {
            if (outError) { *outError = "保存できませんでした"; }
            return false;
        }
        Table() = std::move(defs);
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::System,
            "入力アクションを {} 個にしました", Table().size());
        return true;
    }

    void InputActions::Reload()
    {
        std::vector<InputActionDef> loaded = ReadFile();
        Table() = Validate(loaded, nullptr) ? std::move(loaded) : DefaultDefs();
    }

    std::string_view InputActionToString(InputAction action)
    {
        const std::vector<InputActionDef>& defs = Table();
        const auto index = static_cast<std::size_t>(action);
        return index < defs.size() ? std::string_view(defs[index].id) : std::string_view("Unknown");
    }

    std::string_view InputActionToDisplayName(InputAction action)
    {
        const std::vector<InputActionDef>& defs = Table();
        const auto index = static_cast<std::size_t>(action);
        return index < defs.size() ? std::string_view(defs[index].displayName) : std::string_view("不明");
    }

    InputAction InputActionFromString(std::string_view str)
    {
        const std::vector<InputActionDef>& defs = Table();
        for (std::size_t i = 0; i < defs.size(); ++i) {
            if (defs[i].id == str) {
                return static_cast<InputAction>(i);
            }
        }
        return InputAction::Invalid;
    }
}
