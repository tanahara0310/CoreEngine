#include "pch.h"
#include "ParticleSystemDebugUI.h"

#ifdef USE_IMGUI

#include "Particle/ParticleSystem.h"
#include "Particle/Gpu/GpuParticleSystem.h"
#include "Particle/ParticlePresetManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Editor/ImGui/ImGuiAll.h"

#include <cstdio>

namespace CoreEngine
{

// ──────────────────────────────────────────────────────────
// 公開エントリーポイント（CPU / GPU）
// ──────────────────────────────────────────────────────────

bool ParticleSystemDebugUI::ShowImGui(ParticleSystem* particleSystem) {
    if (!particleSystem) return false;

    bool changed = false;

    ShowStatusHeader(particleSystem, false,
        particleSystem->GetParticleCount(), particleSystem->GetMaxParticleCount());

    changed |= ShowTransport(particleSystem, particleSystem);

    UI::Separator();

    if (particleSystem->presetManager_) {
        particleSystem->presetManager_->ShowImGui(particleSystem);
    }

    UI::Separator();

    changed |= ShowModules(particleSystem);
    // 床との衝突は CPU 版だけが持つので、共通の ShowModules ではなくここで出す
    changed |= DrawModuleSection("床との衝突", particleSystem->GetCollisionModule());
    changed |= ShowRendererSection(particleSystem, particleSystem);
    ShowStatistics(particleSystem);

    return changed;
}

bool ParticleSystemDebugUI::ShowImGui(GpuParticleSystem* particleSystem) {
    if (!particleSystem) return false;

    bool changed = false;

    ShowStatusHeader(particleSystem, true,
        particleSystem->GetAliveCount(), particleSystem->GetEffectiveCapacity());
    UI::HintF("GPUバッファ容量 %u ・ 空きスロット %u（表示は1フレーム遅延）",
        GpuParticleSystem::kMaxParticles, particleSystem->GetFreeCount());

    changed |= ShowTransport(particleSystem, nullptr);

    UI::Separator();

    if (particleSystem->presetManager_) {
        particleSystem->presetManager_->ShowImGui(particleSystem);
    }

    UI::Separator();

    changed |= ShowModules(particleSystem);
    changed |= ShowRendererSection(particleSystem, nullptr);

    return changed;
}

// ──────────────────────────────────────────────────────────
// ステータスヘッダー
// ──────────────────────────────────────────────────────────

void ParticleSystemDebugUI::ShowStatusHeader(IParticleSystem* system, bool isGpu,
                                             uint32_t aliveCount, uint32_t capacity) {
    // バックエンドバッジ + 再生状態
    const ImVec4 badgeColor = isGpu
        ? ImVec4(1.0f, 0.6f, 0.25f, 1.0f)   // GPU: オレンジ
        : ImVec4(0.4f, 0.7f, 0.9f, 1.0f);   // CPU: 水色
    ImGui::TextColored(badgeColor, "[%s]", isGpu ? "GPU" : "CPU");
    UI::Tooltip(isGpu ? "ComputeShaderで更新されるGPUパーティクルです（大量粒子向け）"
                      : "CPUで更新されるパーティクルです（少数・細かい制御向け）");
    UI::SameLine();
    if (system->IsPlaying()) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "再生中");
    } else {
        ImGui::TextDisabled("停止中");
    }

    // 粒子数バー（使用率で色を変える）
    const float ratio = (capacity > 0)
        ? static_cast<float>(aliveCount) / static_cast<float>(capacity) : 0.0f;
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%u / %u", aliveCount, capacity);

    int pushedColors = 0;
    if (ratio > 0.95f) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
        pushedColors = 1;
    } else if (ratio > 0.8f) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.95f, 0.6f, 0.15f, 1.0f));
        pushedColors = 1;
    }
    UI::ProgressBar(ratio, ImVec2(-FLT_MIN, 0), overlay);
    ImGui::PopStyleColor(pushedColors);
    UI::Tooltip("生存パーティクル数 / 最大パーティクル数\n上限に達すると新しいパーティクルは生成されません");
}

// ──────────────────────────────────────────────────────────
// 再生コントロール + エミッター位置
// ──────────────────────────────────────────────────────────

bool ParticleSystemDebugUI::ShowTransport(IParticleSystem* system, ParticleSystem* cpuSystem) {
    bool changed = false;

    if (ImGui::Button("再生")) { system->Play(); }
    UI::Tooltip("最初から再生します（経過時間・バーストをリセット）");

    UI::SameLine();
    if (ImGui::Button("停止")) { system->Stop(); }
    UI::Tooltip("放出を停止します。生存中のパーティクルは寿命まで残ります");

    if (cpuSystem) {
        UI::SameLine();
        if (ImGui::Button("クリア")) { cpuSystem->Clear(); }
        UI::Tooltip("生存中のパーティクルを即座に全て消去します");
    }

    Vector3 emitterPos = system->GetEmitterPosition();
    if (UI::DragVec3("エミッター位置", emitterPos, 0.01f)) {
        system->SetEmitterPosition(emitterPos);
        changed = true;
    }

    return changed;
}

// ──────────────────────────────────────────────────────────
// モジュール一覧
// ──────────────────────────────────────────────────────────

bool ParticleSystemDebugUI::ShowModules(IParticleSystem* system) {
    bool changed = false;

    // Unityのモジュール順に近い並び。メインのみ最初から開いておく。
    changed |= DrawModuleSection("メイン", system->GetMainModule(), true);
    changed |= DrawModuleSection("放出（レート・バースト）", system->GetEmissionModule());
    changed |= DrawModuleSection("形状（放出範囲）", system->GetShapeModule());
    changed |= DrawModuleSection("初速方向", system->GetVelocityModule());
    changed |= DrawModuleSection("色の変化", system->GetColorModule());
    changed |= DrawModuleSection("サイズの変化", system->GetSizeModule());
    changed |= DrawModuleSection("回転", system->GetRotationModule());
    changed |= DrawModuleSection("ノイズ（揺らぎ）", system->GetNoiseModule());
    changed |= DrawModuleSection("外力（重力・風）", system->GetForceModule());

    return changed;
}

bool ParticleSystemDebugUI::DrawModuleSection(const char* label, ParticleModule& module,
                                              bool defaultOpen) {
    bool changed = false;
    ImGui::PushID(label);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    const bool open = ImGui::CollapsingHeader(label, flags);

    // ヘッダー右端に有効トグルを重ねる（Unityのモジュールチェックボックス相当）
    const float toggleWidth = ImGui::GetFrameHeight() * 1.8f;
    const float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    UI::SameLine(rightEdge - toggleWidth - ImGui::GetStyle().FramePadding.x);
    bool enabled = module.IsEnabled();
    if (UI::Widgets::ToggleSwitch("##enabled", &enabled)) {
        module.SetEnabled(enabled);
        changed = true;
    }
    UI::Tooltip(enabled ? "モジュール有効（クリックで無効化）" : "モジュール無効（クリックで有効化）");

    if (open) {
        UI::Scope::IndentScope indent;
        {
            UI::Scope::DisabledScope ds(!module.IsEnabled());
            changed |= module.ShowImGui();
        }
        UI::Spacing();
    }

    ImGui::PopID();
    return changed;
}

// ──────────────────────────────────────────────────────────
// レンダラー設定
// ──────────────────────────────────────────────────────────

bool ParticleSystemDebugUI::ShowRendererSection(IParticleSystem* system, ParticleSystem* cpuSystem) {
    bool changed = false;
    ImGui::PushID("Renderer");

    if (ImGui::CollapsingHeader("レンダラー")) {
        UI::Scope::IndentScope indent;

        // ブレンドモード
        static const char* blendModeNames[] = {
            "なし", "通常（アルファ）", "加算（発光向け）", "減算", "乗算", "スクリーン"
        };
        int currentBlendMode = static_cast<int>(system->GetBlendMode());
        if (ImGui::Combo("ブレンドモード", &currentBlendMode, blendModeNames, IM_ARRAYSIZE(blendModeNames))) {
            system->SetBlendMode(static_cast<BlendMode>(currentBlendMode));
            changed = true;
        }
        UI::SameLine();
        UI::HelpMarker("背景との合成方法。\n炎・光などの発光系は「加算」、煙などは「通常」が定番です。");

        // 描画モード / ビルボード
        const bool isModelParticle = cpuSystem && cpuSystem->IsModelParticle();
        if (isModelParticle) {
            UI::Label("描画モード: 3Dモデル");
            UI::SameLine();
            UI::HelpMarker("描画モードはコードから SetModelResource() で切り替えます（UIからは変更できません）。");

            auto* modelResource = cpuSystem->GetModelResource();
            if (modelResource) {
                UI::HintF("モデル: %s（頂点数 %u）",
                    modelResource->GetFilePath().c_str(), modelResource->GetVertexCount());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "警告: モデルが設定されていません");
            }
        } else {
            static const char* billboardTypeNames[] = {
                "なし（回転をそのまま使用）", "カメラ正面（標準）", "Y軸固定", "スクリーン平行"
            };
            int currentBillboardType = static_cast<int>(system->GetBillboardType());
            if (ImGui::Combo("ビルボード", &currentBillboardType, billboardTypeNames, IM_ARRAYSIZE(billboardTypeNames))) {
                system->SetBillboardType(static_cast<BillboardType>(currentBillboardType));
                changed = true;
            }
            UI::SameLine();
            UI::HelpMarker("板ポリの向きの合わせ方。\n通常は「カメラ正面」、草や炎の柱など直立させたいものは「Y軸固定」。");
        }
        UI::Spacing();
    }

    ImGui::PopID();
    return changed;
}

// ──────────────────────────────────────────────────────────
// 統計情報（CPU版のみ）
// ──────────────────────────────────────────────────────────

void ParticleSystemDebugUI::ShowStatistics(ParticleSystem* particleSystem) {
    ImGui::PushID("Stats");
    if (ImGui::CollapsingHeader("統計情報")) {
        UI::Scope::IndentScope indent;
        const auto& stats = particleSystem->GetStatistics();

        ImGui::Text("生成数（累計）: %u", stats.totalParticlesCreated);
        ImGui::Text("消滅数（累計）: %u", stats.totalParticlesDestroyed);
        ImGui::Text("最大同時数: %u", stats.peakParticleCount);
        ImGui::Text("稼働時間: %.2f 秒", stats.systemRuntime);

        if (ImGui::Button("統計をリセット")) {
            particleSystem->ResetStatistics();
        }
        UI::Spacing();
    }
    ImGui::PopID();
}

} // namespace CoreEngine

#endif // USE_IMGUI
