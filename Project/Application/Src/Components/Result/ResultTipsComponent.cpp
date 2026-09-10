#include "pch.h"
#include "ResultTipsComponent.h"

#include "Utility/JsonManager/JsonManager.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Wrappers/ImGuiInput.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

using namespace CoreEngine;

json GameComponents::ResultTipsComponent::OnSerialize() const
{
    json tips = json::array();
    for (const std::string& tip : tips_) {
        tips.push_back(tip);
    }
    return { { "tips", std::move(tips) } };
}

void GameComponents::ResultTipsComponent::OnDeserialize(const json& j)
{
    if (!j.contains("tips") || !j["tips"].is_array()) {
        return;
    }

    tips_.clear();
    for (const auto& tip : j["tips"]) {
        if (tip.is_string()) {
            tips_.push_back(tip.get<std::string>());
        }
    }

#ifdef USE_IMGUI
    SyncEditBuffers();
#endif
}

#ifdef USE_IMGUI
void GameComponents::ResultTipsComponent::SyncEditBuffers()
{
    editBuffers_.resize(tips_.size());
    for (std::size_t index = 0; index < tips_.size(); ++index) {
        auto& buffer = editBuffers_[index];
        const std::size_t length = (std::min)(
            tips_[index].size(), buffer.size() - 1);
        std::memcpy(buffer.data(), tips_[index].data(), length);
        buffer[length] = '\0';
    }
}

bool GameComponents::ResultTipsComponent::DrawInspector()
{
    bool changed = false;
    if (editBuffers_.size() != tips_.size()) {
        SyncEditBuffers();
    }

    ImGui::TextDisabled(
        "上から順番に、リザルトへ入るたび次のTipが表示されます。最後は先頭へ戻ります。\n"
        "変更後はオブジェクトの Save Object で保存してください。");

    int removeIndex = -1;
    for (std::size_t index = 0; index < tips_.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));

        char label[32]{};
        std::snprintf(label, sizeof(label), "Tip %zu", index + 1);
        if (UI::InputText(label, editBuffers_[index].data(), editBuffers_[index].size())) {
            tips_[index] = editBuffers_[index].data();
            changed = true;
        }
        UI::SameLine();
        if (ImGui::SmallButton("削除")) {
            removeIndex = static_cast<int>(index);
        }

        ImGui::PopID();
        if (removeIndex >= 0) {
            break;
        }
    }

    if (removeIndex >= 0) {
        tips_.erase(tips_.begin() + removeIndex);
        editBuffers_.erase(editBuffers_.begin() + removeIndex);
        changed = true;
    }

    if (ImGui::Button("Tipを追加")) {
        tips_.emplace_back();
        editBuffers_.emplace_back();
        changed = true;
    }

    if (tips_.empty()) {
        UI::Hint("Tipsはまだありません。");
    }
    return changed;
}
#endif
