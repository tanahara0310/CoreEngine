#include "pch.h"
#include "EmissionModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要

#ifdef USE_IMGUI
#include "Editor/ImGui/ImguiManager.h"
#endif


namespace CoreEngine
{
EmissionModule::EmissionModule() {
    emissionData_.rateOverTime = 10;
    emissionData_.burstCount = 0;
    emissionData_.burstTime = 0.0f;
}

uint32_t EmissionModule::CalculateEmissionCount(float deltaTime) {
  if (!enabled_ || !isPlaying_) {
        return 0;
    }

    uint32_t totalCount = 0;

  // Rate over Time による放出
    if (emissionData_.rateOverTime > 0) {
     emissionAccumulator_ += static_cast<float>(emissionData_.rateOverTime) * deltaTime;
  
    if (emissionAccumulator_ >= 1.0f) {
            totalCount += static_cast<uint32_t>(emissionAccumulator_);
        emissionAccumulator_ -= static_cast<float>(static_cast<uint32_t>(emissionAccumulator_));
        }
    }

    // Burst による放出（一度だけ）
  if (!hasBurst_ && emissionData_.burstCount > 0 && elapsedTime_ >= emissionData_.burstTime) {
  totalCount += emissionData_.burstCount;
        hasBurst_ = true;
    }

    return totalCount;
}

void EmissionModule::UpdateTime(float deltaTime) {
    if (!enabled_ || !isPlaying_) {
        return;
    }

    float previousTime = elapsedTime_;
    elapsedTime_ += deltaTime;

    // バーストのリセットチェック（ループ用）
    // elapsedTimeが巻き戻った場合、ループがリセットされたと判断
    if (elapsedTime_ < previousTime) {
        hasBurst_ = false;
        emissionAccumulator_ = 0.0f;
    }
}

void EmissionModule::Play() {
    isPlaying_ = true;
    elapsedTime_ = 0.0f;
    hasBurst_ = false;
    emissionAccumulator_ = 0.0f;
}

void EmissionModule::Stop() {
    isPlaying_ = false;
}

#ifdef USE_IMGUI
bool EmissionModule::ShowImGui() {
    bool changed = false;

    // 継続的な放出
    int rateOverTime = static_cast<int>(emissionData_.rateOverTime);
    if (UI::DragInt("放出レート（個/秒）", rateOverTime, 1, 0, 1000)) {
        emissionData_.rateOverTime = static_cast<uint32_t>(rateOverTime);
        changed = true;
    }
    UI::SameLine();
    UI::HelpMarker("毎秒生成されるパーティクル数。\n実際の生成タイミングはフレームレートにより多少ばらつきます。");

    // バースト（一度に大量放出）
    UI::SectionHeader("バースト");

    int burstCount = static_cast<int>(emissionData_.burstCount);
    if (UI::DragInt("放出数", burstCount, 1, 0, 1000)) {
        emissionData_.burstCount = static_cast<uint32_t>(burstCount);
        changed = true;
    }
    UI::SameLine();
    UI::HelpMarker("指定した時刻に一度だけまとめて放出します。0 でバースト無効。");

    changed |= UI::DragFloat("発生時刻（秒）", emissionData_.burstTime, 0.1f, 0.0f, 60.0f, "%.1f");
    UI::SameLine();
    UI::HelpMarker("サイクル開始からこの時間が経過した時にバーストします。\nループ時はサイクルごとに1回発生します。");

    // ステータス（1行に集約）
    if (emissionData_.burstCount > 0) {
        if (hasBurst_) {
            UI::HintF("経過 %.2f 秒 ・ バースト済み", elapsedTime_);
        } else if (elapsedTime_ < emissionData_.burstTime) {
            UI::HintF("経過 %.2f 秒 ・ バーストまであと %.2f 秒", elapsedTime_, emissionData_.burstTime - elapsedTime_);
        } else {
            UI::HintF("経過 %.2f 秒 ・ バーストは次フレームで発生", elapsedTime_);
        }
    }

    return changed;
}
#endif
}
