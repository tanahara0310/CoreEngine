#include "pch.h"
#include "SpriteAnimatorInspector.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/ImGuiAll.h"
#include "GameObject/Component/Render/SpriteAnimatorComponent.h"
#include "GameObject/Component/Render/SpriteRendererComponent.h"
#include "GameObject/GameObject.h"

#include <string>

namespace CoreEngine
{
namespace SpriteAnimatorInspector
{
    namespace
    {
        /// @brief 1 本ぶんの編集 UI
        /// @param removeRequested 削除のボタンを押したら true にする
        bool DrawOne(SpriteAnimationClip& clip, int index, bool& removeRequested)
        {
            bool changed = false;
            ImGui::PushID(index);

            const std::string header = "[" + std::to_string(index) + "] "
                + (clip.name.empty() ? std::string("（名前なし）") : clip.name);
            if (ImGui::TreeNodeEx("##clip", ImGuiTreeNodeFlags_DefaultOpen, "%s", header.c_str())) {
                changed |= UI::InputString("名前", clip.name);
                changed |= UI::DragInt("最初のコマ", clip.firstFrame, 0.1f, 0, 65535);
                changed |= UI::DragInt("最後のコマ", clip.lastFrame, 0.1f, 0, 65535);
                changed |= UI::DragFloat("fps", clip.fps, 0.1f, 0.0f, 120.0f, "%.1f");
                changed |= ImGui::Checkbox("ループ", &clip.loop);
                if (ImGui::SmallButton("このクリップを削除")) {
                    removeRequested = true;
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
            return changed;
        }
    }

    bool Draw(SpriteAnimatorComponent& animator)
    {
        bool changed = false;

        UI::SectionHeader("クリップ");

        // コマの番号はスプライトのコマ割りで数える
        const GameObject* const owner = animator.GetOwner();
        const auto* const sprite = owner ? owner->GetComponent<SpriteRendererComponent>() : nullptr;
        if (sprite) {
            UI::HintF("コマは 0〜%d（スプライトの横 %d × 縦 %d）", sprite->GetFrameCount() - 1,
                sprite->GetColumns(), sprite->GetRows());
        } else {
            UI::Hint("スプライト描画が無いので、コマを進める先がありません");
        }

        std::vector<SpriteAnimationClip>& clips = animator.GetClips();
        int removeIndex = -1;
        for (int index = 0; index < static_cast<int>(clips.size()); ++index) {
            bool removeRequested = false;
            changed |= DrawOne(clips[static_cast<size_t>(index)], index, removeRequested);
            if (removeRequested) {
                removeIndex = index;
            }
        }
        if (removeIndex >= 0) {
            clips.erase(clips.begin() + removeIndex);
            changed = true;
        }
        if (clips.empty()) {
            UI::Hint("クリップがありません");
        }

        UI::Separator();
        if (ImGui::Button("クリップを追加")) {
            SpriteAnimationClip clip;
            clip.name = "Clip" + std::to_string(clips.size());
            clip.lastFrame = sprite ? (sprite->GetFrameCount() - 1) : 0;
            clips.push_back(std::move(clip));
            changed = true;
        }
        UI::Hint("スクリプトからは owner.spriteAnimator.Play(\"名前\") で流す。");
        return changed;
    }
}
}

#endif // CORE_EDITOR
