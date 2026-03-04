#include "ColorModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要
#include <algorithm>

// コンストラクタでデフォルトパラメータを設定

namespace CoreEngine
{
    ColorModule::ColorModule() {
        colorData_.endColor = { 1.0f, 1.0f, 1.0f, 0.0f };
        colorData_.useGradient = true;
    }

    void ColorModule::UpdateColor(Particle& particle) {
        if (!enabled_ || !colorData_.useGradient) {
            return;
        }

        // ライフタイムに基づいて色を補間
            // MainModuleで設定された初期色（initialColor）から終了色へ補間
        float t = particle.currentTime / particle.lifeTime;
        t = (std::max)(0.0f, (std::min)(1.0f, t)); // 0-1の範囲に制限

        // particle.initialColorを開始色として使用
        particle.color = LerpColor(particle.initialColor, colorData_.endColor, t);
    }

#ifdef _DEBUG
    bool ColorModule::ShowImGui() {
        bool changed = false;

        // 有効/無効の切り替え
        if (ImGui::Checkbox("有効##色", &enabled_)) {
            changed = true;
        }

        if (!enabled_) {
            ImGui::BeginDisabled();
        }

        ImGui::TextDisabled("注意: 初期色はMainModuleで設定してください");
        ImGui::Separator();

        changed |= ImGui::ColorEdit4("終了色", &colorData_.endColor.x);
        changed |= ImGui::Checkbox("グラデーション使用", &colorData_.useGradient);

        if (!enabled_) {
            ImGui::EndDisabled();
        }

        return changed;
    }
#endif

    Vector4 ColorModule::LerpColor(const Vector4& color1, const Vector4& color2, float t) {
        return {
            color1.x + (color2.x - color1.x) * t,
         color1.y + (color2.y - color1.y) * t,
            color1.z + (color2.z - color1.z) * t,
            color1.w + (color2.w - color1.w) * t
        };
    }
}
