#include "pch.h"
#include "ForceModule.h"
#include "Particle/Core/Particle.h"
#include "Math/Geometry/Intersect.h"
#include <algorithm>
#include "Editor/ImGui/ImGuiAll.h"

// コンストラクタでデフォルトパラメータを設定

namespace CoreEngine
{
    ForceModule::ForceModule() {
        data_.gravity = { 0.0f, -9.8f, 0.0f };
        data_.wind = { 0.0f, 0.0f, 0.0f };
        data_.drag = 0.0f;
        data_.useAccelerationField = false;
        data_.acceleration = { 0.0f, 0.0f, 0.0f };
        data_.area = BoundingBox();
    }

    void ForceModule::ApplyForces(Particle& particle, float deltaTime, float gravityModifier) {
        if (!enabled_) {
            return;
        }

        // 重力を適用（gravityModifierを考慮）
        particle.velocity.x += data_.gravity.x * gravityModifier * deltaTime;
        particle.velocity.y += data_.gravity.y * gravityModifier * deltaTime;
        particle.velocity.z += data_.gravity.z * gravityModifier * deltaTime;

        // 風力を適用
        particle.velocity.x += data_.wind.x * deltaTime;
        particle.velocity.y += data_.wind.y * deltaTime;
        particle.velocity.z += data_.wind.z * deltaTime;

        // 抵抗力を適用
        if (data_.drag > 0.0f) {
            float dragFactor = 1.0f - (data_.drag * deltaTime);
            dragFactor = (std::max)(0.0f, dragFactor); // 負の値にならないように

            particle.velocity.x *= dragFactor;
            particle.velocity.y *= dragFactor;
            particle.velocity.z *= dragFactor;
        }

        // 加速度フィールドを適用
        if (data_.useAccelerationField) {
            if (Geometry::Contains(data_.area, particle.transform.translate)) {
                particle.velocity.x += data_.acceleration.x * deltaTime;
                particle.velocity.y += data_.acceleration.y * deltaTime;
                particle.velocity.z += data_.acceleration.z * deltaTime;
            }
        }
    }

#ifdef CORE_EDITOR
    bool ForceModule::ShowImGui() {
        bool changed = false;

        changed |= UI::DragVec3("重力", data_.gravity, 0.1f);
        UI::SameLine();
        UI::HelpMarker("重力加速度（既定は (0, -9.8, 0)）。\n実際の効き方はメインモジュールの「重力係数」との掛け算で決まります。\n重力係数が 0 のままだと効きません。");

        changed |= UI::DragVec3("風", data_.wind, 0.1f);
        UI::SameLine();
        UI::HelpMarker("常に加わる加速度。重力係数の影響を受けません。");

        changed |= UI::DragFloat("空気抵抗", data_.drag, 0.01f, 0.0f, 1.0f);
        UI::SameLine();
        UI::HelpMarker("速度を毎秒この割合だけ減衰させます。0 で減衰なし。");

        UI::SectionHeader("加速フィールド");
        changed |= UI::Widgets::ToggleSwitch("加速フィールドを使用", &data_.useAccelerationField);
        UI::Tooltip("指定した箱型エリア内にいるパーティクルにのみ追加の加速度をかけます");

        if (data_.useAccelerationField) {
            changed |= UI::DragVec3("加速度", data_.acceleration, 0.1f);
            changed |= UI::DragVec3("エリア最小", data_.area.min, 0.1f);
            changed |= UI::DragVec3("エリア最大", data_.area.max, 0.1f);
        }

        return changed;
    }
#endif
}
