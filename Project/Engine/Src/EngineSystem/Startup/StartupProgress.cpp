#include "pch.h"
#include "StartupProgress.h"

#include <thread>

namespace
{
    // 起動シーケンス中しか使わないので、アトミック化やロックは要らない。
    // 所有スレッド以外からの Tick は下で弾く。
    CoreEngine::StartupProgress::Sink g_sink;
    std::thread::id g_ownerThread{};
}

namespace CoreEngine::StartupProgress
{
    void SetSink(Sink sink)
    {
        g_sink = std::move(sink);
        g_ownerThread = std::this_thread::get_id();
    }

    void ClearSink()
    {
        g_sink = nullptr;
        g_ownerThread = std::thread::id{};
    }

    bool IsActive() noexcept
    {
        return static_cast<bool>(g_sink);
    }

    void Tick(const char* detail)
    {
        if (!g_sink) {
            return;
        }
        // ワーカースレッドからの報告は捨てる（スプラッシュの GDI 描画と
        // メッセージポンプはウィンドウを作ったスレッドでしか動かせない）
        if (std::this_thread::get_id() != g_ownerThread) {
            return;
        }
        g_sink(detail);
    }
}
