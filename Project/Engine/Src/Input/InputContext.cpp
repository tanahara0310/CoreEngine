#include "pch.h"
#include "Input/InputContext.h"

namespace CoreEngine
{
    namespace
    {
        struct ContextName {
            InputContext context;
            const char* id;
            const char* display;
        };

        constexpr ContextName kNames[] = {
            { InputContext::Game,   "Game",   "ゲーム" },
            { InputContext::UI,     "UI",     "UI" },
            { InputContext::Editor, "Editor", "エディタ" },
        };
    }

    std::string InputContextToString(InputContext contexts)
    {
        std::string text;
        for (const ContextName& name : kNames) {
            if (!Overlaps(contexts, name.context)) {
                continue;
            }
            if (!text.empty()) {
                text += '|';
            }
            text += name.id;
        }
        return text.empty() ? std::string("None") : text;
    }

    InputContext InputContextFromString(std::string_view text)
    {
        InputContext result = InputContext::None;
        std::size_t begin = 0;
        while (begin <= text.size()) {
            const std::size_t found = text.find('|', begin);
            const std::size_t end = (found == std::string_view::npos) ? text.size() : found;
            const std::string_view token = text.substr(begin, end - begin);
            for (const ContextName& name : kNames) {
                if (token == name.id) {
                    result |= name.context;
                    break;
                }
            }
            if (found == std::string_view::npos) {
                break;
            }
            begin = found + 1;
        }
        // 綴りが読めないアクションを無反応にしないよう、ゲームの操作として扱う
        return result == InputContext::None ? InputContext::Game : result;
    }

    std::string_view InputContextToDisplayName(InputContext context)
    {
        for (const ContextName& name : kNames) {
            if (context == name.context) {
                return name.display;
            }
        }
        return "その他";
    }
}
