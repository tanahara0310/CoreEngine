#include "pch.h"
#include "MaterialDebugUI.h"

#ifdef USE_IMGUI

#include "Graphics/Model/Model.h"
#include "Graphics/Material/MaterialConstants.h"
#include "Editor/ImGui/ImGuiAll.h"

namespace CoreEngine {

    bool MaterialDebugUI::Draw(Model* model) {
        if (!model) return false;

        auto* mat = model->GetMaterial();
        if (!mat) return false;

        bool changed = false;

        // ─────────────── 基本設定 ───────────────
        UI::SectionHeader("基本設定");

        Vector4 color = mat->GetColor();
        if (UI::ColorEdit("カラー", color)) {
            mat->SetColor(color);
            changed = true;
        }

        // ─────────────── ライティング ───────────────
        UI::SectionHeader("ライティング");

        bool enableLighting = mat->IsLightingEnabled();
        if (UI::Widgets::ToggleSwitch("ライティング有効 (PBR)", &enableLighting)) {
            mat->SetLightingEnabled(enableLighting);
            changed = true;
        }
        UI::SameLine();
        UI::Hint(enableLighting ? "PBR" : "Unlit");

        // ─────────────── PBR パラメータ ───────────────
        UI::SectionHeader("PBR パラメータ");

        // ファクターはテクスチャと乗算合成される（glTF 準拠）。
        // テクスチャ無しマテリアルは白フォールバックのためファクター値がそのまま最終値になる。
        {
            const bool hasNormal = model->HasNormalMap();
            UI::Scope::DisabledScope ds(!hasNormal);
            bool normalEnabled = mat->IsNormalMapEnabled();
            if (UI::Widgets::ToggleSwitch("Normal Map", &normalEnabled)) {
                mat->SetNormalMapEnabled(normalEnabled);
                changed = true;
            }
            if (!hasNormal) {
                UI::SameLine();
                UI::Hint("(なし)");
            }
        }

        if (model->HasMetallicRoughnessMap()) {
            UI::Hint("MRマップあり: ファクターはマップ値に乗算されます");
        }

        {
            float metallic = mat->GetMetallic();
            if (UI::SliderFloat("Metallic", metallic, 0.0f, 1.0f)) {
                mat->SetMetallic(metallic);
                changed = true;
            }
        }
        {
            float roughness = mat->GetRoughness();
            if (UI::SliderFloat("Roughness", roughness, 0.0f, 1.0f)) {
                mat->SetRoughness(roughness);
                changed = true;
            }
        }
        {
            UI::Scope::DisabledScope ds(!model->HasOcclusionMap());
            float occlusionStrength = mat->GetOcclusionStrength();
            if (UI::SliderFloat("AO 強度", occlusionStrength, 0.0f, 1.0f)) {
                mat->SetOcclusionStrength(occlusionStrength);
                changed = true;
            }
        }
        {
            Vector3 emissiveVec = mat->GetEmissiveFactor();
            Vector4 emissive = { emissiveVec.x, emissiveVec.y, emissiveVec.z, 1.0f };
            if (UI::ColorEdit("Emissive", emissive)) {
                mat->SetEmissiveFactor({ emissive.x, emissive.y, emissive.z });
                changed = true;
            }
        }

        // ─────────────── シェーディングモード ───────────────
        UI::SectionHeader("シェーディングモード");

        static const char* kModeItems[] = {
            "PBR  (IBL なし)",
            "PBR + IBL",
            "Lambert  (従来)",
            "Half-Lambert  (従来)"
        };

        int currentMode = static_cast<int>(mat->GetShadingMode());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##ShadingMode", &currentMode, kModeItems, 4)) {
            mat->SetShadingMode(static_cast<ShadingMode>(currentMode));
            changed = true;
        }

        // IBL 強度スライダーは PBR+IBL 時のみ表示
        if (static_cast<ShadingMode>(currentMode) == ShadingMode::PBR_IBL) {
            const bool iblAvailable = model->IsIBLAvailable();
            if (!iblAvailable) {
                UI::SameLine();
                UI::Hint("(Irradiance/Prefiltered/BRDF LUT 未設定)");
            } else {
                UI::Scope::IndentScope is;
                float iblIntensity = mat->GetIBLIntensity();
                if (UI::SliderFloat("IBL 強度", iblIntensity, 0.0f, 2.0f)) {
                    mat->SetIBLIntensity(iblIntensity);
                    changed = true;
                }
            }
        }

        // ─────────────── エフェクト ───────────────
        UI::SectionHeader("エフェクト");

        bool enableDithering = mat->IsDitheringEnabled();
        if (UI::Widgets::ToggleSwitch("ディザリング有効", &enableDithering)) {
            mat->SetDitheringEnabled(enableDithering);
            changed = true;
        }
        if (enableDithering) {
            UI::Scope::IndentScope is;
            float ditheringScale = mat->GetDitheringScale();
            if (UI::SliderFloat("スケール##Dithering", ditheringScale, 0.1f, 5.0f)) {
                mat->SetDitheringScale(ditheringScale);
                changed = true;
            }
        }

        return changed;
    }

} // namespace CoreEngine

#endif
