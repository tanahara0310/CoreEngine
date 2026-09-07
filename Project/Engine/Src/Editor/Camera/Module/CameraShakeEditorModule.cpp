#include "pch.h"
#include "CameraShakeEditorModule.h"

#ifdef USE_IMGUI

#include "Editor/ImGui/ImGuiAll.h"
#include <algorithm>
#include <cstdio>

#include "Camera/Camera.h"
#include "Camera/CameraManager.h"
#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShaker.h"

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kWaveformLabels[] = {
            "Perlin (滑らか)",
            "Random (粗い)",
            "Sine (規則的)",
            "Kick (減衰振動)"
        };
        constexpr int kWaveformCount = static_cast<int>(sizeof(kWaveformLabels) / sizeof(kWaveformLabels[0]));

        constexpr const char* kSpaceLabels[] = {
            "カメラローカル",
            "ワールド"
        };
        constexpr int kSpaceCount = static_cast<int>(sizeof(kSpaceLabels) / sizeof(kSpaceLabels[0]));

        constexpr const char* kTimeModeLabels[] = {
            "Scaled (スローの影響を受ける)",
            "Unscaled (ポーズ中も進む)"
        };
        constexpr int kTimeModeCount = static_cast<int>(sizeof(kTimeModeLabels) / sizeof(kTimeModeLabels[0]));

        /// @brief 列挙値を選ぶコンボ。変わったら true
        template<typename Enum>
        bool EnumCombo(const char* label, Enum& value, const char* const* labels, int count)
        {
            int index = static_cast<int>(value);
            if (index < 0 || index >= count) {
                index = 0;
            }

            bool changed = false;
            if (ImGui::BeginCombo(label, labels[index])) {
                for (int i = 0; i < count; ++i) {
                    const bool selected = (i == index);
                    if (ImGui::Selectable(labels[i], selected)) {
                        value = static_cast<Enum>(i);
                        changed = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    }

    void CameraShakeEditorModule::Update(const CameraEditorContext& context)
    {
        // 揺れを進めるのは CameraShakeFeature の仕事。ここは UI だけ。
        (void)context;
    }

    void CameraShakeEditorModule::Draw(const CameraEditorContext& context)
    {
        auto& library = CameraShake::GetPresetLibrary();
        const auto& presets = library.GetAll();

        // 初回は先頭を選んでおく（何も選ばれていない画面を見せない）
        if (selectedPresetName_.empty() && !presets.empty()) {
            selectedPresetName_ = presets.front().name;
        }

        if (!CameraShake::IsAvailable()) {
            UI::Hint("シェイクの委譲先がありません。シーン再生中のみ試せます。");
        }

        // ===== プリセット一覧 =====
        UI::SectionHeader("プリセット");

        if (auto lb = UI::Scope::ListBoxScope("##ShakePresets", ImVec2(-1.0f, 140.0f))) {
            for (const auto& preset : presets) {
                const bool selected = (preset.name == selectedPresetName_);
                if (ImGui::Selectable(preset.name.c_str(), selected)) {
                    selectedPresetName_ = preset.name;
                    statusMessage_.clear();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }

        UI::InputText("新規プリセット名", newPresetNameBuffer_, sizeof(newPresetNameBuffer_));

        if (ImGui::Button("追加")) {
            const std::string name = newPresetNameBuffer_;
            if (name.empty()) {
                statusMessage_ = "名前を入力してください。";
            } else if (library.Find(name)) {
                statusMessage_ = "同じ名前のプリセットが既にあります。";
            } else {
                // 選択中のものを土台にする。ゼロから組むより、近いものを複製して
                // 詰めるほうが実際の作り方に近い。
                const CameraShakeParams* base = library.Find(selectedPresetName_);
                library.Set(name, base ? *base : CameraShakeParams{});
                selectedPresetName_ = name;
                newPresetNameBuffer_[0] = '\0';
                statusMessage_.clear();
            }
        }

        UI::SameLine();
        if (ImGui::Button("削除")) {
            if (presets.size() <= 1) {
                statusMessage_ = "最後の 1 件は削除できません。";
            } else {
                library.Remove(selectedPresetName_);
                selectedPresetName_.clear();
                statusMessage_.clear();
            }
        }

        UI::SameLine();
        if (ImGui::Button("保存")) {
            statusMessage_ = library.Save()
                ? "プリセットを保存しました。"
                : "保存に失敗しました。";
        }

        UI::SameLine();
        if (ImGui::Button("読み込み")) {
            statusMessage_ = library.Load()
                ? "プリセットを読み込みました。"
                : "保存済みファイルがありません。";
            selectedPresetName_.clear();
        }

        UI::SameLine();
        if (ImGui::Button("組み込みに戻す")) {
            library.ResetToBuiltIn();
            selectedPresetName_.clear();
            statusMessage_ = "組み込みプリセットに戻しました（保存はされていません）。";
        }

        if (!statusMessage_.empty()) {
            UI::Hint(statusMessage_.c_str());
        }

        UI::Separator();

        if (DrawParameters(context)) {
            // 値を触ったら、そのまま試せるようにしておく（保存は明示操作）。
        }

        UI::Separator();
        DrawRuntimeStatus();
    }

    bool CameraShakeEditorModule::DrawParameters(const CameraEditorContext& context)
    {
        auto& library = CameraShake::GetPresetLibrary();
        CameraShakeParams* params = library.FindMutable(selectedPresetName_);
        if (!params) {
            UI::Hint("プリセットが選択されていません。");
            return false;
        }

        UI::SectionHeader(selectedPresetName_.c_str());

        bool changed = false;

        UI::Hint("回転がシェイクの主役です。位置を大きくすると壁や地面へめり込みます。");
        changed |= UI::DragVec3("回転振幅 (度)", params->rotationAmplitude, 0.05f, 0.0f, 45.0f);
        changed |= UI::DragVec3("位置振幅 (m)", params->positionAmplitude, 0.01f, 0.0f, 5.0f);
        changed |= UI::DragFloat("画角振幅 (度)", params->fovAmplitude, 0.05f, 0.0f, 30.0f, "%.2f");

        changed |= UI::DragFloat("周波数 (Hz)", params->frequency, 0.1f, 0.1f, 120.0f, "%.1f");
        changed |= UI::DragVec3("軸ごとの周波数倍率", params->frequencyScale, 0.01f, 0.1f, 4.0f);

        changed |= UI::DragFloat("継続時間 (秒)", params->duration, 0.01f, 0.0f, 30.0f, "%.2f");
        UI::SameLine();
        UI::Hint("0 で無限（明示的に止めるまで続く）");
        changed |= UI::DragFloat("立ち上がり (秒)", params->attack, 0.01f, 0.0f, 5.0f, "%.2f");

        changed |= EnumCombo("波形", params->waveform, kWaveformLabels, kWaveformCount);
        changed |= EnumCombo("座標系", params->space, kSpaceLabels, kSpaceCount);
        changed |= EnumCombo("時間軸", params->timeMode, kTimeModeLabels, kTimeModeCount);

        UI::SectionHeader("方向性");
        changed |= UI::DragVec3("向き", params->direction, 0.05f, -1.0f, 1.0f);
        changed |= UI::SliderFloat("方向性の強さ", params->directionality, 0.0f, 1.0f, "%.2f");

        UI::SectionHeader("空間減衰");
        bool falloff = params->useWorldFalloff;
        if (UI::Widgets::ToggleSwitch("発生源からの距離で弱める", &falloff)) {
            params->useWorldFalloff = falloff;
            changed = true;
        }
        if (params->useWorldFalloff) {
            changed |= UI::DragFloat("減衰しない距離", params->innerRadius, 0.1f, 0.0f, 500.0f, "%.1f");
            changed |= UI::DragFloat("振幅が 0 になる距離", params->outerRadius, 0.1f, 0.1f, 1000.0f, "%.1f");
            if (params->outerRadius <= params->innerRadius) {
                params->outerRadius = params->innerRadius + 0.1f;
            }
        }

        UI::Separator();

        if (ImGui::Button("試す")) {
            CameraShake::PlayPreset(selectedPresetName_);
        }

        UI::SameLine();
        if (ImGui::Button("発生源つきで試す")) {
            // ゲーム視点カメラの少し前方を発生源にする。距離減衰の効きを実際の
            // 位置関係で確かめられるようにするため。
            Vector3 origin{ 0.0f, 0.0f, 0.0f };
            if (context.cameraManager) {
                if (Camera* camera = context.cameraManager->GetActiveCamera(CameraType::Camera3D)) {
                    origin = camera->GetTranslate() + camera->GetForward() * 10.0f;
                }
            }
            if (CameraShaker* shaker = CameraShake::GetActiveShaker()) {
                shaker->Play(*params, origin);
            }
        }

        UI::SameLine();
        if (ImGui::Button("全て停止")) {
            CameraShake::StopAll();
        }

        return changed;
    }

    void CameraShakeEditorModule::DrawRuntimeStatus()
    {
        UI::SectionHeader("実行中");

        CameraShaker* shaker = CameraShake::GetActiveShaker();
        if (!shaker) {
            UI::Hint("シーン再生中のみ表示されます。");
            return;
        }

        ImGui::Text("trauma: %.2f", shaker->GetTrauma());
        ImGui::ProgressBar(std::clamp(shaker->GetTrauma(), 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));

        float globalScale = CameraShake::GetGlobalScale();
        if (UI::SliderFloat("全体強度", globalScale, 0.0f, 2.0f, "%.2f")) {
            CameraShake::SetGlobalScale(globalScale);
        }
        UI::Hint("画面揺れは 3D 酔いの原因になります。0 にできる口を設定画面へ出してください。");

        if (ImGui::Button("trauma を 0.3 加算")) {
            CameraShake::AddTrauma(0.3f);
        }
    }
}

#endif // USE_IMGUI
