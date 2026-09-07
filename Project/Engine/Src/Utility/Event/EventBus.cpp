#include "pch.h"
#include "EventBus.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cassert>

#ifdef _DEBUG
#include <thread>
#endif

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "Utility/FrameRate/Time.h"
#include <cstring>
#endif

namespace CoreEngine
{
    namespace
    {
        /// @brief EventBus のシングルトンが生存しているか
        /// @details Subscription は GameObject より長生きすることは無いが、静的変数として
        ///          持たれた場合はプロセス終了時に EventBus より後で壊れ得る。
        ///          その 1 ケースだけのために解除処理を空振りさせる。
        bool g_busAlive = false;

#ifdef _DEBUG
        /// @brief 最初に EventBus へ触れたスレッド（以後ここ以外からの使用を警告する）
        std::thread::id g_ownerThread{};
        bool g_ownerThreadCaptured = false;
        bool g_threadWarningReported = false;

        /// @brief メインスレッド以外からの呼び出しを検出する
        void VerifyOwnerThread(const char* function)
        {
            const std::thread::id current = std::this_thread::get_id();

            if (!g_ownerThreadCaptured) {
                g_ownerThread = current;
                g_ownerThreadCaptured = true;
                return;
            }

            // 一度報告したら黙る（毎フレーム数百回出ると他のログが流れる）
            if (current != g_ownerThread && !g_threadWarningReported) {
                g_threadWarningReported = true;
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                    "EventBus::{} がゲームループ以外のスレッドから呼ばれました。"
                    "EventBus はロックを持たないメインスレッド専用です。", function);
            }
        }
#define EVENTBUS_VERIFY_THREAD() VerifyOwnerThread(__func__)
#else
#define EVENTBUS_VERIFY_THREAD() ((void)0)
#endif
    }

    namespace EventDetail
    {
        EventTypeId AcquireEventTypeId()
        {
            // 0 から連番。EventBus::channels_ の添字として直接使う
            static EventTypeId next = 0;
            return next++;
        }

        bool IsBusAlive() { return g_busAlive; }
    }

    // ──────────────────────────────────────────────────────────────
    // Subscription
    // ──────────────────────────────────────────────────────────────

    void Subscription::Unsubscribe()
    {
        if (handle_ == 0) { return; }

        // 静的破棄フェーズで EventBus が先に消えている場合はもう解除するものが無い
        if (EventDetail::IsBusAlive()) {
            EventBus::GetInstance().UnsubscribeRaw(typeId_, handle_);
        }
        handle_ = 0;
    }

    // ──────────────────────────────────────────────────────────────
    // EventBus 生成・破棄
    // ──────────────────────────────────────────────────────────────

    EventBus::EventBus()
    {
        g_busAlive = true;
    }

    EventBus::~EventBus()
    {
        g_busAlive = false;
    }

    EventBus& EventBus::GetInstance()
    {
        static EventBus instance;
        return instance;
    }

    // ──────────────────────────────────────────────────────────────
    // 購読
    // ──────────────────────────────────────────────────────────────

    EventBus::Channel& EventBus::GetOrCreateChannel(EventTypeId typeId, const char* typeName)
    {
        if (typeId >= channels_.size()) {
            channels_.resize(static_cast<std::size_t>(typeId) + 1);
        }

        std::unique_ptr<Channel>& slot = channels_[typeId];
        if (!slot) {
            slot = std::make_unique<Channel>();
            slot->name = (typeName != nullptr) ? typeName : "<unknown>";
        }
        return *slot;
    }

    Subscription EventBus::SubscribeRaw(EventTypeId typeId, const char* typeName,
        std::function<void(const void*)> invoker, int priority)
    {
        EVENTBUS_VERIFY_THREAD();

        Channel& channel = GetOrCreateChannel(typeId, typeName);

        Listener listener;
        listener.handle = nextHandle_++;
        listener.priority = priority;
        listener.alive = true;
        listener.invoke = std::move(invoker);

        const SubscriptionHandle handle = listener.handle;

        if (dispatchDepth_ > 0) {
            // 配信中に listeners を触ると走査中の要素がずれるので、一旦別の箱へ置く。
            // 「このフレームの配信には参加しない」という挙動になる（意図した仕様）。
            channel.pendingAdd.push_back(std::move(listener));
            hasPendingChanges_ = true;
        } else {
            channel.listeners.push_back(std::move(listener));

            // 優先度の降順。stable_sort なので同値なら登録順が保たれる
            std::stable_sort(channel.listeners.begin(), channel.listeners.end(),
                [](const Listener& a, const Listener& b) { return a.priority > b.priority; });
        }

        return Subscription(typeId, handle);
    }

    void EventBus::UnsubscribeRaw(EventTypeId typeId, SubscriptionHandle handle)
    {
        if (handle == 0 || typeId >= channels_.size()) { return; }

        std::unique_ptr<Channel>& slot = channels_[typeId];
        if (!slot) { return; }

        // 実体を消すのは配信が全段終わってから。ここでは alive を倒すだけにして、
        // 「解除した瞬間から呼ばれない」と「走査中のベクタを壊さない」を両立させる。
        bool found = false;
        for (Listener& listener : slot->listeners) {
            if (listener.handle == handle) {
                listener.alive = false;
                slot->needsCompact = true;
                found = true;
                break;
            }
        }

        // 配信中に登録され、まだ合流していない購読も解除対象
        if (!found) {
            for (Listener& listener : slot->pendingAdd) {
                if (listener.handle == handle) {
                    listener.alive = false;
                    break;
                }
            }
        }

        hasPendingChanges_ = true;

        if (dispatchDepth_ == 0) {
            FlushPendingChanges();
        }
    }

    std::size_t EventBus::GetSubscriberCountRaw(EventTypeId typeId) const
    {
        if (typeId >= channels_.size()) { return 0; }

        const std::unique_ptr<Channel>& slot = channels_[typeId];
        if (!slot) { return 0; }

        std::size_t count = 0;
        for (const Listener& listener : slot->listeners) {
            if (listener.alive) { ++count; }
        }
        for (const Listener& listener : slot->pendingAdd) {
            if (listener.alive) { ++count; }
        }
        return count;
    }

    void EventBus::FlushPendingChanges()
    {
        if (!hasPendingChanges_) { return; }
        assert(dispatchDepth_ == 0 && "配信中に購読リストを組み替えてはいけない");

        for (std::unique_ptr<Channel>& slot : channels_) {
            if (!slot) { continue; }

            const bool hasAdds = !slot->pendingAdd.empty();
            if (!hasAdds && !slot->needsCompact) { continue; }

            // 解除済みを取り除く
            if (slot->needsCompact) {
                slot->listeners.erase(
                    std::remove_if(slot->listeners.begin(), slot->listeners.end(),
                        [](const Listener& listener) { return !listener.alive; }),
                    slot->listeners.end());
                slot->needsCompact = false;
            }

            // 配信中に来た購読を合流させる（合流前に解除されたものは捨てる）
            if (hasAdds) {
                for (Listener& listener : slot->pendingAdd) {
                    if (listener.alive) {
                        slot->listeners.push_back(std::move(listener));
                    }
                }
                slot->pendingAdd.clear();

                std::stable_sort(slot->listeners.begin(), slot->listeners.end(),
                    [](const Listener& a, const Listener& b) { return a.priority > b.priority; });
            }
        }

        hasPendingChanges_ = false;
    }

    // ──────────────────────────────────────────────────────────────
    // 発行
    // ──────────────────────────────────────────────────────────────

    void EventBus::PublishRaw(EventTypeId typeId, const char* typeName, const void* payload)
    {
        EVENTBUS_VERIFY_THREAD();

        // ハンドラ同士が互いに Publish し合うと止まらなくなる。
        // 深さで打ち切り、どのイベントで起きたかをログに残す。
        if (dispatchDepth_ >= kMaxDispatchDepth) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::System,
                "EventBus: イベント '{}' の配信が入れ子 {} 段に達したため打ち切りました。"
                "ハンドラ同士が Publish を呼び合っている可能性があります。",
                (typeName != nullptr) ? typeName : "<unknown>", kMaxDispatchDepth);
            return;
        }

        if (typeId >= channels_.size()) { return; }

        std::unique_ptr<Channel>& slot = channels_[typeId];
        if (!slot) { return; }

        Channel& channel = *slot;

#ifdef USE_IMGUI
        SyncFrameStats();
        ++channel.publishTotal;
        ++channel.publishThisFrame;
        RecordTrace(channel);
#else
        (void)typeName;
#endif

        if (channel.listeners.empty()) { return; }

        ++dispatchDepth_;

        // 添字で回す。配信中の Subscribe は pendingAdd 行き、Unsubscribe は alive を倒すだけなので、
        // このループの最中に listeners のサイズも要素の位置も変化しない。
        for (std::size_t i = 0; i < channel.listeners.size(); ++i) {
            Listener& listener = channel.listeners[i];
            if (!listener.alive || !listener.invoke) { continue; }
            listener.invoke(payload);
        }

        --dispatchDepth_;

        if (dispatchDepth_ == 0) {
            FlushPendingChanges();
        }
    }

    void EventBus::EnqueueRaw(std::unique_ptr<IQueuedEvent> event)
    {
        EVENTBUS_VERIFY_THREAD();

        if (event) {
            queued_.push_back(std::move(event));
        }
    }

    void EventBus::DispatchQueued()
    {
        EVENTBUS_VERIFY_THREAD();

        if (queued_.empty()) { return; }

        // 配信中に積まれた分を次フレームへ回すため、先に入れ替えてしまう。
        // ここを「空になるまで回す」にすると Queue し合うハンドラで無限ループになる。
        dispatching_.swap(queued_);

#ifdef USE_IMGUI
        dispatchingQueued_ = true;
#endif

        for (std::unique_ptr<IQueuedEvent>& event : dispatching_) {
            if (event) { event->Dispatch(*this); }
        }

#ifdef USE_IMGUI
        dispatchingQueued_ = false;
#endif

        dispatching_.clear();
    }

    // ──────────────────────────────────────────────────────────────
    // 管理
    // ──────────────────────────────────────────────────────────────

    void EventBus::Clear()
    {
        EVENTBUS_VERIFY_THREAD();

        if (dispatchDepth_ > 0) {
            Logger::GetInstance().Log(
                "EventBus::Clear() が配信中に呼ばれたため無視しました",
                LogLevel::Error, LogCategory::System);
            return;
        }

        // Channel 自体は残す（型 ID が添字なので詰めてはいけない。統計と型名も維持される）。
        // 残っている Subscription はハンドルが見つからなくなるだけで、解除は安全に空振りする。
        for (std::unique_ptr<Channel>& slot : channels_) {
            if (!slot) { continue; }
            slot->listeners.clear();
            slot->pendingAdd.clear();
            slot->needsCompact = false;
        }

        queued_.clear();
        hasPendingChanges_ = false;
    }

    // ──────────────────────────────────────────────────────────────
    // デバッグ表示
    // ──────────────────────────────────────────────────────────────

#ifdef USE_IMGUI
    void EventBus::SyncFrameStats()
    {
        const std::uint64_t frame = Time::FrameCount();
        if (statsFrame_ == frame) { return; }

        // フレームが変わったので全チャンネルの「今フレーム分」を確定させる
        for (std::unique_ptr<Channel>& slot : channels_) {
            if (!slot) { continue; }
            slot->publishLastFrame = slot->publishThisFrame;
            slot->publishThisFrame = 0;
        }
        statsFrame_ = frame;
    }

    void EventBus::RecordTrace(const Channel& channel)
    {
        if (tracePaused_) { return; }

        if (trace_.size() < kTraceCapacity) {
            trace_.resize(kTraceCapacity);
        }

        std::uint32_t aliveCount = 0;
        for (const Listener& listener : channel.listeners) {
            if (listener.alive) { ++aliveCount; }
        }

        TraceEntry& entry = trace_[traceHead_];
        entry.frame = Time::FrameCount();
        entry.name = channel.name;
        entry.listenerCount = aliveCount;
        entry.wasQueued = dispatchingQueued_;

        traceHead_ = (traceHead_ + 1) % kTraceCapacity;
        traceCount_ = (std::min)(traceCount_ + 1, kTraceCapacity);
    }

    void EventBus::DrawImGui()
    {
        // 今フレーム 1 件も発行されていない場合でも表示を最新にする
        SyncFrameStats();

        // ── サマリ ────────────────────────────────────────────────
        std::size_t channelCount = 0;
        std::size_t listenerCount = 0;
        for (const std::unique_ptr<Channel>& slot : channels_) {
            if (!slot) { continue; }
            ++channelCount;
            for (const Listener& listener : slot->listeners) {
                if (listener.alive) { ++listenerCount; }
            }
        }

        ImGui::Text("Event types: %zu   Listeners: %zu   Queued: %zu",
            channelCount, listenerCount, queued_.size());

        if (dispatchDepth_ > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "[dispatching depth=%d]", dispatchDepth_);
        }

        UI::Separator();

        // ── 型ごとの一覧 ──────────────────────────────────────────
        if (ImGui::CollapsingHeader("Channels", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::BeginTable("##EventBusChannels", 4,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {

                ImGui::TableSetupColumn("Event");
                ImGui::TableSetupColumn("Listeners", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Last frame", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                for (const std::unique_ptr<Channel>& slot : channels_) {
                    if (!slot) { continue; }

                    std::uint32_t alive = 0;
                    for (const Listener& listener : slot->listeners) {
                        if (listener.alive) { ++alive; }
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(slot->name.c_str());

                    ImGui::TableNextColumn();
                    // 発行されているのに誰も聞いていないイベントは配線ミスの可能性が高い
                    if (alive == 0 && slot->publishTotal > 0) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "0 (!)");
                    } else {
                        ImGui::Text("%u", alive);
                    }

                    ImGui::TableNextColumn();
                    ImGui::Text("%u", slot->publishLastFrame);

                    ImGui::TableNextColumn();
                    ImGui::Text("%llu", static_cast<unsigned long long>(slot->publishTotal));
                }

                ImGui::EndTable();
            }
        }

        // ── 直近に流れたイベント ──────────────────────────────────
        if (ImGui::CollapsingHeader("Trace", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Pause", &tracePaused_);
            UI::SameLine();
            if (ImGui::Button("Clear")) {
                traceHead_ = 0;
                traceCount_ = 0;
            }
            UI::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputTextWithHint("##EventBusFilter", "filter", traceFilter_, sizeof(traceFilter_));

            if (ImGui::BeginChild("##EventBusTrace", ImVec2(0.0f, 200.0f), ImGuiChildFlags_Borders)) {
                const bool hasFilter = (traceFilter_[0] != '\0');

                // 新しいものが上に来るように、書き込み位置から遡って表示する
                for (std::size_t i = 0; i < traceCount_; ++i) {
                    const std::size_t index = (traceHead_ + kTraceCapacity - 1 - i) % kTraceCapacity;
                    const TraceEntry& entry = trace_[index];

                    if (hasFilter && entry.name.find(traceFilter_) == std::string::npos) {
                        continue;
                    }

                    ImGui::Text("%8llu  %-9s  x%u  %s",
                        static_cast<unsigned long long>(entry.frame),
                        entry.wasQueued ? "[queued]" : "[direct]",
                        entry.listenerCount,
                        entry.name.c_str());
                }
            }
            ImGui::EndChild();
        }
    }
#endif
}
