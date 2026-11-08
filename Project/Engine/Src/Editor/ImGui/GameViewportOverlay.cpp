#include "pch.h"
#include "GameViewportOverlay.h"

#ifdef CORE_EDITOR

#include <algorithm>
#include <utility>
#include <vector>

namespace CoreEngine::Editor
{
    namespace
    {
        struct OverlayEntry {
            uint32_t id = 0;
            GameViewportOverlayFunction function;
        };

        std::vector<OverlayEntry>& Entries()
        {
            static std::vector<OverlayEntry> entries;
            return entries;
        }

        uint32_t& NextId()
        {
            static uint32_t next = 1;
            return next;
        }
    }

    uint32_t GameViewportOverlay::Add(GameViewportOverlayFunction function)
    {
        if (!function) {
            return 0;
        }
        const uint32_t id = NextId()++;
        Entries().push_back(OverlayEntry{ id, std::move(function) });
        return id;
    }

    void GameViewportOverlay::Remove(uint32_t id)
    {
        auto& entries = Entries();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
            [id](const OverlayEntry& entry) { return entry.id == id; }), entries.end());
    }

    bool GameViewportOverlay::DrawAll(const GameViewportOverlayArgs& args)
    {
        bool consumed = false;
        for (const OverlayEntry& entry : Entries()) {
            consumed = entry.function(args) || consumed;
        }
        return consumed;
    }
}

#endif // CORE_EDITOR
