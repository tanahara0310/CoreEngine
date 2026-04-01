#include "VelocityModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要

namespace CoreEngine
{

using namespace CoreEngine::MathCore;

VelocityModule::VelocityModule() {
    velocityData_.startSpeed = { 0.0f, 1.0f, 0.0f };
    velocityData_.randomSpeedRange = { 1.0f, 1.0f, 1.0f };
    velocityData_.useRandomDirection = true;
}

void VelocityModule::ApplyInitialVelocity(Particle& particle) {
    if (!enabled_) {
        particle.velocity = { 0.0f, 1.0f, 0.0f };  // デフォルトは上向き
        return;
    }

    // 注意: 速度の"大きさ"はMainModuleのstartSpeedで設定されます
    // このモジュールは速度の"方向"のみを決定します

    Vector3 direction = velocityData_.startSpeed;

    if (velocityData_.useRandomDirection) {
        direction = GenerateRandomDirection();
        
        // ランダム方向の場合のみ、ランダム範囲を方向に適用（方向の揺らぎ）
        if (velocityData_.randomSpeedRange.x > 0.0f || 
            velocityData_.randomSpeedRange.y > 0.0f || 
            velocityData_.randomSpeedRange.z > 0.0f) {
            
            Vector3 randomOffset = {
                random_.GetFloat(-velocityData_.randomSpeedRange.x, velocityData_.randomSpeedRange.x),
                random_.GetFloat(-velocityData_.randomSpeedRange.y, velocityData_.randomSpeedRange.y),
                random_.GetFloat(-velocityData_.randomSpeedRange.z, velocityData_.randomSpeedRange.z)
            };

            direction.x += randomOffset.x;
            direction.y += randomOffset.y;
            direction.z += randomOffset.z;
        }
    }

    // 方向ベクトルを正規化（MathCore関数を使用）
    direction = Vector::Normalize(direction);

    // 方向ベクトルを設定（大きさは1.0に正規化済み）
    // MainModuleのstartSpeedが後で掛けられるので、ここでは方向のみを設定
    particle.velocity = direction;
}

#ifdef USE_IMGUI
bool VelocityModule::ShowImGui() {
    bool changed = false;

    // 有効/無効の切り替え
    if (UI::Widgets::ToggleSwitch("有効##速度", &enabled_)) {
        changed = true;
    }

    if (!enabled_) {
        ImGui::BeginDisabled();
    }

    UI::Hint("注意: 速度の大きさはMainModuleのstartSpeedで設定してください");
    ImGui::Text("このモジュールは速度の方向のみを決定します。");
    UI::Separator();

    // 速度方向の編集（-1.0 ～ 1.0 に制限）
    if (UI::DragVec3("速度方向", velocityData_.startSpeed, 0.01f, -1.0f, 1.0f)) {
        changed = true;
    }
    UI::Hint("（自動的に正規化されます）");

    // 正規化後の値を表示（読み取り専用）
    Vector3 normalized = Vector::Normalize(velocityData_.startSpeed);
    float length = Vector::Length(velocityData_.startSpeed);
    
    // 色付きで表示（正規化されていることを強調）
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), 
           "正規化後: (%.3f, %.3f, %.3f)", 
        normalized.x, normalized.y, normalized.z);
    UI::Hint("実際にパーティクルに適用される方向（大きさ1.0）");

    // 大きさを表示
    ImGui::Text("入力ベクトルの大きさ: %.3f", length);
    UI::Hint("正規化により1.0になります");

    UI::Separator();
    ImGui::Text("ランダム設定");
    
    changed |= UI::DragVec3("方向ランダム範囲", velocityData_.randomSpeedRange, 0.1f, 0.0f, 5.0f);
    UI::Hint("方向に揺らぎを加えます（正規化後に適用）");
    
    changed |= UI::Widgets::ToggleSwitch("完全ランダム方向", &velocityData_.useRandomDirection);
    UI::Hint("ONにすると、設定した方向を無視してランダムな方向になります");

    if (!enabled_) {
        ImGui::EndDisabled();
    }

    return changed;
}
#endif

Vector3 VelocityModule::GenerateRandomDirection() {
    Vector3 direction = {
        random_.GetFloatSigned(),
        random_.GetFloatSigned(),
        random_.GetFloatSigned()
    };

    // 正規化（MathCore関数を使用）
    return Vector::Normalize(direction);
}
}
