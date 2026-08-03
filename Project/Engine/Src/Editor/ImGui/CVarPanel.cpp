#include "pch.h"
#include "CVarPanel.h"

#ifdef USE_IMGUI

#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Editor/ImGui/Wrappers/ImGuiInput.h"
#include "Editor/ImGui/Wrappers/ImGuiLayout.h"
#include <algorithm>
#include <imgui.h>
#include <string>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        /// @brief 範囲未指定の数値をドラッグ操作するときの速度
        constexpr float kDefaultDragSpeed = 0.01f;

        /// @brief ドット区切り名の depth 番目のセグメントを取得する
        /// @return セグメントが存在しない場合は空
        std::string_view SegmentAt(std::string_view name, size_t depth)
        {
            size_t begin = 0;
            for (size_t i = 0; i < depth; ++i) {
                const size_t dot = name.find('.', begin);
                if (dot == std::string_view::npos) {
                    return {};
                }
                begin = dot + 1;
            }
            const size_t dot = name.find('.', begin);
            return dot == std::string_view::npos
                ? name.substr(begin)
                : name.substr(begin, dot - begin);
        }

        /// @brief ドット区切り名のセグメント数
        size_t SegmentCount(std::string_view name)
        {
            if (name.empty()) {
                return 0;
            }
            return static_cast<size_t>(std::count(name.begin(), name.end(), '.')) + 1;
        }

        /// @brief 先頭から depth 番目のセグメントまでのパスを取得する
        /// @details ツリーノードの ID を一意にするために使う（"r.Vignette" など）
        std::string_view PathUpTo(std::string_view name, size_t depth)
        {
            size_t pos = 0;
            for (size_t i = 0; i <= depth; ++i) {
                const size_t dot = name.find('.', pos);
                if (dot == std::string_view::npos) {
                    return name;
                }
                if (i == depth) {
                    return name.substr(0, dot);
                }
                pos = dot + 1;
            }
            return name;
        }

        /// @brief 説明とフルネームのツールチップを直前の項目に付ける
        void DrawTooltip(const ICVar* cvar)
        {
            if (!ImGui::IsItemHovered()) {
                return;
            }
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(cvar->GetName());
            if (cvar->GetDescription()[0] != '\0') {
                ImGui::Separator();
                ImGui::TextUnformatted(cvar->GetDescription());
            }
            ImGui::EndTooltip();
        }

        /// @brief 右クリックメニュー（デフォルトへ戻す）
        /// @return 値が変更された場合 true
        bool DrawContextMenu(ICVar* cvar, const char* popupId)
        {
            bool changed = false;
            if (ImGui::BeginPopupContextItem(popupId)) {
                if (ImGui::MenuItem("デフォルトに戻す", nullptr, false, cvar->IsModified())) {
                    cvar->ResetToDefault();
                    changed = true;
                }
                ImGui::EndPopup();
            }
            return changed;
        }
    }

    bool CVarUI::DrawWidget(ICVar* cvar)
    {
        if (!cvar || HasFlag(cvar->GetFlags(), CVarFlags::NoUI)) {
            return false;
        }

        const std::string_view fullName = cvar->GetName();
        const std::string_view leaf = SegmentAt(fullName, SegmentCount(fullName) - 1);

        // 表示は末尾セグメントのみ。ImGui の ID 衝突を避けるため "##フルネーム" を付ける
        const std::string label = std::string(leaf) + "##" + std::string(fullName);
        const CVarRange range = cvar->GetRange();

        // デフォルトから変更されている項目は色を変えて「触った箇所」を一目で分かるようにする
        const bool modified = cvar->IsModified();
        if (modified) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
        }

        bool changed = false;
        switch (cvar->GetType())
        {
        case CVarType::Bool:
            changed = ImGui::Checkbox(label.c_str(), cvar->AsBool());
            break;

        case CVarType::Int:
            if (range.valid) {
                changed = ImGui::SliderInt(label.c_str(), cvar->AsInt(),
                                           static_cast<int>(range.min), static_cast<int>(range.max));
            } else {
                changed = ImGui::DragInt(label.c_str(), cvar->AsInt());
            }
            break;

        case CVarType::Float:
            if (range.valid) {
                changed = ImGui::SliderFloat(label.c_str(), cvar->AsFloat(), range.min, range.max, "%.3f");
            } else {
                changed = ImGui::DragFloat(label.c_str(), cvar->AsFloat(), kDefaultDragSpeed);
            }
            break;

        case CVarType::Vector2:
            if (range.valid) {
                changed = ImGui::SliderFloat2(label.c_str(), &cvar->AsVector2()->x, range.min, range.max, "%.3f");
            } else {
                changed = ImGui::DragFloat2(label.c_str(), &cvar->AsVector2()->x, kDefaultDragSpeed);
            }
            break;

        case CVarType::Vector3:
            if (range.valid) {
                changed = ImGui::SliderFloat3(label.c_str(), &cvar->AsVector3()->x, range.min, range.max, "%.3f");
            } else {
                changed = ImGui::DragFloat3(label.c_str(), &cvar->AsVector3()->x, kDefaultDragSpeed);
            }
            break;

        case CVarType::Color:
            changed = UI::ColorEdit(label.c_str(), *cvar->AsColor());
            break;
        }

        if (modified) {
            ImGui::PopStyleColor();
        }

        DrawTooltip(cvar);
        // ウィジェットと同じ ID でコンテキストメニューを開く
        changed |= DrawContextMenu(cvar, label.c_str());

        if (changed) {
            // ImGui はストレージを直接書き換えるため、通番の更新と通知はここで行う
            cvar->NotifyChanged();
        }
        return changed;
    }

    namespace
    {
        /// @brief ソート済み CVar 列を depth 段目のセグメントでグループ化して再帰描画する
        /// @param items ドット区切り名で昇順ソートされた CVar 列
        /// @param depth 現在のツリー階層（この段のセグメントでグループ化する）
        bool DrawGroup(const std::vector<ICVar*>& items, size_t depth)
        {
            bool anyChanged = false;

            size_t i = 0;
            while (i < items.size()) {
                ICVar* cvar = items[i];
                const std::string_view name = cvar->GetName();

                // この段が最終セグメント = 葉なのでウィジェットを描く
                if (SegmentCount(name) <= depth + 1) {
                    anyChanged |= CVarUI::DrawWidget(cvar);
                    ++i;
                    continue;
                }

                // 同じセグメントを持つ範囲を 1 つのツリーノードにまとめる
                const std::string_view group = SegmentAt(name, depth);
                size_t end = i;
                while (end < items.size() && SegmentAt(items[end]->GetName(), depth) == group) {
                    ++end;
                }

                // ルートからこのグループまでのパスを ID にして、別カテゴリの同名グループと衝突させない
                const std::string nodeLabel =
                    std::string(group) + "##cvargroup_" + std::string(PathUpTo(name, depth));

                if (ImGui::TreeNodeEx(nodeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    const std::vector<ICVar*> sub(items.begin() + i, items.begin() + end);
                    anyChanged |= DrawGroup(sub, depth + 1);
                    ImGui::TreePop();
                }
                i = end;
            }

            return anyChanged;
        }
    }

    bool CVarUI::DrawTree(std::string_view prefix)
    {
        std::vector<ICVar*> items = CVarRegistry::Get().GetByPrefix(prefix);

        // NoUI（別の UI が担当する項目・コンソール専用）を除外
        items.erase(std::remove_if(items.begin(), items.end(), [](const ICVar* cvar) {
            return HasFlag(cvar->GetFlags(), CVarFlags::NoUI);
        }), items.end());

        if (items.empty()) {
            ImGui::TextDisabled("該当する項目はありません");
            return false;
        }

        // prefix で絞った場合、その分の階層はツリーに出さない（"r.Vignette" 指定なら
        // "r" > "Vignette" のノードを重ねて表示しても情報量が無いため）
        const size_t depth = prefix.empty() ? 0 : SegmentCount(prefix);
        return DrawGroup(items, depth);
    }

    void CVarUI::ResetTree(std::string_view prefix)
    {
        for (ICVar* cvar : CVarRegistry::Get().GetByPrefix(prefix)) {
            cvar->ResetToDefault();
        }
    }

}

#endif // USE_IMGUI
