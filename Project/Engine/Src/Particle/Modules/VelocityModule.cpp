#include "pch.h"
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

    UI::Hint("パーティクルが飛んでいく「方向」を決めます（速さはメインの「速度」で設定）");

    // 方向モード選択
    if (ImGui::RadioButton("ランダム方向", velocityData_.useRandomDirection)) {
        if (!velocityData_.useRandomDirection) {
            velocityData_.useRandomDirection = true;
            changed = true;
        }
    }
    UI::SameLine();
    if (ImGui::RadioButton("指定方向", !velocityData_.useRandomDirection)) {
        if (velocityData_.useRandomDirection) {
            velocityData_.useRandomDirection = false;
            changed = true;
        }
    }

    if (velocityData_.useRandomDirection) {
        // ランダムモード: 軸ごとの広がりのみ意味を持つ
        changed |= UI::DragVec3("軸ごとの広がり", velocityData_.randomSpeedRange, 0.1f, 0.0f, 5.0f);
        UI::SameLine();
        UI::HelpMarker("ランダム方向に各軸±この値の偏りを加えます。\n例: Yだけ大きくすると上下方向へ飛びやすくなります。\n(0,0,0) で全方向に均等。");
    } else {
        // 指定方向モード
        if (UI::DragVec3("方向", velocityData_.startSpeed, 0.01f, -1.0f, 1.0f)) {
            changed = true;
        }
        UI::SameLine();
        UI::HelpMarker("全パーティクルがこの方向へ飛びます。\n長さは自動的に1に正規化されるため、比率だけが意味を持ちます。");

        Vector3 normalized = Vector::Normalize(velocityData_.startSpeed);
        UI::HintF("正規化後: (%.2f, %.2f, %.2f)", normalized.x, normalized.y, normalized.z);
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
