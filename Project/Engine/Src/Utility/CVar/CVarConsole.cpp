#include "pch.h"
#include "Utility/CVar/CVarConsole.h"

#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/CVar/CVarUndoStack.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace CoreEngine::CVarConsole
{
    namespace
    {
        /// 一覧で出す行数の上限
        constexpr std::size_t kListLimit = 30;

        std::vector<std::string> Tokenize(std::string_view line)
        {
            std::vector<std::string> tokens;
            std::size_t at = 0;
            while (at < line.size()) {
                while (at < line.size() && std::isspace(static_cast<unsigned char>(line[at])) != 0) {
                    ++at;
                }
                const std::size_t begin = at;
                while (at < line.size() && std::isspace(static_cast<unsigned char>(line[at])) == 0) {
                    ++at;
                }
                if (at > begin) {
                    tokens.emplace_back(line.substr(begin, at - begin));
                }
            }
            return tokens;
        }

        std::string ToLower(std::string_view text)
        {
            std::string lower(text);
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            return lower;
        }

        bool ParseBool(std::string_view text, bool& out)
        {
            const std::string lower = ToLower(text);
            if (lower == "1" || lower == "true" || lower == "on" || lower == "yes") {
                out = true;
                return true;
            }
            if (lower == "0" || lower == "false" || lower == "off" || lower == "no") {
                out = false;
                return true;
            }
            return false;
        }

        /// @brief 数値の並びを読む（区切りは空白・カンマ・括弧）
        /// @return 読めた数（`out` には読めた分だけ入る）
        std::size_t ParseNumbers(std::string_view text, std::size_t limit, float* out)
        {
            std::string cleaned(text);
            for (char& c : cleaned) {
                if (c == ',' || c == '[' || c == ']' || c == '(' || c == ')') {
                    c = ' ';
                }
            }
            std::istringstream stream(cleaned);
            std::size_t count = 0;
            while (count < limit && (stream >> out[count])) {
                ++count;
            }
            std::string extra;
            return (stream >> extra) ? limit + 1 : count;
        }

        const char* TypeName(CVarType type)
        {
            switch (type) {
            case CVarType::Bool:    return "bool";
            case CVarType::Int:     return "int";
            case CVarType::Float:   return "float";
            case CVarType::Vector2: return "Vector2";
            case CVarType::Vector3: return "Vector3";
            case CVarType::Color:   return "Color";
            }
            return "?";
        }

        /// @brief 一覧の 1 行（名前 = 値）
        std::string Summarize(const ICVar& cvar)
        {
            return std::string(cvar.GetName()) + " = " + cvar.ValueToString();
        }

        /// @brief 1 つの CVar の説明（複数行）
        std::vector<std::string> Describe(const ICVar& cvar)
        {
            std::vector<std::string> lines;
            lines.push_back(Summarize(cvar));
            lines.push_back(std::string("  型: ") + TypeName(cvar.GetType()) +
                "  既定: " + cvar.DefaultToString() + (cvar.IsModified() ? "（変更されています）" : ""));
            if (const CVarRange range = cvar.GetRange(); range.valid) {
                lines.push_back("  範囲: " + std::to_string(range.min) + " 〜 " + std::to_string(range.max));
            }
            if (const char* const description = cvar.GetDescription(); description && description[0] != '\0') {
                lines.push_back(std::string("  ") + description);
            }
            std::string flags;
            if (HasFlag(cvar.GetFlags(), CVarFlags::NoSave)) { flags += "保存しない "; }
            if (HasFlag(cvar.GetFlags(), CVarFlags::NoUI)) { flags += "自動 UI に出さない "; }
            if (HasFlag(cvar.GetFlags(), CVarFlags::Mirrored)) { flags += "実体が毎フレーム上書きする "; }
            if (!flags.empty()) {
                lines.push_back("  " + flags);
            }
            return lines;
        }

        /// @brief 書き込みを Undo へ積んで確定させる
        void Commit(ICVar& cvar, const void* value)
        {
            CVarUndoStack::Get().BeginEdit(&cvar);
            cvar.SetFromPointer(value);
            CVarUndoStack::Get().CommitEdit(&cvar);
            CVarRegistry::Get().NotifyCommit();
        }

        std::vector<std::string> Usage()
        {
            return {
                "cvar <接頭辞>        - 名前が一致する CVar と値を一覧",
                "cvar <名前>          - その CVar の型・値・既定値・説明を表示",
                "cvar <名前> <値>     - 値を書き込む（Ctrl+Z で戻せる）",
                "cvar reset <名前>    - 既定値へ戻す",
            };
        }
    }

    bool SetFromString(ICVar& cvar, std::string_view text, std::string& reason)
    {
        float numbers[4]{};
        switch (cvar.GetType()) {
        case CVarType::Bool: {
            bool value = false;
            if (!ParseBool(text, value)) {
                reason = "bool には true / false / on / off / 1 / 0 を渡します";
                return false;
            }
            Commit(cvar, &value);
            return true;
        }
        case CVarType::Int: {
            if (ParseNumbers(text, 1, numbers) != 1) {
                reason = "int には数値を 1 つ渡します";
                return false;
            }
            const int value = static_cast<int>(numbers[0]);
            Commit(cvar, &value);
            return true;
        }
        case CVarType::Float: {
            if (ParseNumbers(text, 1, numbers) != 1) {
                reason = "float には数値を 1 つ渡します";
                return false;
            }
            Commit(cvar, &numbers[0]);
            return true;
        }
        case CVarType::Vector2: {
            if (ParseNumbers(text, 2, numbers) != 2) {
                reason = "Vector2 には数値を 2 つ渡します（例: 1 0.5）";
                return false;
            }
            const Vector2 value{ numbers[0], numbers[1] };
            Commit(cvar, &value);
            return true;
        }
        case CVarType::Vector3: {
            if (ParseNumbers(text, 3, numbers) != 3) {
                reason = "Vector3 には数値を 3 つ渡します（例: 1 0.5 0.25）";
                return false;
            }
            const Vector3 value{ numbers[0], numbers[1], numbers[2] };
            Commit(cvar, &value);
            return true;
        }
        case CVarType::Color: {
            const std::size_t count = ParseNumbers(text, 4, numbers);
            if (count != 3 && count != 4) {
                reason = "色には数値を 3 つか 4 つ渡します（例: 1 0.5 0.25 1）";
                return false;
            }
            const Vector4* const current = cvar.AsColor();
            const Vector4 value{ numbers[0], numbers[1], numbers[2],
                count == 4 ? numbers[3] : (current ? current->w : 1.0f) };
            Commit(cvar, &value);
            return true;
        }
        }
        reason = "この型は書き込めません";
        return false;
    }

    Result Execute(std::string_view commandLine)
    {
        Result result;
        const std::vector<std::string> tokens = Tokenize(commandLine);
        if (tokens.empty() || ToLower(tokens[0]) != "cvar") {
            return result;
        }
        result.handled = true;

        CVarRegistry& registry = CVarRegistry::Get();
        if (tokens.size() == 1) {
            result.lines = Usage();
            result.lines.push_back("登録されている CVar: " + std::to_string(registry.GetAll().size()) + " 個");
            return result;
        }

        // cvar reset <名前>
        if (ToLower(tokens[1]) == "reset") {
            if (tokens.size() < 3) {
                result.failed = true;
                result.lines.push_back("使い方: cvar reset <名前>");
                return result;
            }
            ICVar* const cvar = registry.Find(tokens[2]);
            if (!cvar) {
                result.failed = true;
                result.lines.push_back("見つかりません: " + tokens[2]);
                return result;
            }
            const std::string before = cvar->ValueToString();
            CVarUndoStack::Get().BeginEdit(cvar);
            cvar->ResetToDefault();
            CVarUndoStack::Get().CommitEdit(cvar);
            registry.NotifyCommit();
            result.lines.push_back("既定値へ戻しました: " + Summarize(*cvar) + "（前は " + before + "）");
            return result;
        }

        const std::string& name = tokens[1];
        ICVar* const exact = registry.Find(name);

        // cvar <名前> <値>
        if (tokens.size() >= 3) {
            if (!exact) {
                result.failed = true;
                result.lines.push_back("見つかりません: " + name);
                return result;
            }
            std::string value = tokens[2];
            for (std::size_t i = 3; i < tokens.size(); ++i) {
                value += " " + tokens[i];
            }
            const std::string before = exact->ValueToString();
            std::string reason;
            if (!SetFromString(*exact, value, reason)) {
                result.failed = true;
                result.lines.push_back(std::string(exact->GetName()) + ": " + reason);
                return result;
            }
            result.lines.push_back("設定しました: " + Summarize(*exact) + "（前は " + before + "）");
            return result;
        }

        // cvar <名前>
        if (exact) {
            result.lines = Describe(*exact);
            return result;
        }

        // cvar <接頭辞>
        const std::vector<ICVar*> matched = registry.GetByPrefix(name);
        if (matched.empty()) {
            result.failed = true;
            result.lines.push_back("一致する CVar がありません: " + name);
            return result;
        }
        for (std::size_t i = 0; i < matched.size() && i < kListLimit; ++i) {
            result.lines.push_back(Summarize(*matched[i]));
        }
        if (matched.size() > kListLimit) {
            result.lines.push_back("… 他 " + std::to_string(matched.size() - kListLimit) +
                " 個（もっと長い接頭辞で絞ります）");
        }
        return result;
    }

    std::vector<std::string> Complete(std::string_view commandLine, std::size_t limit)
    {
        std::vector<std::string> candidates;
        const std::vector<std::string> tokens = Tokenize(commandLine);
        if (tokens.empty() || ToLower(tokens[0]) != "cvar" || tokens.size() > 2) {
            return candidates;
        }

        // 区切りで終わっているときは、次の語の候補を全部出す
        const bool typingName = tokens.size() == 2 && !commandLine.empty() &&
            std::isspace(static_cast<unsigned char>(commandLine.back())) == 0;
        const std::string prefix = typingName ? tokens[1] : std::string();
        for (const ICVar* const cvar : CVarRegistry::Get().GetByPrefix(prefix)) {
            if (candidates.size() >= limit) {
                break;
            }
            candidates.push_back("cvar " + std::string(cvar->GetName()));
        }
        return candidates;
    }
}
