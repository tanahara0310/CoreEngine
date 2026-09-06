#include "pch.h"
#include "TweenManager.h"

#include "GameObject/GameObject.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{
    TweenManager& TweenManager::GetInstance()
    {
        static TweenManager instance;
        return instance;
    }

    std::pair<std::uint32_t, std::uint32_t> TweenManager::Add(std::unique_ptr<TweenDetail::TweenItem> item)
    {
        if (!item) { return { 0u, 0u }; }

        std::uint32_t index = 0;
        if (!freeList_.empty()) {
            index = freeList_.back();
            freeList_.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }

        Slot& slot = slots_[index];
        slot.item = std::move(item);
        return { index, slot.generation };
    }

    TweenDetail::TweenItem* TweenManager::Resolve(std::uint32_t index, std::uint32_t generation)
    {
        if (generation == 0 || index >= slots_.size()) { return nullptr; }

        Slot& slot = slots_[index];
        if (slot.generation != generation || !slot.item) { return nullptr; }

        // 完了済みのものは「もう無い」扱いにする（掃除が次の Update まで遅れるだけなので）
        if (slot.item->IsFinished()) { return nullptr; }

        return slot.item.get();
    }

    std::unique_ptr<TweenDetail::TweenItem> TweenManager::Detach(std::uint32_t index, std::uint32_t generation)
    {
        if (generation == 0 || index >= slots_.size()) { return nullptr; }

        Slot& slot = slots_[index];
        if (slot.generation != generation || !slot.item) { return nullptr; }

        // 自分自身の Advance の途中で持ち去られると、戻り先が解放済みになる
        if (slot.item.get() == advancingItem_) {
            Logger::GetInstance().Log(
                "TweenManager: 更新中のトゥイーンを Sequence へ移そうとしたため拒否しました",
                LogLevel::Error, LogCategory::System);
            return nullptr;
        }

        std::unique_ptr<TweenDetail::TweenItem> detached = std::move(slot.item);
        ReleaseSlot(index);
        return detached;
    }

    void TweenManager::BumpGeneration(std::size_t index)
    {
        std::uint32_t& generation = slots_[index].generation;
        ++generation;
        if (generation == 0) { generation = 1; } // 0 は無効値なので飛ばす
    }

    void TweenManager::ReleaseSlot(std::size_t index)
    {
        slots_[index].item.reset();
        BumpGeneration(index);
        freeList_.push_back(static_cast<std::uint32_t>(index));
    }

    void TweenManager::Update()
    {
        const float scaledDelta = Time::DeltaTime();
        const float unscaledDelta = Time::UnscaledDeltaTime();

        // このフレームに存在していた分だけを進める。
        // コールバックから生成されたトゥイーンは次フレームから動き出す（開始が 1 フレーム内で
        // 二重に進まないようにするため）。
        const std::size_t count = slots_.size();

        updating_ = true;

        for (std::size_t i = 0; i < count; ++i) {
            TweenDetail::TweenItem* item = slots_[i].item.get();
            if (!item) { continue; }

            // link 先が死んでいたら、値を書かずに終わらせる。
            // GameObject の実体解放は CleanupDestroyed の 1 フレーム後なので、
            // ここで IsMarkedForDestroy を見れば解放済みメモリには触れない。
            if (item->settings.link != nullptr && item->settings.link->IsMarkedForDestroy()) {
                item->KillSilently();
            }

            advancingItem_ = item;
            const bool finished = item->Advance(scaledDelta, unscaledDelta);
            advancingItem_ = nullptr;

            if (finished) {
                // ここで解放すると、コールバックが確保し直したスロットを同じフレームで
                // 走査してしまう。解放は走査後にまとめて行う。
                pendingFree_.push_back(static_cast<std::uint32_t>(i));
            }
        }

        updating_ = false;

        for (std::uint32_t index : pendingFree_) {
            if (index < slots_.size() && slots_[index].item) {
                ReleaseSlot(index);
            }
        }
        pendingFree_.clear();
    }

    void TweenManager::Clear()
    {
        if (updating_) {
            Logger::GetInstance().Log(
                "TweenManager::Clear() が更新中に呼ばれたため無視しました",
                LogLevel::Error, LogCategory::System);
            return;
        }

        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].item) {
                slots_[i].item.reset();
                BumpGeneration(i);
            }
        }

        freeList_.clear();
        freeList_.reserve(slots_.size());
        for (std::size_t i = slots_.size(); i > 0; --i) {
            freeList_.push_back(static_cast<std::uint32_t>(i - 1));
        }

        pendingFree_.clear();
    }

    int TweenManager::KillById(const std::string& id, bool complete)
    {
        int killed = 0;
        for (Slot& slot : slots_) {
            if (!slot.item || slot.item->IsFinished()) { continue; }
            if (slot.item->settings.id != id) { continue; }

            if (complete) {
                slot.item->CompleteImmediate();
            } else {
                slot.item->KillSilently();
            }
            ++killed;
        }
        return killed;
    }

    int TweenManager::KillByLink(const GameObject* owner, bool complete)
    {
        if (owner == nullptr) { return 0; }

        int killed = 0;
        for (Slot& slot : slots_) {
            if (!slot.item || slot.item->IsFinished()) { continue; }
            if (slot.item->settings.link != owner) { continue; }

            if (complete) {
                slot.item->CompleteImmediate();
            } else {
                slot.item->KillSilently();
            }
            ++killed;
        }
        return killed;
    }

    std::size_t TweenManager::ActiveCount() const
    {
        std::size_t count = 0;
        for (const Slot& slot : slots_) {
            if (slot.item && !slot.item->IsFinished()) { ++count; }
        }
        return count;
    }

#ifdef USE_IMGUI
    void TweenManager::DrawImGui()
    {
        ImGui::Text("Active: %zu   Slots: %zu   Free: %zu",
            ActiveCount(), slots_.size(), freeList_.size());

        UI::SameLine();
        if (ImGui::Button("Kill All")) {
            for (Slot& slot : slots_) {
                if (slot.item) { slot.item->KillSilently(); }
            }
        }

        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##TweenFilter", "filter by id / kind", filter_, sizeof(filter_));

        UI::Separator();

        if (!ImGui::BeginTable("##TweenList", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 260.0f))) {
            return;
        }

        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Loops", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Link", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableHeadersRow();

        const bool hasFilter = (filter_[0] != '\0');

        for (Slot& slot : slots_) {
            if (!slot.item || slot.item->IsFinished()) { continue; }

            TweenDetail::TweenItem& item = *slot.item;

            if (hasFilter) {
                const std::string kind = item.Kind();
                if (item.settings.id.find(filter_) == std::string::npos
                    && kind.find(filter_) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.Kind());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item.settings.id.empty() ? "-" : item.settings.id.c_str());

            ImGui::TableNextColumn();
            ImGui::ProgressBar(item.Progress(), ImVec2(-1.0f, 0.0f));

            ImGui::TableNextColumn();
            if (item.settings.loopCount < 0) {
                ImGui::TextUnformatted("infinite");
            } else {
                ImGui::Text("%d", item.settings.loopCount);
            }

            ImGui::TableNextColumn();
            if (item.settings.link != nullptr) {
                ImGui::TextUnformatted(item.settings.link->GetName().c_str());
            } else {
                // link 無しでポインタを書き換えるトゥイーンは、対象が死ぬと解放済みメモリを踏む
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "none (!)");
            }
        }

        ImGui::EndTable();
    }
#endif
}
