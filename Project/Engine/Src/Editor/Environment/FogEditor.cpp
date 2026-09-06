#include "pch.h"
#include "FogEditor.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Fog/FogManager.h"
#include "Graphics/Fog/Settings/FogCVars.h"
#include "Graphics/Render/RenderDomainContext.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include <iterator>
#endif

namespace CoreEngine {

    namespace {
        constexpr const char* kEditorLabel = "Height Fog";
        constexpr const char* kCVarPrefix = "r.Fog";

#ifdef USE_IMGUI
        /// @brief プリセット 1 件分。単位系は FogCVars と同じ
        struct FogPreset {
            const char* name;
            const char* description;
            Vector4 color;
            float colorIntensity; ///< 色の明るさ倍率（HDR）
            float density;        ///< [1/m]
            float heightFalloff;  ///< [1/m]
            float heightRefM;     ///< [m]
            float maxOpacity;
            bool  applyToSky;
            float skyColorBlend;  ///< 空の色へ寄せる量 [0,1]
            Vector4 sunTint;      ///< 太陽方向でのフォグの色味（基準色への倍率）
            float sunGain;        ///< 太陽方向でのフォグの明るさ倍率（1 で無効）
            float sunExponent;    ///< 内散乱ローブの鋭さ
        };

        /// プリセット一覧。index 0 は FogCVars の既定値と同じ
        const FogPreset* FogPresets()
        {
            static const FogPreset presets[] = {
                { "屋外の霞み（既定）",
                  "遠景がゆるく霞む、汎用の屋外フォグ。\n"
                  "水平方向 50m で透過率 0.37。高いところほど薄くなる。\n"
                  "大気があるシーンではフォグ色が空の色に一致する。",
                  { 0.62f, 0.68f, 0.75f, 1.0f }, 1.0f, 0.02f, 0.1f, 0.0f, 1.0f, true,
                  1.0f, { 1.0f, 0.82f, 0.60f, 1.0f }, 2.5f, 16.0f },
                { "地表の霧",
                  "高さ 2m 付近から下に溜まる薄い霧。\n"
                  "立っているキャラの足元だけが白む。",
                  { 0.75f, 0.78f, 0.82f, 1.0f }, 1.5f, 0.15f, 1.5f, 2.0f, 1.0f, true,
                  0.6f, { 1.0f, 0.86f, 0.68f, 1.0f }, 2.0f, 24.0f },
                { "霧の海",
                  "境界のはっきりした雲海。HeightRef が「水面」の高さになる。\n"
                  "地形の天面は素通りし、下に落ちる視線だけがフォグ色で埋まる。\n"
                  "様式的な見た目を保つため空色ブレンドは切ってある。",
                  { 0.86f, 0.89f, 0.93f, 1.0f }, 3.0f, 1.0f, 8.0f, 0.2f, 1.0f, true,
                  0.0f, { 1.0f, 0.95f, 0.86f, 1.0f }, 1.6f, 8.0f },
                { "濃霧",
                  "視界 30m 程度の高さ非依存フォグ（＝古典的な指数距離フォグ）。\n"
                  "空も塗り潰されるので、屋内・閉所や演出向け。",
                  { 0.72f, 0.74f, 0.76f, 1.0f }, 1.5f, 0.1f, 0.05f, 0.0f, 1.0f, true,
                  0.0f, { 1.0f, 0.97f, 0.92f, 1.0f }, 1.3f, 4.0f },
            };
            return presets;
        }

        constexpr int kFogPresetCount = 4;

        void ApplyFogPreset(const FogPreset& preset)
        {
            FogCVars::Color.Set(preset.color);
            FogCVars::ColorIntensity.Set(preset.colorIntensity);
            FogCVars::Density.Set(preset.density);
            FogCVars::HeightFalloff.Set(preset.heightFalloff);
            FogCVars::HeightRefM.Set(preset.heightRefM);
            FogCVars::MaxOpacity.Set(preset.maxOpacity);
            FogCVars::ApplyToSky.Set(preset.applyToSky);
            FogCVars::SkyColorBlend.Set(preset.skyColorBlend);
            FogCVars::SunTint.Set(preset.sunTint);
            FogCVars::SunScatteringGain.Set(preset.sunGain);
            FogCVars::SunScatteringExponent.Set(preset.sunExponent);
        }

        /// @brief UE 風の (?) ホバーツールチップ（ラベルの右に付ける）
        void HelpMarker(const char* desc)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", desc);
            }
        }
#endif // USE_IMGUI
    }

    void FogEditor::Initialize(EngineSystem& engine)
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

    FogEditor::~FogEditor()
    {
#ifdef USE_IMGUI
        // エンジン終了時にドロワーがダングリングしないよう登録を解除する。
        // engine_->GetDebugSubsystem() を呼び直さないこと（サブシステム一括破棄中に走るため）
        if (gameDebugUI_) {
            gameDebugUI_->UnregisterEnvironmentEditor(kEditorLabel, this);
        }
#endif
    }

    void FogEditor::DrawContent()
    {
#ifdef USE_IMGUI
        ImGui::PushID("HeightFog");

        // シェーダーのコンパイルに失敗していると、有効にしても何も起きない。
        // 「設定は正しいのに出ない」で悩まないよう、原因をここで切り分けて出す
        const FogManager* fog = GetFogManager();
        if (!fog) {
            ImGui::TextDisabled("FogManager がありません");
            ImGui::PopID();
            return;
        }
        if (!fog->IsPipelineReady()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "合成パイプラインの構築に失敗しています（Shader ログを確認してください）");
            UI::Separator();
        }

        bool enabled = FogCVars::Enabled.Get();
        if (ImGui::Checkbox("フォグを有効にする", &enabled)) {
            FogCVars::Enabled.Set(enabled);
        }
        HelpMarker(
            "大気散乱（Aerial Perspective）とは効く距離帯が 2 桁以上違う別の媒質なので、\n"
            "両方同時に有効にしてよい。重ねた結果は放射伝達として正しい。");

        UI::Separator();
        ImGui::TextUnformatted("プリセット");
        DrawPresetButtons();

        UI::Separator();
        if (ImGui::CollapsingHeader("詳細設定", ImGuiTreeNodeFlags_DefaultOpen)) {
            CVarUI::DrawTree(kCVarPrefix);

            UI::Separator();
            if (ImGui::Button("デフォルトに戻す")) {
                CVarUI::ResetTree(kCVarPrefix);
            }
        }

        ImGui::PopID();
#endif // USE_IMGUI
    }

    void FogEditor::DrawPresetButtons()
    {
#ifdef USE_IMGUI
        for (int i = 0; i < kFogPresetCount; ++i) {
            const FogPreset& preset = FogPresets()[i];
            if (ImGui::Button(preset.name)) {
                ApplyFogPreset(preset);
                // プリセットを押した時点で見たいはずなので、無効なら一緒に有効化する
                FogCVars::Enabled.Set(true);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", preset.description);
            }
        }
#endif // USE_IMGUI
    }

    FogManager* FogEditor::GetFogManager() const
    {
        if (!engine_ || !engine_->GetRenderDomainContext()) {
            return nullptr;
        }
        return engine_->GetRenderDomainContext()->GetFogManager();
    }
}
