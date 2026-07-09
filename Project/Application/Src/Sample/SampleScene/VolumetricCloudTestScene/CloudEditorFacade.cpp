#include "pch.h"
#include "CloudEditorFacade.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Render/RenderDomainContext.h"

#ifdef USE_IMGUI
#include "Utility/Debug/ImGui/ImGuiAll.h"
#endif

#include <cmath>

using namespace CoreEngine;

void CloudEditorFacade::Initialize(EngineSystem& engine)
{
    engine_ = &engine;
}

void CloudEditorFacade::DrawImGui()
{
#ifdef USE_IMGUI
    // 初回表示時は画面内の見やすい位置に配置する（ドック外へはみ出して診断値が読めなくなるのを防ぐ）
    ImGui::SetNextWindowPos(ImVec2(60.0f, 80.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Volumetric Cloud")) {
        ImGui::End();
        return;
    }

    auto* cloudManager = GetVolumetricCloudManager();
    if (!cloudManager) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "VolumetricCloudManager が見つかりません");
        ImGui::End();
        return;
    }

    // ===== 機能トグル =====
    bool enabled = cloudManager->IsEnabled();
    if (ImGui::Checkbox("雲を有効にする", &enabled)) {
        cloudManager->SetEnabled(enabled);
    }

    auto& params = cloudManager->GetParametersMutable();
    bool noiseScaleChanged = false;

    // ===== 雲層ジオメトリ =====
    if (ImGui::CollapsingHeader("雲層ジオメトリ", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("雲底高度 [m]", &params.layerBottomAltitudeM, 10.0f, 200.0f, 10000.0f, "%.0f");
        ImGui::DragFloat("層厚 [m]", &params.layerThicknessM, 10.0f, 500.0f, 10000.0f, "%.0f");
    }

    // ===== カバレッジ・密度 =====
    if (ImGui::CollapsingHeader("カバレッジ・密度", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("全体カバレッジ", &params.globalCoverage, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("密度スケール [1/m]", &params.densityScale, 0.005f, 0.01f, 0.5f, "%.3f");
        ImGui::SliderFloat("縁の侵食強度", &params.detailErosionStrength, 0.0f, 1.0f, "%.2f");
    }

    // ===== ノイズスケール =====
    if (ImGui::CollapsingHeader("ノイズスケール")) {
        noiseScaleChanged |= ImGui::DragFloat("ベースノイズ実寸 [m]", &params.baseNoiseScaleM, 100.0f, 1000.0f, 40000.0f, "%.0f");
        noiseScaleChanged |= ImGui::DragFloat("ディテールノイズ実寸 [m]", &params.detailNoiseScaleM, 10.0f, 50.0f, 5000.0f, "%.0f");
        noiseScaleChanged |= ImGui::DragFloat("天候マップ実寸 [m]", &params.weatherMapScaleM, 1000.0f, 5000.0f, 200000.0f, "%.0f");
        ImGui::TextDisabled("(サンプルスケールのみ。ノイズ形状自体は手続き生成のため再生成不要)");
    }

    // ===== 風 =====
    if (ImGui::CollapsingHeader("風", ImGuiTreeNodeFlags_DefaultOpen)) {
        float windDir[2] = { params.windDirX, params.windDirZ };
        if (ImGui::SliderFloat2("風向 XZ", windDir, -1.0f, 1.0f, "%.2f")) {
            const float len = std::sqrt(windDir[0] * windDir[0] + windDir[1] * windDir[1]);
            if (len > 1e-4f) {
                params.windDirX = windDir[0] / len;
                params.windDirZ = windDir[1] / len;
            }
        }
        ImGui::DragFloat("風速 [m/s]", &params.windSpeedMPerS, 0.5f, 0.0f, 50.0f, "%.1f");
    }

    // ===== ライティング =====
    if (ImGui::CollapsingHeader("ライティング", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("位相 g0（前方散乱）", &params.phaseG0, 0.0f, 0.99f, "%.2f");
        ImGui::SliderFloat("位相 g1（後方散乱）", &params.phaseG1, -0.99f, 0.0f, "%.2f");
        ImGui::SliderFloat("位相ブレンド", &params.phaseBlend, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("アンビエント強度", &params.ambientIntensity, 0.02f, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Powder 効果", &params.beerPowderStrength, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("太陽散乱スケール", &params.sunLightScale, 0.05f, 0.1f, 10.0f, "%.2f");

        ImGui::SeparatorText("多重散乱オクターブ");
        ImGui::SliderFloat("消散減衰", &params.msAttenuation, 0.1f, 0.9f, "%.2f");
        ImGui::SliderFloat("寄与減衰", &params.msContribution, 0.1f, 0.9f, "%.2f");
        ImGui::SliderFloat("位相非対称度減衰", &params.msEccentricity, 0.1f, 0.9f, "%.2f");

        ImGui::DragFloat("サンライトマーチ基準ステップ [m]", &params.lightMarchStepM, 5.0f, 20.0f, 1000.0f, "%.0f");
    }

    // ===== マーチング・パフォーマンス =====
    if (ImGui::CollapsingHeader("マーチング・パフォーマンス")) {
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

    if (noiseScaleChanged) {
        cloudManager->MarkNoiseDirty();
    }

    ImGui::Spacing();
    if (ImGui::Button("パラメータをリセット")) {
        params = CoreEngine::VolumetricCloudParameters{};
        cloudManager->MarkNoiseDirty();
    }

    // ===== 診断情報 =====
    if (ImGui::CollapsingHeader("診断", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("雲アクティブ（このフレーム）: %s", cloudManager->AreCloudsActive() ? "true" : "false");
        ImGui::Text("ノイズテクスチャ生成済み: %s", cloudManager->AreNoiseTexturesReady() ? "true" : "false");
    }

    ImGui::End();
#endif
}

VolumetricCloudManager* CloudEditorFacade::GetVolumetricCloudManager() const
{
    if (!engine_ || !engine_->GetRenderDomainContext()) {
        return nullptr;
    }
    return engine_->GetRenderDomainContext()->GetVolumetricCloudManager();
}
