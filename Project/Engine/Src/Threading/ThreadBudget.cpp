#include "pch.h"
#include "ThreadBudget.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

namespace
{
    std::mutex g_mutex;
    uint32_t   g_reserved = 0;
}

namespace CoreEngine::ThreadBudget
{
    uint32_t GetCapacity()
    {
        const uint32_t hardware = std::thread::hardware_concurrency();
        if (hardware <= 1) {
            return 1;
        }
        // メインスレッドのぶんを 1 本残す
        return hardware - 1;
    }

    uint32_t GetReserved()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_reserved;
    }

    uint32_t Reserve(uint32_t requested)
    {
        const uint32_t capacity = GetCapacity();

        std::lock_guard<std::mutex> lock(g_mutex);

        const uint32_t remaining = (g_reserved < capacity) ? (capacity - g_reserved) : 0u;

        if (requested == 0) {
            // 既定は「残り枠の半分」。あとから作られるプールのために枠を残す。
            requested = (std::max)(1u, remaining / 2u);
        }

        const uint32_t granted = (std::max)(1u, (std::min)(requested, remaining));
        g_reserved += granted;
        return granted;
    }

    void Release(uint32_t count)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_reserved = (g_reserved > count) ? (g_reserved - count) : 0u;
    }
}
