#include "EmissionModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要

#ifdef _DEBUG
#include "Engine/Utility/Debug/ImGui/ImguiManager.h"
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

#ifdef _DEBUG
bool EmissionModule::ShowImGui() {
    bool changed = false;

  // 有効/無効の切り替え
    if (ImGui::Checkbox("有効##放出", &enabled_)) {
     changed = true;
    }

    if (!enabled_) {
        ImGui::BeginDisabled();
    }

    ImGui::TextDisabled("注意: 持続時間とループ設定はMainModuleで管理されます");
    ImGui::Separator();
    ImGui::Text("放出設定");
    
    // Rate over Time
    int rateOverTime = static_cast<int>(emissionData_.rateOverTime);
    if (ImGui::DragInt("時間あたりの放出数", &rateOverTime, 1, 0, 100)) {
        emissionData_.rateOverTime = static_cast<uint32_t>(rateOverTime);
        changed = true;
    }
    ImGui::TextDisabled("毎秒放出されるパーティクル数");
    
    // 実際の放出タイミングの説明
    if (emissionData_.rateOverTime > 0) {
        float interval = 1.0f / static_cast<float>(emissionData_.rateOverTime);
        ImGui::Text("放出間隔: 約%.3f秒ごとに1個", interval);
        ImGui::TextDisabled("注意: フレームレートにより多少ばらつきます（Unity準拠）");
    }

    ImGui::Separator();
    ImGui::Text("バースト設定");

    // Burst Count
    int burstCount = static_cast<int>(emissionData_.burstCount);
    if (ImGui::DragInt("バースト放出数", &burstCount, 1, 0, 1000)) {
        emissionData_.burstCount = static_cast<uint32_t>(burstCount);
  changed = true;
    }
    ImGui::TextDisabled("一度に大量のパーティクルを放出");

    // Burst Time
    changed |= ImGui::DragFloat("バーストタイミング", &emissionData_.burstTime, 0.1f, 0.0f, 60.0f, "%.1f秒");
    ImGui::TextDisabled("システム開始からこの時間後にバーストが発生");

    // バーストの状態表示
    ImGui::Separator();
    ImGui::Text("バースト状態:");
    if (emissionData_.burstCount > 0) {
        ImGui::Text("  経過時間: %.2f秒 / バーストタイミング: %.2f秒", elapsedTime_, emissionData_.burstTime);
        
    if (hasBurst_) {
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "  ✓ バースト済み");
        } else {
         if (elapsedTime_ < emissionData_.burstTime) {
                float remaining = emissionData_.burstTime - elapsedTime_;
          ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "  ⏳ あと%.2f秒でバースト", remaining);
 } else {
          ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "  ⚠ バーストタイミング到達（次フレームで発生）");
            }
        }
    } else {
        ImGui::TextDisabled("  バースト放出数が0のため無効");
    }

    ImGui::Separator();
    ImGui::Text("現在の状態");
    ImGui::Text("再生中: %s", isPlaying_ ? "はい" : "いいえ");
    ImGui::Text("経過時間: %.2f秒", elapsedTime_);

    if (emissionData_.burstCount > 0) {
        ImGui::Text("バースト済み: %s", hasBurst_ ? "はい" : "いいえ");
    }

    if (!enabled_) {
        ImGui::EndDisabled();
    }

    return changed;
}
#endif
}
