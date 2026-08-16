#include "pch.h"
#include "LightDebugVisualizer.h"

#include "LightManager.h"
#include "Math/MathCore.h"
#include "Graphics/Line/LineManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef _DEBUG
#include "Editor/ImGui/ImGuiAll.h"
#endif

namespace CoreEngine
{
    namespace
    {
        constexpr float kPi = MathCore::Constants::kPi;

        const char* GetTypeLabel(LightType type)
        {
            switch (type) {
            case LightType::Directional: return "Directional";
            case LightType::Point:       return "Point";
            case LightType::Spot:        return "Spot";
            case LightType::Area:        return "Area";
            }
            return "Unknown";
        }

#ifdef _DEBUG
        // ==================== ギズモ描画ヘルパー ====================
        // 注意: LineManager::DrawLine の第4引数は「太さ」ではなく「透明度(alpha)」。
        // 旧実装は太さのつもりで 0.2〜0.3 を渡しており、ギズモの大半が
        // ほぼ透明で描かれて視認できなかった（見づらさの根本原因）。

        /// @brief 方向ベクトルに直交する正規直交基底を作る
        void MakePerpBasis(const Vector3& dir, Vector3& outP1, Vector3& outP2)
        {
            Vector3 up = { 0.0f, 1.0f, 0.0f };
            if (std::abs(CoreEngine::Dot(dir, up)) > 0.99f) {
                up = { 1.0f, 0.0f, 0.0f };
            }
            outP1 = CoreEngine::Normalize(CoreEngine::Cross(dir, up));
            outP2 = CoreEngine::Cross(dir, outP1);
        }

        /// @brief 矢じり付きの矢印を描く（UE のライト方向表示風）
        void DrawArrow(LineManager& lm, const Vector3& from, const Vector3& to,
                       const Vector3& color, float alpha)
        {
            const Vector3 delta = to - from;
            const float len = std::sqrt(CoreEngine::Dot(delta, delta));
            if (len < 1e-4f) return;
            const Vector3 dir = delta * (1.0f / len);

            lm.DrawLine(from, to, color, alpha);

            Vector3 p1{}, p2{};
            MakePerpBasis(dir, p1, p2);
            const float head = std::min(0.35f, len * 0.25f);
            const Vector3 base = to - dir * head;
            lm.DrawLine(to, base + p1 * (head * 0.5f), color, alpha);
            lm.DrawLine(to, base - p1 * (head * 0.5f), color, alpha);
            lm.DrawLine(to, base + p2 * (head * 0.5f), color, alpha);
            lm.DrawLine(to, base - p2 * (head * 0.5f), color, alpha);
        }

        /// @brief 任意向きの円を描く（DrawCircle は XZ 平面固定のため自前で描く）
        void DrawOrientedCircle(LineManager& lm, const Vector3& center,
                                const Vector3& axis1, const Vector3& axis2, float radius,
                                int segments, const Vector3& color, float alpha)
        {
            for (int j = 0; j < segments; ++j)
            {
                const float a1 = (float)j / segments * 2.0f * kPi;
                const float a2 = (float)(j + 1) / segments * 2.0f * kPi;
                const Vector3 point1 = center + (axis1 * std::cos(a1) + axis2 * std::sin(a1)) * radius;
                const Vector3 point2 = center + (axis1 * std::cos(a2) + axis2 * std::sin(a2)) * radius;
                lm.DrawLine(point1, point2, color, alpha);
            }
        }

        /// @brief 光源本体のマーカー（小さなワイヤ球）
        void DrawBulbMarker(LineManager& lm, const Vector3& position, float size,
                            const Vector3& color, float alpha)
        {
            DrawOrientedCircle(lm, position, { 1,0,0 }, { 0,1,0 }, size, 12, color, alpha);
            DrawOrientedCircle(lm, position, { 1,0,0 }, { 0,0,1 }, size, 12, color, alpha);
            DrawOrientedCircle(lm, position, { 0,1,0 }, { 0,0,1 }, size, 12, color, alpha);
        }
#endif
    }

    // ==================== Hierarchy 子ツリー ====================

    bool LightDebugVisualizer::DrawHierarchyChildren(LightManager& manager)
    {
#ifdef _DEBUG
        bool clicked = false;

        std::vector<LightHandle> handles;
        manager.ForEachLight([&handles](LightHandle h, Light&) { handles.push_back(h); });

        for (LightHandle handle : handles) {
            const Light* light = manager.GetLight(handle);
            if (!light) continue;

            ImGui::PushID(static_cast<int>(handle.index));

            // 無効ライトはラベルを暗くして状態を示す
            if (!light->enabled) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            }
            char label[96];
            snprintf(label, sizeof(label), "%s  [%s]", light->name.c_str(), GetTypeLabel(light->type));
            if (ImGui::Selectable(label, handle == selectedLight_)) {
                selectedLight_ = handle;
                clicked = true;
            }
            if (!light->enabled) {
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }
        return clicked;
#else
        (void)manager;
        return false;
#endif
    }

    // ==================== Inspector ====================

    void LightDebugVisualizer::DrawImGui(LightManager& manager)
    {
#ifdef _DEBUG
        if (manager.GetLight(selectedLight_)) {
            DrawSelectedLightInspector(manager);
        } else {
            DrawOverview(manager);
        }
#else
        (void)manager;
#endif
    }

    void LightDebugVisualizer::DrawOverview(LightManager& manager)
    {
#ifdef _DEBUG
        // ── 概要 ──
        {
            uint32_t total = 0;
            constexpr LightType kTypes[] = {
                LightType::Directional, LightType::Point, LightType::Spot, LightType::Area
            };
            for (LightType t : kTypes) {
                total += manager.GetLightCount(t);
            }
            const uint32_t totalMax = LightManager::MAX_TOTAL_LIGHTS;

            float fraction = static_cast<float>(total) / totalMax;
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "%u / %u", total, totalMax);
            ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);

            UI::Widgets::ToggleSwitch("デバッグ可視化", &enableVisualization_);

            auto setAllEnabled = [&](bool enabled) {
                manager.ForEachLight([enabled](LightHandle, Light& l) { l.enabled = enabled; });
            };
            if (ImGui::SmallButton("全て有効")) setAllEnabled(true);
            UI::SameLine();
            if (ImGui::SmallButton("全て無効")) setAllEnabled(false);
        }

        UI::Spacing();

        // ── ライト追加（追加後は新しいライトを選択して即編集に入る） ──
        {
            constexpr LightType kTypes[] = {
                LightType::Directional, LightType::Point, LightType::Spot, LightType::Area
            };
            bool first = true;
            for (LightType t : kTypes) {
                if (!first) UI::SameLine();
                first = false;

                const bool full = manager.GetLightCount(t) >= LightManager::GetMaxLightCount(t);
                char label[32];
                snprintf(label, sizeof(label), "+ %s", GetTypeLabel(t));
                ImGui::BeginDisabled(full);
                if (ImGui::SmallButton(label)) {
                    LightHandle created = manager.CreateLight(t);
                    if (created.IsValid()) {
                        selectedLight_ = created;
                    }
                }
                ImGui::EndDisabled();
            }
        }

        UI::Spacing();
        UI::Hint("Hierarchy の Environment > Lighting からライトを選択して編集");
#else
        (void)manager;
#endif
    }

    void LightDebugVisualizer::DrawSelectedLightInspector(LightManager& manager)
    {
#ifdef _DEBUG
        Light* light = manager.GetLight(selectedLight_);
        if (!light) return;

        // ── ヘッダー（有効トグル＋名前＋種類） ──
        UI::Widgets::ToggleSwitch("##en", &light->enabled);
        UI::SameLine();
        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "%s", light->name.c_str());
        if (UI::InputText("##name", nameBuf, sizeof(nameBuf))) {
            light->name = nameBuf;
        }
        UI::HintF("種類: %s", GetTypeLabel(light->type));

        UI::Spacing();
        DrawLightProperties(*light);

        // ── 操作 ──
        UI::Spacing();
        UI::Separator();
        if (ImGui::SmallButton("複製")) {
            const Light srcCopy = *light;
            LightHandle newHandle = manager.CreateLight(srcCopy.type, srcCopy.name + " Copy");
            if (Light* dst = manager.GetLight(newHandle)) {
                const std::string newName = dst->name;
                *dst = srcCopy;
                dst->name = newName;
                // 太陽・月の役割は複製しない（大気の光源が二重にならないように）
                dst->isAtmosphereSun = false;
                dst->isAtmosphereMoon = false;
                selectedLight_ = newHandle;
            }
        }
        UI::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::SmallButton("削除")) {
            manager.DestroyLight(selectedLight_);
            selectedLight_ = {};
        }
        ImGui::PopStyleColor(2);
#else
        (void)manager;
#endif
    }

    void LightDebugVisualizer::DrawLightProperties(Light& light)
    {
#ifdef _DEBUG
        // 物理量プリセットボタン列（現実の光源の目安値。対数スライダーの補助）
        auto drawPresets = [](float& value, std::initializer_list<std::pair<const char*, float>> presets) {
            bool first = true;
            for (const auto& [label, preset] : presets) {
                if (!first) UI::SameLine();
                first = false;
                if (ImGui::SmallButton(label)) {
                    value = preset;
                }
            }
        };

        UI::ColorEdit3("色", light.color);

        switch (light.type) {
        case LightType::Directional:
            UI::DragVec3("方向", light.direction, 0.01f, -1.0f, 1.0f);
            ImGui::SliderFloat("照度 [lx]", &light.intensity, 0.0f, 150000.0f, "%.0f",
                ImGuiSliderFlags_Logarithmic);
            drawPresets(light.intensity, {
                { "快晴 100k", 100000.0f }, { "薄曇り 30k", 30000.0f },
                { "曇天 10k", 10000.0f }, { "夕暮れ 500", 500.0f } });
            UI::DragVec3("位置（ギズモ表示用）", light.position, 0.1f, -50.0f, 50.0f);
            UI::Hint("平行光なので位置は明るさに影響しません（ギズモの表示位置のみ）");
            if (ImGui::SmallButton("方向を正規化")) {
                light.direction = CoreEngine::Normalize(light.direction);
            }
            ImGui::Checkbox("大気の太陽", &light.isAtmosphereSun);
            UI::SameLine();
            ImGui::Checkbox("大気の月", &light.isAtmosphereMoon);
            if (light.isAtmosphereSun || light.isAtmosphereMoon) {
                UI::DragFloat("空の明るさ（散乱スケール）", light.atmosphereIntensity, 0.1f, 0.0f, 100.0f);
                UI::Hint("空・雲の明るさ（無次元、太陽の目安 20）。0 = 照度から自動換算。\n"
                         "方向とこの値は Sky Atmosphere エディタからも操作できます（同じライトを編集）");
            }
            break;

        case LightType::Point:
            UI::DragVec3("位置", light.position, 0.1f, -50.0f, 50.0f);
            ImGui::SliderFloat("光度 [cd]", &light.intensity, 0.0f, 1000000.0f, "%.0f",
                ImGuiSliderFlags_Logarithmic);
            drawPresets(light.intensity, {
                { "ろうそく 1", 1.0f }, { "電球 100", 100.0f },
                { "街灯 1k", 1000.0f }, { "投光器 100k", 100000.0f } });
            UI::DragFloat("到達距離 [m]", light.range, 0.1f, 0.1f, 200.0f);
            UI::HintF("参考: 3m 先の照度 %.0f lx（快晴の太陽 = 100,000 lx）。\n"
                      "昼シーンでは太陽より十分明るくないと視認できません", light.intensity / 9.0f);
            break;

        case LightType::Spot:
            UI::DragVec3("位置", light.position, 0.1f, -50.0f, 50.0f);
            UI::DragVec3("方向", light.direction, 0.01f, -1.0f, 1.0f);
            ImGui::SliderFloat("光度 [cd]", &light.intensity, 0.0f, 1000000.0f, "%.0f",
                ImGuiSliderFlags_Logarithmic);
            drawPresets(light.intensity, {
                { "懐中電灯 300", 300.0f }, { "車のHID 20k", 20000.0f },
                { "サーチライト 100k", 100000.0f } });
            UI::DragFloat("到達距離 [m]", light.range, 0.1f, 0.1f, 200.0f);
            if (UI::SliderFloat("外角 [deg]", light.outerConeAngleDeg, 1.0f, 89.0f)) {
                light.innerConeAngleDeg = std::min(light.innerConeAngleDeg, light.outerConeAngleDeg);
            }
            if (UI::SliderFloat("減衰開始角 [deg]", light.innerConeAngleDeg, 0.0f, 89.0f)) {
                light.outerConeAngleDeg = std::max(light.outerConeAngleDeg, light.innerConeAngleDeg);
            }
            UI::HintF("参考: 3m 先の照度 %.0f lx（快晴の太陽 = 100,000 lx）", light.intensity / 9.0f);
            if (ImGui::SmallButton("方向を正規化")) {
                light.direction = CoreEngine::Normalize(light.direction);
            }
            break;

        case LightType::Area:
            UI::DragVec3("位置", light.position, 0.1f, -50.0f, 50.0f);
            UI::DragVec3("法線", light.direction, 0.01f, -1.0f, 1.0f);
            ImGui::SliderFloat("輝度 [nt]", &light.intensity, 0.0f, 100000.0f, "%.0f",
                ImGuiSliderFlags_Logarithmic);
            drawPresets(light.intensity, {
                { "液晶画面 300", 300.0f }, { "照明パネル 8k", 8000.0f },
                { "曇り空 25k", 25000.0f } });
            UI::DragFloat("幅 [m]", light.areaWidth, 0.1f, 0.1f, 20.0f);
            UI::DragFloat("高さ [m]", light.areaHeight, 0.1f, 0.1f, 20.0f);
            UI::DragFloat("到達距離 [m]", light.range, 0.1f, 0.1f, 100.0f);
            if (ImGui::SmallButton("法線を正規化")) {
                light.direction = CoreEngine::Normalize(light.direction);
            }
            break;
        }
#else
        (void)light;
#endif
    }

    // ==================== デバッグ可視化（UE 風ギズモ） ====================
    // 方針: 光源本体 = 小さなワイヤ球、方向 = 矢じり付き矢印、範囲 = ワイヤ形状。
    //       選択中は不透明で全要素、非選択は半透明でマーカー＋方向のみの簡略表示。

    void LightDebugVisualizer::DrawVisualization(const Light& light, bool selected)
    {
#ifdef _DEBUG
        if (!enableVisualization_ || !light.enabled) {
            return;
        }

        switch (light.type) {
        case LightType::Directional: DrawDirectionalLightVisualization(light, selected); break;
        case LightType::Point:       DrawPointLightVisualization(light, selected); break;
        case LightType::Spot:        DrawSpotLightVisualization(light, selected); break;
        case LightType::Area:        DrawAreaLightVisualization(light, selected); break;
        }
#else
        (void)light;
        (void)selected;
#endif
    }

    void LightDebugVisualizer::DrawDirectionalLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef _DEBUG
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;
        const float alpha = selected ? 1.0f : 0.45f;

        // 太陽アイコン: position をアンカーに小さなワイヤ球＋放射状の短い光線
        const Vector3 anchor = light.position;
        const Vector3 dir = CoreEngine::Normalize(light.direction);

        DrawBulbMarker(lm, anchor, 0.35f, color, alpha);

        Vector3 p1{}, p2{};
        MakePerpBasis(dir, p1, p2);
        const int rays = 8;
        for (int j = 0; j < rays; ++j)
        {
            const float a = (float)j / rays * 2.0f * kPi;
            const Vector3 rayDir = p1 * std::cos(a) + p2 * std::sin(a);
            lm.DrawLine(anchor + rayDir * 0.5f, anchor + rayDir * 0.85f, color, alpha);
        }

        // 方向矢印（本体）
        DrawArrow(lm, anchor, anchor + dir * 3.0f, color, alpha);

        // 選択中は平行光線の矢印を追加（UE のディレクショナルライト風）
        if (selected)
        {
            const Vector3 offsets[4] = {
                p1 * 0.9f, p1 * -0.9f, p2 * 0.9f, p2 * -0.9f
            };
            for (const Vector3& off : offsets)
            {
                DrawArrow(lm, anchor + off + dir * 0.8f, anchor + off + dir * 2.8f, color, 0.5f);
            }
        }
#endif
    }

    void LightDebugVisualizer::DrawPointLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef _DEBUG
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;

        // 光源本体
        DrawBulbMarker(lm, light.position, 0.25f, color, selected ? 1.0f : 0.45f);
        if (!selected) return;

        // 到達距離のワイヤ球（3大円）
        DrawOrientedCircle(lm, light.position, { 1,0,0 }, { 0,1,0 }, light.range, 32, color, 0.8f);
        DrawOrientedCircle(lm, light.position, { 1,0,0 }, { 0,0,1 }, light.range, 32, color, 0.8f);
        DrawOrientedCircle(lm, light.position, { 0,1,0 }, { 0,0,1 }, light.range, 32, color, 0.8f);
#endif
    }

    void LightDebugVisualizer::DrawSpotLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef _DEBUG
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;
        const float alpha = selected ? 1.0f : 0.45f;

        const Vector3 dir = CoreEngine::Normalize(light.direction);

        // 光源本体＋方向矢印
        DrawBulbMarker(lm, light.position, 0.2f, color, alpha);
        DrawArrow(lm, light.position,
            light.position + dir * std::min(1.5f, light.range * 0.3f), color, alpha);
        if (!selected) return;

        // コーン（外角: 母線4本＋端円 / 減衰開始角: 内側の淡い円）
        const float outerAngle = light.outerConeAngleDeg * (kPi / 180.0f);
        const float innerAngle = light.innerConeAngleDeg * (kPi / 180.0f);
        const float outerRadius = std::tan(outerAngle) * light.range;
        const float innerRadius = std::tan(innerAngle) * light.range;
        const Vector3 coneEnd = light.position + dir * light.range;

        Vector3 p1{}, p2{};
        MakePerpBasis(dir, p1, p2);

        DrawOrientedCircle(lm, coneEnd, p1, p2, outerRadius, 32, color, 0.9f);
        for (int j = 0; j < 4; ++j)
        {
            const float a = (float)j / 4.0f * 2.0f * kPi;
            const Vector3 rim = coneEnd + (p1 * std::cos(a) + p2 * std::sin(a)) * outerRadius;
            lm.DrawLine(light.position, rim, color, 0.9f);
        }

        DrawOrientedCircle(lm, coneEnd, p1, p2, innerRadius, 32, color, 0.35f);
#endif
    }

    void LightDebugVisualizer::DrawAreaLightVisualization(const Light& light, bool selected)
    {
        (void)light;
        (void)selected;
#ifdef _DEBUG
        auto& lm = LineManager::GetInstance();
        const Vector3 color = light.color;
        const float alpha = selected ? 1.0f : 0.45f;

        // GPU 転送と同じ規則で発光面の基底を導出する
        const Vector3 normal = CoreEngine::Normalize(light.direction);
        Vector3 right{}, up{};
        LightManager::ComputeAreaLightBasis(light.direction, right, up);

        const Vector3 halfRight = right * (light.areaWidth * 0.5f);
        const Vector3 halfUp = up * (light.areaHeight * 0.5f);

        const Vector3 corners[4] = {
            light.position - halfRight - halfUp,
            light.position + halfRight - halfUp,
            light.position + halfRight + halfUp,
            light.position - halfRight + halfUp,
        };

        // 発光面の矩形＋法線矢印（UE の Rect Light 風）
        for (int j = 0; j < 4; ++j)
        {
            lm.DrawLine(corners[j], corners[(j + 1) % 4], color, alpha);
        }
        DrawArrow(lm, light.position, light.position + normal * 1.0f, color, alpha);
        if (!selected) return;

        // 放射方向のプレビュー: 少し先へ投影した淡い矩形と4隅の接続線
        const float previewDist = std::min(light.range * 0.25f, 2.0f);
        for (int j = 0; j < 4; ++j)
        {
            const Vector3 projected0 = corners[j] + normal * previewDist;
            const Vector3 projected1 = corners[(j + 1) % 4] + normal * previewDist;
            lm.DrawLine(projected0, projected1, color, 0.35f);
            lm.DrawLine(corners[j], projected0, color, 0.35f);
        }
#endif
    }
}
