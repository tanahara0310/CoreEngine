#include "pch.h"
#include "VolumetricCloudEditor.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Cloud/CloudCVars.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Render/RenderDomainContext.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#endif

namespace CoreEngine {

    namespace {
        constexpr const char* kEditorLabel = "Volumetric Cloud";

#ifdef USE_IMGUI
        /// @brief UE 風の (?) ホバーツールチップ（ラベルの右に付ける）
        void HelpMarker(const char* desc)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", desc);
            }
        }

        /// @brief 天候プリセット 1 件分の「見た目」パラメータ
        /// @note パフォーマンス系とライティングの物理定数はプリセットでは触らない。
        ///       coverage と erosion は必ずセットで調整すること（片方だけだと雲が粒状に崩れる）。
        struct CloudPreset {
            const char* name;
            const char* description;
            float coverage;
            float density;
            float erosion;
            float bottomAltitudeM;
            float thicknessM;
            float baseNoiseScaleM;
            float windSpeedMPerS;
            float ambientIntensity;
            float sunLightScale;
        };

        /// 天候プリセット一覧。index 1（部分的な曇り）は CloudCVars の既定値をそのまま引く
        const CloudPreset* CloudPresets()
        {
            static const CloudPreset presets[] = {
                { "快晴（まばらな雲）",
                  "青空が主体で、ぽつぽつと積雲が浮かぶ晴天。\nカバレッジを下げるぶん縁の侵食も弱め、雲塊が粒に崩れないようにしている。",
                  0.50f, 0.12f, 0.16f, 2000.0f, 3000.0f, 8000.0f, 6.0f, 1.15f, 3.5f },
                { "部分的な曇り（既定）",
                  "青空と雲が半々の既定バランス。エンジン既定値と同じ。",
                  CloudCVars::GlobalCoverage.GetDefault(),
                  CloudCVars::DensityScale.GetDefault(),
                  CloudCVars::DetailErosionStrength.GetDefault(),
                  CloudCVars::LayerBottomAltitudeM.GetDefault(),
                  CloudCVars::LayerThicknessM.GetDefault(),
                  CloudCVars::BaseNoiseScaleM.GetDefault(),
                  CloudCVars::WindSpeedMPerS.GetDefault(),
                  CloudCVars::AmbientIntensity.GetDefault(),
                  CloudCVars::SunLightScale.GetDefault() },
                { "曇天",
                  "空の大部分が雲デッキで覆われる曇り空。\n密度を下げて柔らかい灰色にし、雲底の暗部はアンビエントで持ち上げる。",
                  0.85f, 0.08f, 0.10f, 1000.0f, 2500.0f, 12000.0f, 6.0f, 1.35f, 3.5f },
                { "高い薄雲",
                  "高度 4.5km に浮かぶ薄い高積雲風の層。\n密度が低く太陽が透けて見える。",
                  0.55f, 0.045f, 0.18f, 4500.0f, 1200.0f, 7000.0f, 12.0f, 1.25f, 3.5f },
                { "嵐の前",
                  "低く厚い黒雲が速い風で流れる荒天。\n層厚と密度を上げ、太陽散乱を絞って重い印象にする。",
                  0.92f, 0.20f, 0.10f, 800.0f, 6000.0f, 11000.0f, 18.0f, 0.90f, 3.0f },
            };
            return presets;
        }
        constexpr int kCloudPresetCount = 5;

        /// @brief 雲パラメータの CVar 接頭辞（定義は CloudCVars.cpp）
        constexpr const char* kCloudCVarPrefix = "r.Cloud";

        /// @brief 現在のパラメータがプリセットと一致するか
        bool MatchesCloudPreset(const VolumetricCloudParameters& params, const CloudPreset& preset)
        {
            return params.globalCoverage == preset.coverage
                && params.densityScale == preset.density
                && params.detailErosionStrength == preset.erosion
                && params.layerBottomAltitudeM == preset.bottomAltitudeM
                && params.layerThicknessM == preset.thicknessM
                && params.baseNoiseScaleM == preset.baseNoiseScaleM
                && params.windSpeedMPerS == preset.windSpeedMPerS
                && params.ambientIntensity == preset.ambientIntensity
                && params.sunLightScale == preset.sunLightScale;
        }

        /// @brief 現在のパラメータに一致するプリセット index を返す（無ければ -1 = カスタム）
        int FindMatchingCloudPreset(const VolumetricCloudParameters& params)
        {
            for (int i = 0; i < kCloudPresetCount; ++i) {
                if (MatchesCloudPreset(params, CloudPresets()[i])) {
                    return i;
                }
            }
            return -1;
        }

        /// @brief プリセットの「見た目」パラメータを適用する（マーチング予算などは維持）
        void ApplyCloudPreset(const CloudPreset& preset)
        {
            CloudCVars::GlobalCoverage.Set(preset.coverage);
            CloudCVars::DensityScale.Set(preset.density);
            CloudCVars::DetailErosionStrength.Set(preset.erosion);
            CloudCVars::LayerBottomAltitudeM.Set(preset.bottomAltitudeM);
            CloudCVars::LayerThicknessM.Set(preset.thicknessM);
            CloudCVars::BaseNoiseScaleM.Set(preset.baseNoiseScaleM);
            CloudCVars::WindSpeedMPerS.Set(preset.windSpeedMPerS);
            CloudCVars::AmbientIntensity.Set(preset.ambientIntensity);
            CloudCVars::SunLightScale.Set(preset.sunLightScale);
        }
#endif
    }

    void VolumetricCloudEditor::Initialize(EngineSystem& engine)
    {
        engine_ = &engine;
#ifdef USE_IMGUI
        // Hierarchy の Environment ツリーへ登録し、選択時に Inspector で編集できるようにする。
        // GameDebugUI はここで一度だけ取得してキャッシュする（デストラクタで使うため）
        if (auto* debug = engine_->GetDebugSubsystem()) {
            gameDebugUI_ = debug->GetGameDebugUI();
            if (gameDebugUI_) {
                gameDebugUI_->RegisterEnvironmentEditor(kEditorLabel, this, [this]() { DrawContent(); });
            }
        }
#endif
    }

    VolumetricCloudEditor::~VolumetricCloudEditor()
    {
#ifdef USE_IMGUI
        // エンジン終了時にドロワーがダングリングしないよう登録を解除する。
        // engine_->GetDebugSubsystem() を呼び直さないこと（サブシステム一括破棄中に走るため、
        // 破棄済みサブシステムへの dynamic_cast でアクセス違反になる）。キャッシュ済みポインタのみ使う。
        if (gameDebugUI_) {
            gameDebugUI_->UnregisterEnvironmentEditor(kEditorLabel, this);
        }
#endif
    }

    // manager は USE_IMGUI 無効時（Release）に本体が丸ごと消えて未使用になる。
    // Release は TreatWarningAsError なので C4100 でビルドが止まる。
    void VolumetricCloudEditor::DrawPresetSelector([[maybe_unused]] VolumetricCloudManager& manager)
    {
#ifdef USE_IMGUI
        // 表示は現在値から導出する（どの経路で値が変わっても追随する）
        activePresetIndex_ = FindMatchingCloudPreset(manager.GetParameters());

        const char* currentName = (activePresetIndex_ >= 0 && activePresetIndex_ < kCloudPresetCount)
            ? CloudPresets()[activePresetIndex_].name
            : "カスタム";
        if (ImGui::BeginCombo("プリセット", currentName)) {
            for (int i = 0; i < kCloudPresetCount; ++i) {
                const bool selected = (i == activePresetIndex_);
                if (ImGui::Selectable(CloudPresets()[i].name, selected)) {
                    ApplyCloudPreset(CloudPresets()[i]);
                    activePresetIndex_ = i;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", CloudPresets()[i].description);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        HelpMarker("代表的な天候の見た目を1クリックで適用します。\n"
            "適用後に各パラメータを調整すると「カスタム」表示になります。\n"
            "（マーチングのステップ数など品質・負荷の設定は変更しません）");
        if (activePresetIndex_ >= 0 && activePresetIndex_ < kCloudPresetCount) {
            ImGui::TextDisabled("%s", CloudPresets()[activePresetIndex_].description);
        }
#endif
    }

    void VolumetricCloudEditor::DrawContent()
    {
#ifdef USE_IMGUI
        auto* cloudManager = GetVolumetricCloudManager();
        if (!cloudManager) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "VolumetricCloudManager が見つかりません");
            return;
        }

        // ===== 機能トグルとプリセット（常時表示） =====
        bool enabled = cloudManager->IsEnabled();
        if (ImGui::Checkbox("雲を有効にする", &enabled)) {
            cloudManager->SetEnabled(enabled);
        }

        DrawPresetSelector(*cloudManager);
        ImGui::Spacing();

        // パラメータ UI は CVar から自動生成される（値は Update が毎フレーム取り込む）。
        // ノイズテクスチャは手続き生成 CS が定数バッファを持たず、パラメータに依存しないため
        // ここでの再生成要求は不要
        CVarUI::DrawTree(kCloudCVarPrefix);

        // ===== 診断情報 =====
        if (ImGui::CollapsingHeader("診断")) {
            ImGui::Text("雲アクティブ（このフレーム）: %s", cloudManager->AreCloudsActive() ? "true" : "false");
            ImGui::Text("ノイズテクスチャ生成済み: %s", cloudManager->AreNoiseTexturesReady() ? "true" : "false");
        }


        ImGui::Spacing();
        if (ImGui::Button("パラメータを既定値にリセット")) {
            CVarUI::ResetTree(kCloudCVarPrefix);
        }
#endif
    }

    VolumetricCloudManager* VolumetricCloudEditor::GetVolumetricCloudManager() const
    {
        if (!engine_ || !engine_->GetRenderDomainContext()) {
            return nullptr;
        }
        return engine_->GetRenderDomainContext()->GetVolumetricCloudManager();
    }
}
