#include "pch.h"
#include "VolumetricCloudEditor.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Render/RenderDomainContext.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#endif

#include <cmath>
#include <iterator>

namespace CoreEngine {

    namespace {
        constexpr const char* kEditorLabel = "Volumetric Cloud";

#ifdef USE_IMGUI
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

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
        /// @details パフォーマンス系（ステップ数・解像度分割）とライティングの物理定数
        ///          （位相関数・多重散乱オクターブ）はプリセットでは触らない。
        ///          coverage と erosion は必ずセットで調整すること（coverage が低いのに
        ///          erosion が強いと雲が孤立した粒＝ポップコーン状に崩れる既知の不具合がある）。
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

        /// 天候プリセット一覧。index 1（部分的な曇り）は VolumetricCloudParameters の既定値と一致させる
        constexpr CloudPreset kCloudPresets[] = {
            { "快晴（まばらな雲）",
              "青空が主体で、ぽつぽつと積雲が浮かぶ晴天。\nカバレッジを下げるぶん縁の侵食も弱め、雲塊が粒に崩れないようにしている。",
              0.50f, 0.12f, 0.16f, 2000.0f, 3000.0f, 8000.0f, 6.0f, 1.15f, 3.5f },
            { "部分的な曇り（既定）",
              "青空と雲が半々の既定バランス。エンジン既定値と同じ。",
              0.62f, 0.12f, 0.22f, 1500.0f, 4000.0f, 9000.0f, 8.0f, 1.15f, 3.5f },
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
        constexpr int kCloudPresetCount = static_cast<int>(std::size(kCloudPresets));

        /// @brief プリセットの「見た目」パラメータを適用する（マーチング予算などは維持）
        void ApplyCloudPreset(VolumetricCloudManager& manager, const CloudPreset& preset)
        {
            auto& params = manager.GetParametersMutable();
            params.globalCoverage = preset.coverage;
            params.densityScale = preset.density;
            params.detailErosionStrength = preset.erosion;
            params.layerBottomAltitudeM = preset.bottomAltitudeM;
            params.layerThicknessM = preset.thicknessM;
            params.baseNoiseScaleM = preset.baseNoiseScaleM;
            params.windSpeedMPerS = preset.windSpeedMPerS;
            params.ambientIntensity = preset.ambientIntensity;
            params.sunLightScale = preset.sunLightScale;
        }
#endif
    }

    void VolumetricCloudEditor::Initialize(EngineSystem& engine)
    {
        engine_ = &engine;
#ifdef USE_IMGUI
        // Hierarchy の Environment ツリーへ登録し、選択時に Inspector で編集できるようにする
        if (auto* debug = engine_->GetDebugSubsystem()) {
            if (auto* ui = debug->GetGameDebugUI()) {
                ui->RegisterEnvironmentEditor(kEditorLabel, this, [this]() { DrawContent(); });
            }
        }
#endif
    }

    VolumetricCloudEditor::~VolumetricCloudEditor()
    {
#ifdef USE_IMGUI
        // エンジン終了時にドロワーがダングリングしないよう登録を解除する
        if (engine_) {
            if (auto* debug = engine_->GetDebugSubsystem()) {
                if (auto* ui = debug->GetGameDebugUI()) {
                    ui->UnregisterEnvironmentEditor(kEditorLabel, this);
                }
            }
        }
#endif
    }

    void VolumetricCloudEditor::DrawPresetSelector(VolumetricCloudManager& manager)
    {
#ifdef USE_IMGUI
        const char* currentName = (activePresetIndex_ >= 0 && activePresetIndex_ < kCloudPresetCount)
            ? kCloudPresets[activePresetIndex_].name
            : "カスタム";
        if (ImGui::BeginCombo("プリセット", currentName)) {
            for (int i = 0; i < kCloudPresetCount; ++i) {
                const bool selected = (i == activePresetIndex_);
                if (ImGui::Selectable(kCloudPresets[i].name, selected)) {
                    ApplyCloudPreset(manager, kCloudPresets[i]);
                    activePresetIndex_ = i;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", kCloudPresets[i].description);
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
            ImGui::TextDisabled("%s", kCloudPresets[activePresetIndex_].description);
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

        auto& params = cloudManager->GetParametersMutable();
        bool lookChanged = false;        // プリセット由来の値が手動変更されたか（→カスタム表示）
        bool noiseScaleChanged = false;

        // ===== 基本（よく触る主要パラメータのみ） =====
        if (ImGui::CollapsingHeader("基本", ImGuiTreeNodeFlags_DefaultOpen)) {
            lookChanged |= ImGui::SliderFloat("カバレッジ", &params.globalCoverage, 0.0f, 1.0f, "%.2f");
            HelpMarker("空を雲が覆う割合。0=快晴、1=全天曇り。\n"
                "低くしすぎると雲が孤立した粒に崩れるため、\nその場合は「縁の侵食」も下げてください。");

            lookChanged |= ImGui::SliderFloat("密度", &params.densityScale, 0.01f, 0.5f, "%.3f");
            HelpMarker("雲の濃さ [1/m]。低いと霧のように薄く透け、\n高いと輪郭の固い入道雲のようになります。");

            lookChanged |= ImGui::SliderFloat("縁の侵食", &params.detailErosionStrength, 0.0f, 1.0f, "%.2f");
            HelpMarker("雲の縁を削るディテールの強さ。\n"
                "0.2 前後でカリフラワー状の凹凸、0.12〜0.18 で柔らかい縁。\n"
                "カバレッジに対して強すぎると内部まで穴が開き斑点状になります。");

            ImGui::Spacing();
            lookChanged |= ImGui::DragFloat("雲底高度 [m]", &params.layerBottomAltitudeM, 10.0f, 200.0f, 10000.0f, "%.0f");
            HelpMarker("雲層の下端の高度。低いほど雲が近く大きく見えます。");
            lookChanged |= ImGui::DragFloat("層の厚さ [m]", &params.layerThicknessM, 10.0f, 500.0f, 10000.0f, "%.0f");
            HelpMarker("雲層の縦の厚み。厚いほど背の高いもくもくした雲になります。");
        }

        // ===== 風 =====
        if (ImGui::CollapsingHeader("風", ImGuiTreeNodeFlags_DefaultOpen)) {
            // 内部表現は正規化 XZ ベクトルだが、UI は方位角 1 本のほうが直感的
            float windAzimuthDeg = std::atan2(params.windDirX, params.windDirZ) / kDegToRad;
            if (ImGui::SliderFloat("風向 [deg]", &windAzimuthDeg, -180.0f, 180.0f, "%.0f")) {
                params.windDirX = std::sin(windAzimuthDeg * kDegToRad);
                params.windDirZ = std::cos(windAzimuthDeg * kDegToRad);
            }
            HelpMarker("雲が流れていく方角（0°=+Z、90°=+X。太陽の方位角と同じ規約）");
            lookChanged |= ImGui::DragFloat("風速 [m/s]", &params.windSpeedMPerS, 0.5f, 0.0f, 50.0f, "%.1f");
        }

        // ===== ライティング（上級） =====
        if (ImGui::CollapsingHeader("ライティング（上級）")) {
            ImGui::TextDisabled("陰影と散乱の物理パラメータ。通常はプリセットのままで十分です");
            lookChanged |= ImGui::DragFloat("太陽散乱スケール", &params.sunLightScale, 0.05f, 0.1f, 10.0f, "%.2f");
            HelpMarker("雲の明るさの全体スケール。上げすぎると白飛びして陰影が消えます。");
            lookChanged |= ImGui::DragFloat("アンビエント強度", &params.ambientIntensity, 0.02f, 0.0f, 3.0f, "%.2f");
            HelpMarker("空からの環境光。雲の暗部（雲底など）の持ち上げ量。\n密度を上げたときは連動して上げると暗部が黒く沈みません。");
            ImGui::SliderFloat("Powder 効果", &params.beerPowderStrength, 0.0f, 1.0f, "%.2f");
            HelpMarker("照射面の外殻を暗くする効果。強すぎると順光の雲面全体が沈みます。");

            ImGui::SeparatorText("位相関数（散乱の方向性）");
            ImGui::SliderFloat("前方散乱 g0", &params.phaseG0, 0.0f, 0.99f, "%.2f");
            ImGui::SliderFloat("後方散乱 g1", &params.phaseG1, -0.99f, 0.0f, "%.2f");
            ImGui::SliderFloat("ブレンド", &params.phaseBlend, 0.0f, 1.0f, "%.2f");

            ImGui::SeparatorText("多重散乱オクターブ");
            ImGui::SliderFloat("消散減衰", &params.msAttenuation, 0.1f, 0.9f, "%.2f");
            ImGui::SliderFloat("寄与減衰", &params.msContribution, 0.1f, 0.9f, "%.2f");
            ImGui::SliderFloat("位相非対称度減衰", &params.msEccentricity, 0.1f, 0.9f, "%.2f");

            ImGui::DragFloat("サンライトマーチ基準ステップ [m]", &params.lightMarchStepM, 5.0f, 20.0f, 1000.0f, "%.0f");
        }

        // ===== 形状ノイズ（上級） =====
        if (ImGui::CollapsingHeader("形状ノイズ（上級）")) {
            ImGui::TextDisabled("雲塊のスケール感。値はサンプル実寸で、ノイズ自体の再生成は不要です");
            noiseScaleChanged |= ImGui::DragFloat("ベースノイズ実寸 [m]", &params.baseNoiseScaleM, 100.0f, 1000.0f, 40000.0f, "%.0f");
            HelpMarker("雲塊 1 個の大きさの目安（幅はこの約半分）。\n層の厚さの 2〜2.5 倍で積雲らしい形になります。");
            noiseScaleChanged |= ImGui::DragFloat("ディテールノイズ実寸 [m]", &params.detailNoiseScaleM, 10.0f, 50.0f, 5000.0f, "%.0f");
            noiseScaleChanged |= ImGui::DragFloat("天候マップ実寸 [m]", &params.weatherMapScaleM, 1000.0f, 5000.0f, 200000.0f, "%.0f");
            HelpMarker("晴れ間と雲域の分布パターンの繰り返し実寸。");
        }

        // ===== ゴッドレイ =====
        if (ImGui::CollapsingHeader("ゴッドレイ")) {
            ImGui::Checkbox("ゴッドレイを有効にする", &params.godRayEnabled);
            ImGui::DragFloat("遮蔽差分の強度（物理項）", &params.godRayIntensity, 0.05f, 0.0f, 3.0f, "%.2f");
            ImGui::DragFloat("ミー散乱ブースト（演出）", &params.godRayMieBoost, 0.05f, 0.0f, 2.0f, "%.2f");
            ImGui::DragFloat("マーチ最大距離 [m]##godray", &params.godRayMaxDistanceM, 500.0f, 5000.0f, 60000.0f, "%.0f");
            int godRaySteps = static_cast<int>(params.godRayStepCount);
            if (ImGui::SliderInt("ステップ数##godray", &godRaySteps, 8, 64)) {
                params.godRayStepCount = static_cast<uint32_t>(godRaySteps);
            }
            ImGui::DragFloat("雲シャドウ範囲 [m]", &params.cloudShadowRegionSizeM, 1000.0f, 20000.0f, 200000.0f, "%.0f");
        }

        // ===== パフォーマンス =====
        if (ImGui::CollapsingHeader("パフォーマンス")) {
            int maxSteps = static_cast<int>(params.maxSteps);
            if (ImGui::SliderInt("ステップ数予算", &maxSteps, 16, 256)) {
                params.maxSteps = static_cast<uint32_t>(maxSteps);
            }
            ImGui::DragFloat("マーチ最大距離 [m]", &params.maxMarchDistanceM, 500.0f, 5000.0f, 100000.0f, "%.0f");
            ImGui::DragFloat("早期終了しきい値", &params.earlyExitTransmittance, 0.001f, 0.0001f, 0.05f, "%.4f");

            int divisor = static_cast<int>(params.resolutionDivisor);
            if (ImGui::SliderInt("解像度分割数（1=フル, 2=半解像度）", &divisor, 1, 4)) {
                params.resolutionDivisor = static_cast<uint32_t>(divisor);
            }
        }

        // ===== 診断情報 =====
        if (ImGui::CollapsingHeader("診断")) {
            ImGui::Text("雲アクティブ（このフレーム）: %s", cloudManager->AreCloudsActive() ? "true" : "false");
            ImGui::Text("ノイズテクスチャ生成済み: %s", cloudManager->AreNoiseTexturesReady() ? "true" : "false");
        }

        if (noiseScaleChanged) {
            cloudManager->MarkNoiseDirty();
            lookChanged = true;
        }
        if (lookChanged) {
            // プリセット適用後に手動調整されたので「カスタム」表示へ切り替える
            activePresetIndex_ = -1;
        }

        ImGui::Spacing();
        if (ImGui::Button("パラメータを既定値にリセット")) {
            params = VolumetricCloudParameters{};
            cloudManager->MarkNoiseDirty();
            activePresetIndex_ = 1; // 既定値 = 「部分的な曇り（既定）」
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
