#include "pch.h"
#include "SpawnPopComponent.h"

#include "GameObject/GameObject.h"
#include "Utility/Tween/Tween.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

namespace GameComponents
{
    CVar<float> SpawnPopComponent::SpawnScale{
        "Game.SpawnPop.SpawnScale",
        0.05f,
        "現れ始めるときの倍率。0 に近いほど無から湧いて見える",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> SpawnPopComponent::StretchDuration{
        "Game.SpawnPop.StretchDuration",
        0.18f,
        "縦に伸び上がるまでの時間（秒）",
        CVarRange{ 0.01f, 2.0f } };

    CVar<float> SpawnPopComponent::StretchWidth{
        "Game.SpawnPop.StretchWidth",
        0.70f,
        "伸び上がった瞬間の横方向（XZ）の倍率",
        CVarRange{ 0.1f, 3.0f } };

    CVar<float> SpawnPopComponent::StretchHeight{
        "Game.SpawnPop.StretchHeight",
        1.40f,
        "伸び上がった瞬間の縦方向（Y）の倍率",
        CVarRange{ 0.1f, 3.0f } };

    CVar<float> SpawnPopComponent::SquashDuration{
        "Game.SpawnPop.SquashDuration",
        0.10f,
        "伸びきったあと、潰れるまでの時間（秒）",
        CVarRange{ 0.01f, 2.0f } };

    CVar<float> SpawnPopComponent::SquashWidth{
        "Game.SpawnPop.SquashWidth",
        1.28f,
        "潰れた瞬間の横方向（XZ）の倍率",
        CVarRange{ 0.1f, 3.0f } };

    CVar<float> SpawnPopComponent::SquashHeight{
        "Game.SpawnPop.SquashHeight",
        0.74f,
        "潰れた瞬間の縦方向（Y）の倍率",
        CVarRange{ 0.1f, 3.0f } };

    CVar<float> SpawnPopComponent::SettleDuration{
        "Game.SpawnPop.SettleDuration",
        0.22f,
        "潰れた状態から通常サイズへ跳ね戻る時間（秒）",
        CVarRange{ 0.01f, 2.0f } };
}

#ifdef USE_IMGUI
bool GameComponents::SpawnPopComponent::DrawInspector() {
    ImGui::TextDisabled("現在の係数: %.3f, %.3f, %.3f",
        popScale_.x, popScale_.y, popScale_.z);
    ImGui::TextWrapped("演出の強さと速さは CVar パネルの Game.SpawnPop.* で調整します。");
    if (ImGui::Button("もう一度再生")) {
        PlayPopIn();
    }
    return false;
}
#endif

void GameComponents::SpawnPopComponent::Awake() {
    // GameObject は生成されたフレームにも描画される。Start() まで待つと
    // 等倍のまま1フレーム映ってしまうので、アタッチされた時点で縮めておく。
    popScale_ = Factor(SpawnScale.Get(), SpawnScale.Get());
}

void GameComponents::SpawnPopComponent::Start() {
    PlayPopIn();
}

void GameComponents::SpawnPopComponent::PlayPopIn() {
    popScale_ = Factor(SpawnScale.Get(), SpawnScale.Get());

    // 伸び上がって、着地で潰れて、跳ね戻って収まる。
    // 最後だけ EaseOutBack にして、通常サイズを軽く行き過ぎてから止める。
    TweenSequence()
        .Append(Tween::To(
            &popScale_,
            Factor(StretchWidth.Get(), StretchHeight.Get()),
            StretchDuration.Get())
            .SetEase(EasingUtil::Type::EaseOutCubic))
        .Append(Tween::To(
            &popScale_,
            Factor(SquashWidth.Get(), SquashHeight.Get()),
            SquashDuration.Get())
            .SetEase(EasingUtil::Type::EaseOutQuad))
        .Append(Tween::To(
            &popScale_,
            Vector3{ 1.0f, 1.0f, 1.0f },
            SettleDuration.Get())
            .SetEase(EasingUtil::Type::EaseOutBack))
        .SetLink(GetOwner())
        .SetId("SpawnPop");
}

Vector3 GameComponents::SpawnPopComponent::Factor(
    float widthRate, float heightRate) {
    return { widthRate, heightRate, widthRate };
}
