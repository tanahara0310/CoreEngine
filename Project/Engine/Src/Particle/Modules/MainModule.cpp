#include "pch.h"
#include "MainModule.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Math/MathCore.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif


namespace CoreEngine
{
MainModule::MainModule() {
    // デフォルト値を設定
    mainData_.duration = 5.0f;
    mainData_.looping = true;
    mainData_.playOnAwake = true;
    mainData_.maxParticles = 1000;
    mainData_.simulationSpace = SimulationSpace::World;

    mainData_.startLifetime = 1.0f;
    mainData_.startLifetimeRandomness = 0.0f;

    mainData_.startSpeed = 5.0f;
    mainData_.startSpeedRandomness = 0.0f;

    mainData_.startSize = { 1.0f, 1.0f, 1.0f };
    mainData_.startSizeRandomness = 0.0f;

    mainData_.startRotation = { 0.0f, 0.0f, 0.0f };
    mainData_.startRotationRandomness = 0.0f;

    mainData_.startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    mainData_.startColorRandomness = 0.0f;

    mainData_.gravityModifier = 0.0f;
}

void MainModule::Play() {
    isPlaying_ = true;
}

void MainModule::Stop() {
    isPlaying_ = false;
}

void MainModule::Restart() {
    elapsedTime_ = 0.0f;
    isPlaying_ = true;
}

void MainModule::UpdateTime(float deltaTime) {
    if (!enabled_) {
        return;
    }

    // 再生中のみ時間更新
    if (isPlaying_) {
        elapsedTime_ += deltaTime;

        // ループ処理
        if (mainData_.looping && elapsedTime_ >= mainData_.duration) {
            elapsedTime_ = 0.0f;  // 時間をリセット
        }

        // ループしない場合、持続時間を超えたら停止
        if (!mainData_.looping && elapsedTime_ >= mainData_.duration) {
            isPlaying_ = false;
        }
    }
}

bool MainModule::IsFinished() const {
    if (mainData_.looping) {
        return false;  // ループする場合は終了しない
    }
    return !isPlaying_ && elapsedTime_ >= mainData_.duration;
}

float MainModule::GenerateStartLifetime() const {
    return ApplyRandomness(mainData_.startLifetime, mainData_.startLifetimeRandomness);
}

float MainModule::GenerateStartSpeed() const {
    return ApplyRandomness(mainData_.startSpeed, mainData_.startSpeedRandomness);
}

Vector3 MainModule::GenerateStartSize() const {
    float randomness = mainData_.startSizeRandomness;
    return {
        ApplyRandomness(mainData_.startSize.x, randomness),
     ApplyRandomness(mainData_.startSize.y, randomness),
        ApplyRandomness(mainData_.startSize.z, randomness)
    };
}

Vector3 MainModule::GenerateStartRotation() const {
    float randomness = mainData_.startRotationRandomness;
    // 度からラジアンに変換
    constexpr float kDegToRad = MathCore::Constants::kDegToRad;
    return {
 ApplyRandomness(mainData_.startRotation.x, randomness) * kDegToRad,
        ApplyRandomness(mainData_.startRotation.y, randomness) * kDegToRad,
        ApplyRandomness(mainData_.startRotation.z, randomness) * kDegToRad
    };
}

Vector4 MainModule::GenerateStartColor() const {
    float randomness = mainData_.startColorRandomness;
    return {
        ApplyRandomness(mainData_.startColor.x, randomness),
  ApplyRandomness(mainData_.startColor.y, randomness),
        ApplyRandomness(mainData_.startColor.z, randomness),
      ApplyRandomness(mainData_.startColor.w, randomness)
    };
}

float MainModule::ApplyRandomness(float base, float randomness) const {
    if (randomness <= 0.0f) {
        return base;
    }

    // -randomness ～ +randomness の範囲でランダム値を生成
    float range = base * randomness;
    float randomValue = random_.GetFloat(-range, range);
    float result = base + randomValue;
    return (result < 0.0f) ? 0.0f : result;  // 負の値を防ぐ
}

#ifdef USE_IMGUI
bool MainModule::ShowImGui() {
    bool changed = false;

    // ===== システム =====
    UI::SectionHeader("システム");

    changed |= UI::DragFloat("継続時間（秒）", mainData_.duration, 0.1f, 0.1f, 60.0f, "%.1f");
    UI::SameLine();
    UI::HelpMarker(
        "エミッターが1サイクル動作する時間。\n"
        "ループONの場合はこの時間ごとに繰り返します。\n"
        "個々の粒の寿命は下の「寿命」で設定します。\n"
        "注意: バースト時刻がこの時間を超えていると、継続時間の終了時に強制発火します。");

    changed |= UI::Widgets::ToggleSwitch("ループ", &mainData_.looping);
    UI::Tooltip("継続時間の終了後、自動的に最初から繰り返します");

    changed |= UI::Widgets::ToggleSwitch("起動時に再生", &mainData_.playOnAwake);
    UI::Tooltip("シーン開始時に自動的に再生を始めます");

    {
        int maxParticles = static_cast<int>(mainData_.maxParticles);
        const int hardLimit = (capacityLimit_ > 0) ? static_cast<int>(capacityLimit_) : 100000;
        if (ImGui::DragInt("最大パーティクル数", &maxParticles, 10, 1, hardLimit)) {
            mainData_.maxParticles = static_cast<uint32_t>(std::clamp(maxParticles, 1, hardLimit));
            changed = true;
        }
        if (capacityLimit_ > 0) {
            UI::SameLine();
            char help[128];
            std::snprintf(help, sizeof(help),
                "同時に存在できる粒子数の上限。\nこのシステムのバッファ容量は %u です（それ以上は設定しても反映されません）。",
                capacityLimit_);
            UI::HelpMarker(help);
        }
    }

    {
        const char* spaceNames[] = { "ローカル（エミッターに追従）", "ワールド（放出後は独立）" };
        int currentSpace = static_cast<int>(mainData_.simulationSpace);
        if (ImGui::Combo("シミュレーション空間", &currentSpace, spaceNames, IM_ARRAYSIZE(spaceNames))) {
            mainData_.simulationSpace = static_cast<SimulationSpace>(currentSpace);
            changed = true;
        }
    }

    // ===== パーティクル初期値（Unityの Start～ に相当） =====
    UI::SectionHeader("パーティクル初期値");

    changed |= UI::DragFloat("寿命（秒）", mainData_.startLifetime, 0.1f, 0.01f, 10.0f, "%.2f");
    UI::SameLine();
    UI::HelpMarker("粒1つが生成されてから消えるまでの時間。\n上の「継続時間」（エミッター全体の動作時間）とは別の設定です。");
    changed |= UI::SliderFloat("寿命ランダム幅", mainData_.startLifetimeRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::DragFloat("速度", mainData_.startSpeed, 0.1f, 0.0f, 50.0f, "%.1f");
    UI::SameLine();
    UI::HelpMarker("放出時の速さ（大きさ）。飛ぶ方向は「初速方向」モジュールで設定します。");
    changed |= UI::SliderFloat("速度ランダム幅", mainData_.startSpeedRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::DragVec3("サイズ", mainData_.startSize, 0.1f, 0.1f, 10.0f);
    changed |= UI::SliderFloat("サイズランダム幅", mainData_.startSizeRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::DragVec3("回転（度）", mainData_.startRotation, 1.0f, -180.0f, 180.0f);
    changed |= UI::SliderFloat("回転ランダム幅", mainData_.startRotationRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::ColorEdit("色", mainData_.startColor);
    changed |= UI::SliderFloat("色ランダム幅", mainData_.startColorRandomness, 0.0f, 1.0f, "%.2f");

    // ===== 物理 =====
    UI::SectionHeader("物理");
    changed |= UI::DragFloat("重力係数", mainData_.gravityModifier, 0.1f, -2.0f, 2.0f, "%.1f");
    UI::SameLine();
    UI::HelpMarker("「外力」モジュールの重力に掛かる係数。\n0.0 = 重力なし / 1.0 = 通常 / 負の値 = 逆方向（上昇）");

    return changed;
}
#endif
}
