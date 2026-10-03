#include "pch.h"
#include "EmissionModule.h"

#ifdef CORE_EDITOR
#include "Editor/ImGui/ImguiManager.h"
#endif


namespace CoreEngine
{
EmissionModule::EmissionModule() {
    emissionData_.rateOverTime = 10;
    emissionData_.burstCount = 0;
    emissionData_.burstTime = 0.0f;
}

#ifdef CORE_EDITOR
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
    UI::HelpMarker("サイクル開始からこの時間が経過した時にバーストします。\nループ時はサイクルごとに1回発生します。\n継続時間より後の時刻は、サイクルの終わりに発生します。");

    return changed;
}
#endif
}
