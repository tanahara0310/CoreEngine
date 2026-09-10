#include "pch.h"
#include "ResultMonkeyShakeComponent.h"

#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Utility/FrameRate/Time.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif

#include <algorithm>
#include <cmath>

using namespace CoreEngine;

namespace GameComponents
{
    CVar<float> ResultMonkeyShakeComponent::XAmplitude{
        "Result.Monkey.Shake.XAmplitude",
        0.06f,
        "リザルトサルのX軸シェイク幅（ワールド単位）",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> ResultMonkeyShakeComponent::ZAmplitude{
        "Result.Monkey.Shake.ZAmplitude",
        0.05f,
        "リザルトサルのZ軸シェイク幅（ワールド単位）",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> ResultMonkeyShakeComponent::XInterval{
        "Result.Monkey.Shake.XInterval",
        1.15f,
        "リザルトサルのX軸シェイク間隔（秒）",
        CVarRange{ 0.1f, 10.0f } };

    CVar<float> ResultMonkeyShakeComponent::ZInterval{
        "Result.Monkey.Shake.ZInterval",
        1.65f,
        "リザルトサルのZ軸シェイク間隔（秒）",
        CVarRange{ 0.1f, 10.0f } };

    CVar<float> ResultMonkeyShakeComponent::XDuration{
        "Result.Monkey.Shake.XDuration",
        0.28f,
        "リザルトサルのX軸シェイク時間（秒）",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> ResultMonkeyShakeComponent::ZDuration{
        "Result.Monkey.Shake.ZDuration",
        0.34f,
        "リザルトサルのZ軸シェイク時間（秒）",
        CVarRange{ 0.05f, 2.0f } };

    CVar<float> ResultMonkeyShakeComponent::XFrequency{
        "Result.Monkey.Shake.XFrequency",
        34.0f,
        "リザルトサルのX軸シェイク周波数",
        CVarRange{ 1.0f, 100.0f } };

    CVar<float> ResultMonkeyShakeComponent::ZFrequency{
        "Result.Monkey.Shake.ZFrequency",
        27.0f,
        "リザルトサルのZ軸シェイク周波数",
        CVarRange{ 1.0f, 100.0f } };
}

#ifdef USE_IMGUI
bool GameComponents::ResultMonkeyShakeComponent::DrawInspector()
{
    return CoreEngine::CVarUI::DrawTree("Result.Monkey.Shake");
}
#endif

void GameComponents::ResultMonkeyShakeComponent::Start()
{
    auto* owner = GetOwner();
    transform_ = owner ? owner->GetComponent<TransformComponent>() : nullptr;
    if (!transform_) {
        SetEnabled(false);
        return;
    }

    baseTranslation_ = transform_->Translate();

    const float index = static_cast<float>(monkeyIndex_);
    const float xInterval = std::max(0.1f, XInterval.Get());
    const float zInterval = std::max(0.1f, ZInterval.Get());
    xIntervalTimer_ = std::fmod(index * 0.37f, xInterval);
    zIntervalTimer_ = std::fmod(0.42f + index * 0.53f, zInterval);
    xPhase_ = index * 1.73f;
    zPhase_ = 0.65f + index * 2.11f;
}

void GameComponents::ResultMonkeyShakeComponent::Update()
{
    if (!transform_) {
        return;
    }

    const float deltaTime = std::max(0.0f, Time::UnscaledDeltaTime());
    Vector3 translation = baseTranslation_;
    translation.x += UpdateAxis(
        deltaTime,
        std::max(0.0f, XAmplitude.Get()),
        XInterval.Get(),
        XDuration.Get(),
        XFrequency.Get(),
        xIntervalTimer_,
        xBurstTimer_,
        xPhase_);
    translation.z += UpdateAxis(
        deltaTime,
        std::max(0.0f, ZAmplitude.Get()),
        ZInterval.Get(),
        ZDuration.Get(),
        ZFrequency.Get(),
        zIntervalTimer_,
        zBurstTimer_,
        zPhase_);

    transform_->Translate() = translation;
    // TransformComponent の更新後に位置を変えるため、今フレームの描画へ反映する。
    transform_->Get().TransferMatrix();
}

float GameComponents::ResultMonkeyShakeComponent::UpdateAxis(
    float deltaTime,
    float amplitude,
    float interval,
    float duration,
    float frequency,
    float& intervalTimer,
    float& burstTimer,
    float phase)
{
    if (amplitude <= 0.0f || interval <= 0.0f || duration <= 0.0f || frequency <= 0.0f) {
        burstTimer = -1.0f;
        return 0.0f;
    }

    intervalTimer += deltaTime;
    if (burstTimer < 0.0f && intervalTimer >= interval) {
        intervalTimer = std::fmod(intervalTimer, interval);
        burstTimer = 0.0f;
    }

    if (burstTimer < 0.0f) {
        return 0.0f;
    }

    burstTimer += deltaTime;
    const float progress = std::clamp(burstTimer / duration, 0.0f, 1.0f);
    const float envelope = std::sin(progress * 3.14159265358979323846f);
    const float offset = std::sin(phase + burstTimer * frequency) * amplitude * envelope;
    if (burstTimer >= duration) {
        burstTimer = -1.0f;
        return 0.0f;
    }
    return offset;
}
