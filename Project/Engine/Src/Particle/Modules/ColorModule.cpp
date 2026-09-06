#include "pch.h"
#include "ColorModule.h"
#include "../ParticleSystem.h" // Particle構造体のために必要
#include "Math/MathCore.h"
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
        const float lifeRatio = MathCore::Saturate(particle.currentTime / particle.lifeTime);

        // startRatio より手前は開始色のまま。そこから寿命の終わりまでで変化させる
        const float start = std::clamp(colorData_.startRatio, 0.0f, 0.999f);
        const float t = MathCore::Saturate((lifeRatio - start) / (1.0f - start));

        // particle.initialColorを開始色として使用
        particle.color = MathCore::Lerp(particle.initialColor, colorData_.endColor, t);
    }

#ifdef USE_IMGUI
    bool ColorModule::ShowImGui() {
        bool changed = false;

        changed |= UI::Widgets::ToggleSwitch("寿命に応じて色を変化", &colorData_.useGradient);
        UI::Tooltip("開始色（メインの「色」）から終了色へ、寿命に沿って線形補間します");

        {
            UI::Scope::DisabledScope ds(!colorData_.useGradient);
            changed |= UI::ColorEdit("終了色", colorData_.endColor);
            UI::SameLine();
            UI::HelpMarker("寿命が尽きる瞬間の色。\nアルファを0にするとフェードアウトになります。\n開始色はメインモジュールの「色」です。");

            changed |= UI::SliderFloat("変化を始める割合", colorData_.startRatio, 0.0f, 0.99f, "%.2f");
            UI::SameLine();
            UI::HelpMarker("寿命のどこから色を変え始めるか。\n0 で生まれた瞬間から、0.7 なら最後の 30% だけで変化します。\n消え際だけフェードさせたいときに上げます。");
        }

        return changed;
    }
#endif

}
