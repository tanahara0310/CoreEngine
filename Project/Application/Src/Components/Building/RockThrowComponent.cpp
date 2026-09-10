#include "pch.h"
#include "RockThrowComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <utility>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

json GameComponents::RockThrowComponent::OnSerialize() const
{
    return {
        { "duration", duration_ },
        { "arcHeight", arcHeight_ },
        { "stoneScale", stoneScale_ },
        { "rotationSpeed", rotationSpeed_ }
    };
}

void GameComponents::RockThrowComponent::OnDeserialize(const json& j)
{
    duration_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "duration", duration_));
    arcHeight_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "arcHeight", arcHeight_));
    stoneScale_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "stoneScale", stoneScale_));
    rotationSpeed_ = JsonManager::SafeGet<float>(j, "rotationSpeed", rotationSpeed_);
}

#ifdef USE_IMGUI
bool GameComponents::RockThrowComponent::DrawInspector()
{
    bool changed = false;
    changed |= ImGui::DragFloat("飛行時間", &duration_, 0.01f, 0.01f, 5.0f);
    changed |= ImGui::DragFloat("放物線の高さ", &arcHeight_, 0.05f, 0.0f, 20.0f);
    changed |= ImGui::DragFloat("石のスケール", &stoneScale_, 0.01f, 0.01f, 5.0f);
    changed |= ImGui::DragFloat("石の回転速度", &rotationSpeed_, 0.1f, -100.0f, 100.0f);
    ImGui::TextDisabled("投石中: %s", isPlaying_ ? "はい" : "いいえ");
    return changed;
}
#endif

void GameComponents::RockThrowComponent::Start()
{
    transform_ = Sibling<TransformComponent>();
    renderer_ = Sibling<MeshRendererComponent>();
    if (!transform_ || !renderer_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "RockThrowComponent: Transform または MeshRenderer が未設定です");
        SetEnabled(false);
        return;
    }
    renderer_->SetEnabled(false);
}

void GameComponents::RockThrowComponent::Update()
{
    if (!isPlaying_ || !transform_ || !renderer_) {
        return;
    }

    const float deltaTime = std::max(0.0f, Time::UnscaledDeltaTime());
    elapsed_ = std::min(elapsed_ + deltaTime, duration_);
    const float progress = std::clamp(elapsed_ / duration_, 0.0f, 1.0f);
    auto& value = transform_->Get();
    value.translate = {
        startPosition_.x + (targetPosition_.x - startPosition_.x) * progress,
        startPosition_.y + (targetPosition_.y - startPosition_.y) * progress +
            arcHeight_ * 4.0f * progress * (1.0f - progress),
        startPosition_.z + (targetPosition_.z - startPosition_.z) * progress
    };
    value.rotate.x += rotationSpeed_ * deltaTime;
    value.rotate.z += rotationSpeed_ * 0.7f * deltaTime;

    if (elapsed_ < duration_) {
        return;
    }

    isPlaying_ = false;
    renderer_->SetEnabled(false);
    auto onImpact = std::move(onImpact_);
    onImpact_ = nullptr;
    if (onImpact) {
        onImpact();
    }
}

bool GameComponents::RockThrowComponent::Play(
    const Vector3& start,
    const Vector3& target,
    std::function<void()> onImpact)
{
    if (isPlaying_ || !transform_ || !renderer_) {
        return false;
    }

    startPosition_ = start;
    targetPosition_ = target;
    onImpact_ = std::move(onImpact);
    elapsed_ = 0.0f;
    isPlaying_ = true;

    auto& value = transform_->Get();
    value.translate = startPosition_;
    value.rotate = { 0.0f, 0.0f, 0.0f };
    value.scale = { stoneScale_, stoneScale_, stoneScale_ };
    renderer_->SetEnabled(true);
    return true;
}
