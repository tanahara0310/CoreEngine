#include "pch.h"
#include "MainModule.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Math/MathCore.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/ImguiManager.h"
#include "Editor/Inspector/InspectorLayout.h"
#endif


namespace CoreEngine
{
MainModule::MainModule() {
    // デフォルト値を設定
    data_.duration = 5.0f;
    data_.looping = true;
    data_.playOnAwake = true;
    data_.maxParticles = 1000;
    data_.simulationSpace = SimulationSpace::World;

    data_.startLifetime = 1.0f;
    data_.startLifetimeRandomness = 0.0f;

    data_.startSpeed = 5.0f;
    data_.startSpeedRandomness = 0.0f;

    data_.startSize = { 1.0f, 1.0f, 1.0f };
    data_.startSizeRandomness = 0.0f;

    data_.startRotation = { 0.0f, 0.0f, 0.0f };
    data_.startRotationRandomness = 0.0f;

    data_.startColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    data_.startColorRandomness = 0.0f;

    data_.gravityModifier = 0.0f;
}

float MainModule::GenerateStartLifetime() const {
    return ApplyRandomness(data_.startLifetime, data_.startLifetimeRandomness);
}

float MainModule::GenerateStartSpeed() const {
    return ApplyRandomness(data_.startSpeed, data_.startSpeedRandomness);
}

Vector3 MainModule::GenerateStartSize() const {
    float randomness = data_.startSizeRandomness;
    return {
        ApplyRandomness(data_.startSize.x, randomness),
     ApplyRandomness(data_.startSize.y, randomness),
        ApplyRandomness(data_.startSize.z, randomness)
    };
}

Vector3 MainModule::GenerateStartRotation() const {
    float randomness = data_.startRotationRandomness;
    // 度からラジアンに変換
    constexpr float kDegToRad = MathCore::Constants::kDegToRad;
    return {
 ApplyRandomness(data_.startRotation.x, randomness) * kDegToRad,
        ApplyRandomness(data_.startRotation.y, randomness) * kDegToRad,
        ApplyRandomness(data_.startRotation.z, randomness) * kDegToRad
    };
}

Vector4 MainModule::GenerateStartColor() const {
    float randomness = data_.startColorRandomness;
    return {
        ApplyRandomness(data_.startColor.x, randomness),
  ApplyRandomness(data_.startColor.y, randomness),
        ApplyRandomness(data_.startColor.z, randomness),
      ApplyRandomness(data_.startColor.w, randomness)
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

#ifdef CORE_EDITOR
bool MainModule::ShowImGui() {
    bool changed = false;

    // ===== システム =====
    UI::SectionHeader("システム");

    changed |= UI::DragFloat("継続時間（秒）", data_.duration, 0.1f, 0.1f, 60.0f, "%.1f");
    UI::SameLine();
    UI::HelpMarker(
        "エミッターが1サイクル動作する時間。\n"
        "ループONの場合はこの時間ごとに繰り返します。\n"
        "個々の粒の寿命は下の「寿命」で設定します。\n"
        "注意: バースト時刻がこの時間を超えていると、継続時間の終了時に強制発火します。");

    changed |= UI::Widgets::ToggleSwitch("ループ", &data_.looping);
    UI::Tooltip("継続時間の終了後、自動的に最初から繰り返します");

    changed |= UI::Widgets::ToggleSwitch("起動時に再生", &data_.playOnAwake);
    UI::Tooltip("シーン開始時に自動的に再生を始めます");

    {
        int maxParticles = static_cast<int>(data_.maxParticles);
        const int hardLimit = (capacityLimit_ > 0) ? static_cast<int>(capacityLimit_) : 100000;
        if (ImGui::DragInt("最大パーティクル数", &maxParticles, 10, 1, hardLimit)) {
            data_.maxParticles = static_cast<uint32_t>(std::clamp(maxParticles, 1, hardLimit));
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
        changed |= InspectorLayout::EnumCombo("シミュレーション空間", data_.simulationSpace, spaceNames);
    }

    // ===== パーティクル初期値（Unityの Start～ に相当） =====
    UI::SectionHeader("パーティクル初期値");

    changed |= UI::DragFloat("寿命（秒）", data_.startLifetime, 0.1f, 0.01f, 10.0f, "%.2f");
    UI::SameLine();
    UI::HelpMarker("粒1つが生成されてから消えるまでの時間。\n上の「継続時間」（エミッター全体の動作時間）とは別の設定です。");
    changed |= UI::SliderFloat("寿命ランダム幅", data_.startLifetimeRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::DragFloat("速度", data_.startSpeed, 0.1f, 0.0f, 50.0f, "%.1f");
    UI::SameLine();
    UI::HelpMarker("放出時の速さ（大きさ）。飛ぶ方向は「初速方向」モジュールで設定します。");
    changed |= UI::SliderFloat("速度ランダム幅", data_.startSpeedRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::DragVec3("サイズ", data_.startSize, 0.1f, 0.1f, 10.0f);
    changed |= UI::SliderFloat("サイズランダム幅", data_.startSizeRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::DragVec3("回転（度）", data_.startRotation, 1.0f, -180.0f, 180.0f);
    changed |= UI::SliderFloat("回転ランダム幅", data_.startRotationRandomness, 0.0f, 1.0f, "%.2f");

    changed |= UI::ColorEdit("色", data_.startColor);
    changed |= UI::SliderFloat("色ランダム幅", data_.startColorRandomness, 0.0f, 1.0f, "%.2f");

    // ===== 物理 =====
    UI::SectionHeader("物理");
    changed |= UI::DragFloat("重力係数", data_.gravityModifier, 0.1f, -2.0f, 2.0f, "%.1f");
    UI::SameLine();
    UI::HelpMarker("「外力」モジュールの重力に掛かる係数。\n0.0 = 重力なし / 1.0 = 通常 / 負の値 = 逆方向（上昇）");

    return changed;
}
#endif
}
