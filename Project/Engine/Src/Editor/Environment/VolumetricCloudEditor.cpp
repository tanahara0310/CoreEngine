#include "pch.h"
#include "VolumetricCloudEditor.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Cloud/Settings/CloudCVars.h"
#include "Graphics/Cloud/VolumetricCloudManager.h"
#include "Graphics/Render/RenderDomainContext.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/CVar/CVarUndoStack.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string_view>
#include <unordered_set>
#include <vector>
#endif

namespace CoreEngine {

    namespace {
        constexpr const char* kEditorLabel = "Volumetric Cloud";

#ifdef USE_IMGUI
        constexpr float kPi = 3.14159265358979323846f;

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

        /// @brief 曇り度に応じた縁の侵食量（プリセットの実測点を通る折れ線）
        /// @details カバレッジが低いのに侵食が強いと雲塊が粒状に崩れ、
        ///          高いときは雲デッキになるので侵食を弱める
        float ErosionForCoverage(float coverage)
        {
            struct Point { float coverage; float erosion; };
            constexpr Point kCurve[] = {
                { 0.30f, 0.10f }, { 0.50f, 0.16f }, { 0.62f, 0.22f }, { 0.85f, 0.10f },
            };
            constexpr int kPointCount = static_cast<int>(std::size(kCurve));

            if (coverage <= kCurve[0].coverage) {
                return kCurve[0].erosion;
            }
            for (int i = 1; i < kPointCount; ++i) {
                if (coverage <= kCurve[i].coverage) {
                    const float t = (coverage - kCurve[i - 1].coverage)
                        / (kCurve[i].coverage - kCurve[i - 1].coverage);
                    return kCurve[i - 1].erosion + (kCurve[i].erosion - kCurve[i - 1].erosion) * t;
                }
            }
            return kCurve[kPointCount - 1].erosion;
        }

        /// @brief スライダーの編集終了で CVar の Undo 記録と自動保存を確定する
        void CommitOnDeactivate(ICVar* cvar)
        {
            if (!ImGui::IsItemDeactivatedAfterEdit()) {
                return;
            }
            CVarUndoStack::Get().CommitEdit(cvar);
            CVarRegistry::Get().NotifyCommit();
        }

        /// @brief 連動スライダーの確定: 主従 2 CVar を 1 バッチ（Ctrl+Z 1 回）の Undo にする
        /// @details CVarUndoStack の編集セッションは同時に 1 本しか持てないため、
        ///          ドラッグ中は主 CVar だけをセッションにし、従 CVar は編集開始時の値へ
        ///          一旦戻してからセッションを作り直して同じバッチへ積む
        void CommitCoupledEdit(CVar<float>& primary, CVar<float>& secondary, float secondaryOldValue)
        {
            auto& undoStack = CVarUndoStack::Get();
            undoStack.BeginBatch();
            undoStack.CommitEdit(&primary);

            const float secondaryNew = secondary.Get();
            secondary.Set(secondaryOldValue);
            undoStack.BeginEdit(&secondary);
            secondary.Set(secondaryNew);
            undoStack.CommitEdit(&secondary);

            undoStack.EndBatch();
            CVarRegistry::Get().NotifyCommit();
        }

        /// @brief CVar 1 本を任意ラベルのスライダーで編集する（範囲は CVar 定義から取る）
        void MetaSliderFloat(const char* label, CVar<float>& cvar, const char* format, const char* help)
        {
            const CVarRange range = cvar.GetRange();
            float value = cvar.Get();
            if (ImGui::SliderFloat(label, &value, range.min, range.max, format)) {
                CVarUndoStack::Get().BeginEdit(&cvar);
                cvar.Set(value);
            }
            CommitOnDeactivate(&cvar);
            if (help) {
                HelpMarker(help);
            }
        }

        /// @brief グループ 1 つ分を折りたたみヘッダーで描く
        void DrawCVarGroup(const char* label, ICVar* const* items, size_t count)
        {
            if (!ImGui::CollapsingHeader(label)) {
                return;
            }
            for (size_t i = 0; i < count; ++i) {
                CVarUI::DrawWidget(items[i]);
            }
        }

        // ---- 詳細設定のグループ分け ----
        // 追記漏れは「その他（未分類）」ヘッダーへ自動で出るので UI から消えることはない

        ICVar* const kShapeGroup[] = {
            &CloudCVars::GlobalCoverage,
            &CloudCVars::DensityScale,
            &CloudCVars::LayerBottomAltitudeM,
            &CloudCVars::LayerThicknessM,
            &CloudCVars::BaseNoiseScaleM,
            &CloudCVars::DetailNoiseScaleM,
            &CloudCVars::DetailErosionStrength,
            &CloudCVars::WeatherMapScaleM,
            &CloudCVars::BaseNoiseVerticalScale,
            &CloudCVars::HeightSkewM,
            &CloudCVars::CloudStreetStretch,
            &CloudCVars::CloudTopVariation,
            &CloudCVars::WindDirX,
            &CloudCVars::WindDirZ,
            &CloudCVars::WindSpeedMPerS,
            &CloudCVars::PaintRegionCenterX,
            &CloudCVars::PaintRegionCenterZ,
            &CloudCVars::PaintRegionSizeM,
            &CloudCVars::PaintEdgeFade,
        };

        ICVar* const kLightingGroup[] = {
            &CloudCVars::SunLightScale,
            &CloudCVars::DropletDiameterUm,
            &CloudCVars::MaxPhase,
            &CloudCVars::AmbientIntensity,
            &CloudCVars::AmbientCosZenith,
            &CloudCVars::AmbientBottomOcclusion,
            &CloudCVars::AmbientChroma,
            &CloudCVars::AmbientGroundStrength,
            &CloudCVars::BeerPowderStrength,
            &CloudCVars::LightMarchCoverage,
            &CloudCVars::LightMarchConeSpread,
            &CloudCVars::MaxSunOpticalDepth,
            &CloudCVars::MsAttenuation,
            &CloudCVars::MsContribution,
            &CloudCVars::MsEccentricity,
        };

        ICVar* const kQualityGroup[] = {
            &CloudCVars::MaxSteps,
            &CloudCVars::ResolutionDivisor,
            &CloudCVars::EarlyExitTransmittance,
            &CloudCVars::MaxMarchDistanceM,
            &CloudCVars::DetailFadeDistanceM,
            &CloudCVars::NoiseLodBias,
            &CloudCVars::FarFadeWidthM,
            &CloudCVars::HazeDistanceM,
            &CloudCVars::UpsampleDepthTolerance,
            &CloudCVars::ReprojectEnabled,
            &CloudCVars::ReprojectBlendMin,
            &CloudCVars::ReprojectTolerance,
        };

        ICVar* const kCirrusGroup[] = {
            &CloudCVars::CirrusCoverage,
            &CloudCVars::CirrusAltitudeM,
            &CloudCVars::CirrusDensity,
            &CloudCVars::CirrusScaleM,
            &CloudCVars::CirrusStretch,
            &CloudCVars::CirrusWindScale,
        };

        ICVar* const kGodRayGroup[] = {
            &CloudCVars::GodRayEnabled,
            &CloudCVars::GodRayIntensity,
            &CloudCVars::GodRayMieBoost,
            &CloudCVars::GodRayMaxDistanceM,
            &CloudCVars::GodRayStepCount,
            &CloudCVars::CloudShadowRegionSizeM,
            &CloudCVars::SceneShadowStrength,
        };

        /// @brief 全グループでカバー済みの CVar 名集合（未分類検出用）
        const std::unordered_set<std::string_view>& CoveredCVarNames()
        {
            static const std::unordered_set<std::string_view> covered = [] {
                std::unordered_set<std::string_view> names;
                auto addAll = [&names](ICVar* const* items, size_t count) {
                    for (size_t i = 0; i < count; ++i) {
                        names.insert(items[i]->GetName());
                    }
                };
                addAll(kShapeGroup, std::size(kShapeGroup));
                addAll(kLightingGroup, std::size(kLightingGroup));
                addAll(kQualityGroup, std::size(kQualityGroup));
                addAll(kCirrusGroup, std::size(kCirrusGroup));
                addAll(kGodRayGroup, std::size(kGodRayGroup));
                return names;
            }();
            return covered;
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

    void VolumetricCloudEditor::DrawWeatherSliders()
    {
#ifdef USE_IMGUI
        // 曇り度は GlobalCoverage と DetailErosionStrength を連動させる
        // （カバレッジだけ下げると雲塊が粒状に崩れるため。個別調整は詳細設定から）
        float coverage = CloudCVars::GlobalCoverage.Get();
        const bool coverageChanged = ImGui::SliderFloat("曇り度", &coverage, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemActivated()) {
            erosionOnEditStart_ = CloudCVars::DetailErosionStrength.Get();
        }
        if (coverageChanged) {
            CVarUndoStack::Get().BeginEdit(&CloudCVars::GlobalCoverage);
            CloudCVars::GlobalCoverage.Set(coverage);
            CloudCVars::DetailErosionStrength.Set(ErosionForCoverage(coverage));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            CommitCoupledEdit(CloudCVars::GlobalCoverage,
                CloudCVars::DetailErosionStrength, erosionOnEditStart_);
        }
        HelpMarker("空を覆う雲の割合。縁の侵食量（DetailErosionStrength）も\n"
            "粒状に崩れない値へ連動して動きます。");

        MetaSliderFloat("雲の濃さ", CloudCVars::DensityScale, "%.3f",
            "雲の密度。上げるほど厚く不透明になり、下げると薄い霞になります。");
        MetaSliderFloat("雲底の高さ", CloudCVars::LayerBottomAltitudeM, "%.0f m",
            "雲層の下端の高度。");
        MetaSliderFloat("層の厚み", CloudCVars::LayerThicknessM, "%.0f m",
            "雲層の縦の厚み。厚いほど背の高い雲が育ちます。");

        // 風向きは角度 1 本で WindDirX/Z の 2 CVar へ分解する（+X 基準の反時計回り）
        float angleDeg = std::atan2(CloudCVars::WindDirZ.Get(), CloudCVars::WindDirX.Get())
            * (180.0f / kPi);
        if (angleDeg < 0.0f) {
            angleDeg += 360.0f;
        }
        const bool windChanged = ImGui::SliderFloat("風向き", &angleDeg, 0.0f, 360.0f, "%.0f°");
        if (ImGui::IsItemActivated()) {
            windDirZOnEditStart_ = CloudCVars::WindDirZ.Get();
        }
        if (windChanged) {
            const float rad = angleDeg * (kPi / 180.0f);
            CVarUndoStack::Get().BeginEdit(&CloudCVars::WindDirX);
            CloudCVars::WindDirX.Set(std::cos(rad));
            CloudCVars::WindDirZ.Set(std::sin(rad));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            CommitCoupledEdit(CloudCVars::WindDirX, CloudCVars::WindDirZ, windDirZOnEditStart_);
        }
        HelpMarker("雲が流れていく方角（+X 方向が 0°、+Z 方向が 90°）。\n"
            "配置ペイントのマップ左下にも矢印で表示されます。");

        MetaSliderFloat("風速", CloudCVars::WindSpeedMPerS, "%.1f m/s",
            "雲の流れる速さ。");
#endif
    }

    void VolumetricCloudEditor::DrawPaintSection([[maybe_unused]] VolumetricCloudManager& manager)
    {
#ifdef USE_IMGUI
        CloudResources& resources = manager.GetResources();
        if (!manager.AreNoiseTexturesReady()
            || resources.weatherChannelSrvs[0].gpuHandle.ptr == 0) {
            ImGui::TextDisabled("ウェザーマップが未生成のためペイントできません");
            return;
        }
        const VolumetricCloudParameters& params = manager.GetParameters();

        // ---- ツール ----
        ImGui::RadioButton("雲を置く", &paintTool_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("晴れさせる", &paintTool_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("ブラシ跡を消す", &paintTool_, 2);
        HelpMarker("雲を置く: 下の「置く雲の性質」をブラシ位置へ描き込みます。\n"
            "晴れさせる: ブラシ位置を雲量 0 で塗り、晴れ間を作ります。\n"
            "ブラシ跡を消す: ペイントを剥がして手続き生成の空へ戻します。");

        // ブラシは「こういう雲を置く」という 1 組の性質を運ぶ
        if (paintTool_ == 0) {
            ImGui::SliderFloat("雲のタイプ", &typeTarget_, 0.0f, 1.0f, "%.2f");
            HelpMarker("0 = 平らな層雲、1 = 背の高い積乱雲。高度勾配の形が変わります。");

            ImGui::SliderFloat("雲頂の高さ", &topTarget_, 0.0f, 1.0f, "%.2f");
            HelpMarker("置く雲の背の高さ。\n"
                "詳細設定の CloudTopVariation が 0 のままだと効きません。");
            if (params.cloudTopVariation <= 0.0f) {
                ImGui::SameLine();
                if (ImGui::SmallButton("有効化")) {
                    auto& undoStack = CVarUndoStack::Get();
                    undoStack.BeginEdit(&CloudCVars::CloudTopVariation);
                    CloudCVars::CloudTopVariation.Set(0.4f);
                    undoStack.CommitEdit(&CloudCVars::CloudTopVariation);
                    CVarRegistry::Get().NotifyCommit();
                }
                HelpMarker("CloudTopVariation を 0.4 にして雲頂の高さを効くようにします。");
            }
        }

        ImGui::SliderFloat("ブラシ半径", &brushRadiusM_, 500.0f, 20000.0f, "%.0f m",
            ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("ブラシ強さ", &brushStrength_, 0.05f, 1.0f, "%.2f");

        // ---- 3D ビューを見ながらの配置 ----
        float aimX = 0.0f;
        float aimZ = 0.0f;
        const bool hasAim = manager.GetCameraAimOnCloudLayer(aimX, aimZ);
        if (ImGui::Button("視線の先に雲を置く")) {
            if (hasAim) {
                VolumetricCloudManager::WeatherPaintStamp stamp;
                stamp.worldX = aimX;
                stamp.worldZ = aimZ;
                stamp.radiusM = brushRadiusM_;
                stamp.strength = 1.0f;
                stamp.coverage = 1.0f;
                stamp.cloudType = typeTarget_;
                stamp.cloudTop = topTarget_;
                manager.PaintWeather(stamp);
                manager.SaveWeatherPaint();
            }
        }
        HelpMarker("カメラの視線が雲層と交わる位置へ 1 回スタンプします。\n"
            "空を見上げて置きたい方向を向いてから押してください。");
        if (hasAim) {
            ImGui::SameLine();
            ImGui::TextDisabled("着地点 X=%.1fkm Z=%.1fkm", aimX / 1000.0f, aimZ / 1000.0f);
        } else {
            ImGui::SameLine();
            ImGui::TextDisabled("視線が雲層と交わりません");
        }

        // ---- ペイント領域（ワールド固定・タイルしない） ----
        MetaSliderFloat("ペイント領域の広さ", CloudCVars::PaintRegionSizeM, "%.0f m",
            "ペイントを置けるワールド上の矩形の一辺。この外は手続き生成のままです。");
        if (ImGui::Button("領域をカメラ位置へ移動")) {
            const Vector3 cam = manager.GetCameraWorldPosition();
            auto& undoStack = CVarUndoStack::Get();
            undoStack.BeginBatch();
            undoStack.BeginEdit(&CloudCVars::PaintRegionCenterX);
            CloudCVars::PaintRegionCenterX.Set(cam.x);
            undoStack.CommitEdit(&CloudCVars::PaintRegionCenterX);
            undoStack.BeginEdit(&CloudCVars::PaintRegionCenterZ);
            CloudCVars::PaintRegionCenterZ.Set(cam.z);
            undoStack.CommitEdit(&CloudCVars::PaintRegionCenterZ);
            undoStack.EndBatch();
            CVarRegistry::Get().NotifyCommit();
        }
        HelpMarker("ペイント領域の中心を現在のカメラ位置へ移します。\n"
            "既に描いたペイントはテクスチャ側に残るため、領域ごと移動します。");

        // ---- マップ表示（上空視点。+X が右、+Z が上、カメラが常に中心）----
        const float tileM = std::max(params.weatherMapScaleM, 1.0f);
        const float regionM = std::max(params.paintRegionSizeM, 1.0f);
        if (mapSpanM_ <= 0.0f) {
            mapSpanM_ = regionM * 1.5f;
        }
        ImGui::SliderFloat("表示範囲", &mapSpanM_, 5000.0f, 400000.0f, "%.0f m",
            ImGuiSliderFlags_Logarithmic);

        static const char* const kMapChannelNames[] = { "雲量", "雲タイプ", "雲頂高さ" };
        ImGui::Combo("表示チャンネル", &mapChannel_, kMapChannelNames, 3);
        HelpMarker("マップに表示する値。背景（手続き生成）と重ねたペイントの両方が切り替わります。");

        const float mapSize = std::clamp(ImGui::GetContentRegionAvail().x, 128.0f, 384.0f);
        const ImVec2 mapPos = ImGui::GetCursorScreenPos();
        const ImVec2 mapEnd(mapPos.x + mapSize, mapPos.y + mapSize);
        const ImVec2 mapCenter(mapPos.x + mapSize * 0.5f, mapPos.y + mapSize * 0.5f);

        const Vector3 cameraPos = manager.GetCameraWorldPosition();
        const float pxPerM = mapSize / mapSpanM_;

        // ワールド XZ → マップ上のスクリーン座標（画面の上が +Z）
        auto worldToScreen = [&](float wx, float wz) {
            return ImVec2(mapCenter.x + (wx - cameraPos.x) * pxPerM,
                          mapCenter.y - (wz - cameraPos.z) * pxPerM);
        };

        ImGui::InvisibleButton("##weatherPaintCanvas", ImVec2(mapSize, mapSize));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(mapPos, mapEnd, true);

        // 背景 = 手続き生成の天候マップ。ワールドでタイルするので、表示範囲と交差する
        // タイルを並べて描く（ImGui の静的サンプラは CLAMP でラップできない）
        const int tileX0 = static_cast<int>(std::floor((cameraPos.x - mapSpanM_ * 0.5f) / tileM));
        const int tileX1 = static_cast<int>(std::floor((cameraPos.x + mapSpanM_ * 0.5f) / tileM));
        const int tileZ0 = static_cast<int>(std::floor((cameraPos.z - mapSpanM_ * 0.5f) / tileM));
        const int tileZ1 = static_cast<int>(std::floor((cameraPos.z + mapSpanM_ * 0.5f) / tileM));
        constexpr int kMaxTilesPerAxis = 12;   // 引きすぎたときに描画枚数が膨らまないように
        if (tileX1 - tileX0 < kMaxTilesPerAxis && tileZ1 - tileZ0 < kMaxTilesPerAxis) {
            const ImTextureID weatherTexture =
                (ImTextureID)resources.weatherChannelSrvs[mapChannel_].gpuHandle.ptr;
            for (int tz = tileZ0; tz <= tileZ1; ++tz) {
                for (int tx = tileX0; tx <= tileX1; ++tx) {
                    const ImVec2 topLeft = worldToScreen(tx * tileM, (tz + 1) * tileM);
                    const ImVec2 bottomRight = worldToScreen((tx + 1) * tileM, tz * tileM);
                    drawList->AddImage(weatherTexture, topLeft, bottomRight,
                        ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
                }
            }
        }

        // ペイントを領域の矩形へ重ねる。影響度がアルファなので塗った所だけ乗る
        const ImVec2 regionMin = worldToScreen(params.paintRegionCenterX - regionM * 0.5f,
                                               params.paintRegionCenterZ + regionM * 0.5f);
        const ImVec2 regionMax = worldToScreen(params.paintRegionCenterX + regionM * 0.5f,
                                               params.paintRegionCenterZ - regionM * 0.5f);
        if (resources.paintChannelSrvs[mapChannel_].gpuHandle.ptr != 0) {
            drawList->AddImage((ImTextureID)resources.paintChannelSrvs[mapChannel_].gpuHandle.ptr,
                regionMin, regionMax, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        drawList->AddRect(regionMin, regionMax, IM_COL32(255, 230, 120, 160));

        // カメラ位置マーカー（常にマップ中心）と視線方向
        const ImVec2 cameraPt(mapPos.x + mapSize * 0.5f, mapPos.y + mapSize * 0.5f);

        const Vector3 forward = manager.GetCameraForward();
        const float forwardLenXZ = std::sqrt(forward.x * forward.x + forward.z * forward.z);
        constexpr ImU32 kCameraColor = IM_COL32(255, 160, 90, 255);
        if (forwardLenXZ > 1e-3f) {
            const float baseAngle = std::atan2(-forward.z, forward.x);   // 画面座標は +Z が上なので Z を反転
            for (const float offset : { -kPi / 6.0f, kPi / 6.0f }) {     // 視野の目安 ±30°
                const float a = baseAngle + offset;
                drawList->AddLine(cameraPt,
                    ImVec2(cameraPt.x + std::cos(a) * 22.0f, cameraPt.y + std::sin(a) * 22.0f),
                    kCameraColor, 1.5f);
            }
        }
        drawList->AddCircleFilled(cameraPt, 4.0f, kCameraColor);

        // 風向き矢印（マップ左下）
        const float windLen = std::sqrt(params.windDirX * params.windDirX
            + params.windDirZ * params.windDirZ);
        if (windLen > 1e-4f) {
            const ImVec2 windOrigin(mapPos.x + 20.0f, mapEnd.y - 20.0f);
            const ImVec2 windDir(params.windDirX / windLen, -params.windDirZ / windLen);
            const ImVec2 windTip(windOrigin.x + windDir.x * 16.0f, windOrigin.y + windDir.y * 16.0f);
            constexpr ImU32 kWindColor = IM_COL32(120, 200, 255, 255);
            drawList->AddLine(windOrigin, windTip, kWindColor, 2.0f);
            drawList->AddCircleFilled(windTip, 3.0f, kWindColor);
        }

        // 視線の着地点（3D ビューで見ている位置）
        if (hasAim) {
            const ImVec2 aimPt = worldToScreen(aimX, aimZ);
            constexpr ImU32 kAimColor = IM_COL32(255, 220, 120, 220);
            drawList->AddCircle(aimPt, 5.0f, kAimColor, 0, 1.5f);
            drawList->AddLine(ImVec2(aimPt.x - 8.0f, aimPt.y), ImVec2(aimPt.x + 8.0f, aimPt.y), kAimColor);
            drawList->AddLine(ImVec2(aimPt.x, aimPt.y - 8.0f), ImVec2(aimPt.x, aimPt.y + 8.0f), kAimColor);
        }

        // ブラシカーソル
        if (hovered || active) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float radiusPx = std::max(brushRadiusM_ * pxPerM, 2.0f);
            drawList->AddCircle(mouse, radiusPx, IM_COL32(255, 255, 255, 200), 0, 1.5f);
        }

        drawList->AddRect(mapPos, mapEnd, IM_COL32(180, 180, 180, 255));
        drawList->PopClipRect();

        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            VolumetricCloudManager::WeatherPaintStamp stamp;
            // スクリーン座標 → ワールド XZ（領域外へ出た分は PaintWeather 側で捨てられる）
            stamp.worldX = cameraPos.x + (mouse.x - mapCenter.x) / pxPerM;
            stamp.worldZ = cameraPos.z - (mouse.y - mapCenter.y) / pxPerM;
            stamp.radiusM = brushRadiusM_;
            // 押しっぱなしで徐々に濃くなるエアブラシ挙動（フレームレート非依存）
            stamp.strength = std::min(brushStrength_ * ImGui::GetIO().DeltaTime * 6.0f, 1.0f);
            stamp.erase = (paintTool_ == 2);
            stamp.coverage = (paintTool_ == 0) ? 1.0f : 0.0f;
            stamp.cloudType = typeTarget_;
            stamp.cloudTop = topTarget_;
            manager.PaintWeather(stamp);
            paintingStroke_ = true;
        }
        // ストローク終了（マウスリリース）で自動保存する。
        // CPU 側の書き込みが止まった後にもう一度転送されるので、転送の破れが残らない
        if (paintingStroke_ && !active) {
            manager.SaveWeatherPaint();
            paintingStroke_ = false;
        }

        // ---- 情報と全消去 ----
        ImGui::TextDisabled("カメラ（X=%.1fkm, Z=%.1fkm）が常に中心・黄枠 = ペイント領域",
            cameraPos.x / 1000.0f, cameraPos.z / 1000.0f);
        ImGui::TextDisabled("背景 = 手続き生成の雲量（%.0f km でタイル）・重ねた明るい部分 = ペイント",
            tileM / 1000.0f);

        if (manager.HasWeatherPaint()) {
            ImGui::Text("ペイントあり（ストローク終了時に自動保存）");
            ImGui::SameLine();
            if (ImGui::SmallButton("全消去")) {
                ImGui::OpenPopup("##clearWeatherPaint");
            }
            if (ImGui::BeginPopup("##clearWeatherPaint")) {
                ImGui::TextUnformatted("ペイントを全消去して手続き生成のみへ戻しますか？");
                if (ImGui::Button("消去する")) {
                    manager.ClearWeatherPaint();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("キャンセル")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::TextDisabled("ペイントなし（手続き生成のみ）");
        }
#endif
    }

    void VolumetricCloudEditor::DrawAdvancedSection()
    {
#ifdef USE_IMGUI
        DrawCVarGroup("形状（ノイズ・雲層・風）", kShapeGroup, std::size(kShapeGroup));
        DrawCVarGroup("ライティング", kLightingGroup, std::size(kLightingGroup));
        DrawCVarGroup("品質・負荷（マーチング）", kQualityGroup, std::size(kQualityGroup));
        DrawCVarGroup("巻雲シェル", kCirrusGroup, std::size(kCirrusGroup));
        DrawCVarGroup("ゴッドレイ・雲影", kGodRayGroup, std::size(kGodRayGroup));

        // グループに載っていない CVar の受け皿（追加漏れでも UI から消えないようにする）
        const auto& covered = CoveredCVarNames();
        std::vector<ICVar*> uncovered;
        for (ICVar* cvar : CVarRegistry::Get().GetByPrefix(kCloudCVarPrefix)) {
            if (HasFlag(cvar->GetFlags(), CVarFlags::NoUI)) {
                continue;
            }
            if (covered.find(cvar->GetName()) == covered.end()) {
                uncovered.push_back(cvar);
            }
        }
        if (!uncovered.empty()) {
            if (ImGui::CollapsingHeader("その他（未分類）")) {
                for (ICVar* cvar : uncovered) {
                    CVarUI::DrawWidget(cvar);
                }
            }
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

        bool enabled = cloudManager->IsEnabled();
        if (ImGui::Checkbox("雲を有効にする", &enabled)) {
            cloudManager->SetEnabled(enabled);
        }

        // ===== ① 天候（プリセット + メタスライダー） =====
        ImGui::SeparatorText("天候");
        DrawPresetSelector(*cloudManager);
        ImGui::Spacing();
        DrawWeatherSliders();
        ImGui::Spacing();

        // ===== ② 配置ペイント =====
        if (ImGui::CollapsingHeader("配置ペイント（ウェザーマップ）", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawPaintSection(*cloudManager);
        }

        // ===== ③ 詳細設定 =====
        ImGui::SeparatorText("詳細設定");
        DrawAdvancedSection();

        // ===== 診断情報 =====
        if (ImGui::CollapsingHeader("診断")) {
            ImGui::Text("雲アクティブ（このフレーム）: %s", cloudManager->AreCloudsActive() ? "true" : "false");
            ImGui::Text("ノイズテクスチャ生成済み: %s", cloudManager->AreNoiseTexturesReady() ? "true" : "false");
            ImGui::Text("配置ペイント: %s", cloudManager->HasWeatherPaint() ? "あり" : "なし");
        }

        ImGui::Spacing();
        if (ImGui::Button("パラメータを既定値にリセット")) {
            CVarUI::ResetTree(kCloudCVarPrefix);
        }
        HelpMarker("CVar を全て既定値へ戻します。配置ペイントは対象外です\n"
            "（ペイントは配置ペイント内の「全消去」から）。");
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
