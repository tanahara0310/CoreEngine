#include "pch.h"
#include "ForceModule.h"
#include "../ParticleSystem.h"
#include "Math/Geometry/Intersect.h"
#include <algorithm>
#include "Editor/ImGui/ImGuiAll.h"

// コンストラクタでデフォルトパラメータを設定

namespace CoreEngine
{
    ForceModule::ForceModule() {
        forceData_.gravity = { 0.0f, -9.8f, 0.0f };
        forceData_.wind = { 0.0f, 0.0f, 0.0f };
        forceData_.drag = 0.0f;
        forceData_.useAccelerationField = false;
        forceData_.acceleration = { 0.0f, 0.0f, 0.0f };
        forceData_.area = BoundingBox();
    }

    void ForceModule::ApplyForces(Particle& particle, float deltaTime, float gravityModifier) {
        if (!enabled_) {
            return;
        }

        // 重力を適用（gravityModifierを考慮）
        particle.velocity.x += forceData_.gravity.x * gravityModifier * deltaTime;
        particle.velocity.y += forceData_.gravity.y * gravityModifier * deltaTime;
        particle.velocity.z += forceData_.gravity.z * gravityModifier * deltaTime;

        // 風力を適用
        particle.velocity.x += forceData_.wind.x * deltaTime;
        particle.velocity.y += forceData_.wind.y * deltaTime;
        particle.velocity.z += forceData_.wind.z * deltaTime;

        // 抵抗力を適用
        if (forceData_.drag > 0.0f) {
            float dragFactor = 1.0f - (forceData_.drag * deltaTime);
            dragFactor = (std::max)(0.0f, dragFactor); // 負の値にならないように

            particle.velocity.x *= dragFactor;
            particle.velocity.y *= dragFactor;
            particle.velocity.z *= dragFactor;
        }

        // 加速度フィールドを適用
        if (forceData_.useAccelerationField) {
            if (Geometry::Contains(forceData_.area, particle.transform.translate)) {
                particle.velocity.x += forceData_.acceleration.x * deltaTime;
                particle.velocity.y += forceData_.acceleration.y * deltaTime;
                particle.velocity.z += forceData_.acceleration.z * deltaTime;
            }
        }
    }

#ifdef USE_IMGUI
    bool ForceModule::ShowImGui() {
        bool changed = false;

        changed |= UI::DragVec3("重力", forceData_.gravity, 0.1f);
        UI::SameLine();
        UI::HelpMarker("重力加速度（既定は (0, -9.8, 0)）。\n実際の効き方はメインモジュールの「重力係数」との掛け算で決まります。\n重力係数が 0 のままだと効きません。");

        changed |= UI::DragVec3("風", forceData_.wind, 0.1f);
        UI::SameLine();
        UI::HelpMarker("常に加わる加速度。重力係数の影響を受けません。");

        changed |= UI::DragFloat("空気抵抗", forceData_.drag, 0.01f, 0.0f, 1.0f);
        UI::SameLine();
        UI::HelpMarker("速度を毎秒この割合だけ減衰させます。0 で減衰なし。");

        UI::SectionHeader("加速フィールド");
        changed |= UI::Widgets::ToggleSwitch("加速フィールドを使用", &forceData_.useAccelerationField);
        UI::Tooltip("指定した箱型エリア内にいるパーティクルにのみ追加の加速度をかけます");

        if (forceData_.useAccelerationField) {
            changed |= UI::DragVec3("加速度", forceData_.acceleration, 0.1f);
            changed |= UI::DragVec3("エリア最小", forceData_.area.min, 0.1f);
            changed |= UI::DragVec3("エリア最大", forceData_.area.max, 0.1f);
        }

        return changed;
    }
#endif
}
