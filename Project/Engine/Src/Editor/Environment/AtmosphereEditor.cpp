#include "pch.h"
#include "Editor/Environment/AtmosphereEditor.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/EditorTheme.h"
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/Widgets/EditorBars.h"
#include "Editor/Inspector/ComponentInspectors.h"
#include "Editor/Inspector/InspectorLayout.h"
#include "EngineSystem/EngineSystem.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Scene/Feature/TimeOfDayFeature.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/CVar/CVarUndoStack.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <optional>

namespace CoreEngine
{
    namespace
    {
        namespace Theme = Editor::Theme;

        CVar<int> cvSkyMapView{
            "d.Atmosphere.SkyMapView", 1,
            "空と大気散乱のインスペクタの空の図（0 = 真上から・1 = 展開図）",
            CVarRange{ 0.0f, 1.0f }, CVarFlags::NoUI };

        /// この設定を持つコンポーネントの型名
        constexpr const char* kComponentTypeName = "SkyBox";

        /// 行のラベルと欄の右クリックで開くメニューの ID
        constexpr const char* kRowMenuId = "##lightRowMenu";

        /// 太陽と月の値の既定（新しく置いたライトと同じ）
        const SunSettings kSunDefaults{};
        const MoonSettings kMoonDefaults{};

        /// 散乱係数を見やすい桁で出す倍率（×10⁻⁶ /m の単位）
        constexpr float kPerMegaMeter = 1.0e6f;

        /// 朔望月 [日]
        constexpr float kMoonMonthDays = TimeOfDayFeature::kSynodicMonthDays;

        /// m を km で出す倍率
        constexpr float kMeterToKm = 0.001f;

        /// RGB の成分の文字
        constexpr const char* kRgbLabels[] = { "R", "G", "B" };

        /// 「大気」の節に並べる CVar（既定に戻すの対象）
        constexpr const char* kAtmosphereCVars[] = {
            "r.Atmosphere.PlanetRadius",
            "r.Atmosphere.TopRadius",
            "r.Atmosphere.RayleighScattering",
            "r.Atmosphere.RayleighScaleHeight",
            "r.Atmosphere.MieScattering",
            "r.Atmosphere.MieAbsorption",
            "r.Atmosphere.MieScaleHeight",
            "r.Atmosphere.MiePhaseG",
            "r.Atmosphere.OzoneAbsorption",
            "r.Atmosphere.OzoneLayerCenter",
            "r.Atmosphere.OzoneLayerHalfWidth",
            "r.Atmosphere.GroundAlbedo",
            "r.Atmosphere.MultiScatteringFactor",
            "r.Atmosphere.SunDiskAngularRadius",
            "r.Atmosphere.SunDiskLuminanceScale",
            "r.Atmosphere.MoonDiskAngularRadius",
            "r.Atmosphere.MoonDiskLuminanceScale",
            "r.Atmosphere.StarIntensity",
            "r.Atmosphere.AerialPerspectiveKmPerSlice",
        };

        ICVar* FindCVar(const char* name)
        {
            return CVarRegistry::Get().Find(name);
        }

        /// @brief 値がほぼ同じか（大きい方の値の 0.1% 以内）
        bool NearlyEqual(float lhs, float rhs)
        {
            return std::fabs(lhs - rhs) <= 0.001f * (std::max)({ 1.0e-6f, std::fabs(lhs), std::fabs(rhs) });
        }

        bool NearlyEqual(const Vector3& lhs, const Vector3& rhs)
        {
            return NearlyEqual(lhs.x, rhs.x) && NearlyEqual(lhs.y, rhs.y) && NearlyEqual(lhs.z, rhs.z);
        }

        /// @brief 方位を 0〜360 で出す
        float AzimuthForDisplay(float azimuthDeg)
        {
            float a = std::fmod(azimuthDeg, 360.0f);
            if (a < 0.0f) {
                a += 360.0f;
            }
            return a >= 359.95f ? 0.0f : a;
        }

        /// @brief 右クリックのメニュー（既定値へ戻す）
        /// @return 既定値へ戻すを選んだら true
        bool ResetMenu(bool modified)
        {
            bool reset = false;
            ImGui::OpenPopupOnItemClick(kRowMenuId, ImGuiPopupFlags_MouseButtonRight);
            if (ImGui::BeginPopup(kRowMenuId)) {
                if (ImGui::MenuItem("既定値へ戻す", nullptr, false, modified)) {
                    reset = true;
                }
                ImGui::EndPopup();
            }
            return reset;
        }

        /// @brief 小数の欄の作り
        struct FloatField
        {
            float min = 0.0f;
            float max = 1.0f;
            const char* format = "%.3f";
            bool logarithmic = false;
            std::optional<float> defaultValue; ///< 既定値（無ければ橙にせず、戻すメニューも出さない）
        };

        /// @brief ライトの小数の行（掴んだ・離したを履歴へ渡し、変えたら apply を呼ぶ）
        void LightFloatRow(SunMoonController& controller, const char* id, const char* label, float value,
            const FloatField& field, const std::function<void(float)>& apply)
        {
            ImGui::PushID(id);
            const bool modified = field.defaultValue && !NearlyEqual(value, *field.defaultValue);
            InspectorLayout::BeginRow(label, modified ? Theme::kWarm : Theme::kTextDim,
                field.defaultValue ? kRowMenuId : nullptr);

            ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
            if (field.logarithmic) {
                flags |= ImGuiSliderFlags_Logarithmic;
            }
            const float speed = (field.max - field.min) / 400.0f;
            const auto drawField = [&] {
                return ImGui::DragFloat("##value", &value, speed, field.min, field.max, field.format, flags);
            };
            const bool edited = field.logarithmic
                ? drawField()
                : InspectorLayout::FilledField((value - field.min) / (field.max - field.min), drawField);
            controller.TrackItem();
            if (edited) {
                apply(value);
            }
            if (field.defaultValue && ResetMenu(modified)) {
                controller.BeginEdit();
                apply(*field.defaultValue);
                controller.EndEdit();
            }
            ImGui::PopID();
        }

        /// @brief ライトの方位の行（0〜360 で出し、端を越えたら回り込む）
        void LightAzimuthRow(SunMoonController& controller, const char* id, float azimuthDeg,
            const std::function<void(float)>& apply)
        {
            ImGui::PushID(id);
            InspectorLayout::BeginRow("方位", Theme::kTextDim);
            float shown = AzimuthForDisplay(azimuthDeg);
            char format[48];
            std::snprintf(format, sizeof(format), "%%.1f°（%s）", CompassName(azimuthDeg));
            const bool edited = InspectorLayout::FilledField(shown / 360.0f, [&] {
                return ImGui::DragFloat("##value", &shown, 0.5f, 0.0f, 0.0f, format);
            });
            controller.TrackItem();
            if (edited) {
                apply(WrapAzimuth(shown));
            }
            ImGui::PopID();
        }

        /// @brief ライトの色の行
        void LightColorRow(SunMoonController& controller, const char* id, const Vector3& color, const Vector3& defaultColor,
            const std::function<void(const Vector3&)>& apply)
        {
            ImGui::PushID(id);
            const bool modified = !NearlyEqual(color, defaultColor);
            InspectorLayout::BeginRow("色", modified ? Theme::kWarm : Theme::kTextDim, kRowMenuId);
            float rgba[4] = { color.x, color.y, color.z, 1.0f };
            const bool edited = InspectorLayout::ColorSwatch("##value", rgba, false);
            controller.TrackItem();
            if (edited) {
                apply(Vector3{ rgba[0], rgba[1], rgba[2] });
            }
            if (ResetMenu(modified)) {
                controller.BeginEdit();
                apply(defaultColor);
                controller.EndEdit();
            }
            ImGui::PopID();
        }

        /// @brief 編集できない値の行（左にラベル、右に淡い文字）
        void InfoRow(const char* label, const char* format, ...)
        {
            char text[160];
            va_list args;
            va_start(args, format);
            std::vsnprintf(text, sizeof(text), format, args);
            va_end(args);
            InspectorLayout::BeginRow(label, Theme::kTextDim);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", text);
        }

        /// @brief 右へ寄せたボタンの並び
        struct ButtonSpec
        {
            const char* label = "";
            const char* tooltip = nullptr;
            bool enabled = true;
            std::function<void()> onPress;
            bool on = false; ///< 選んでいる状態として描くか
        };

        void RightButtons(std::initializer_list<ButtonSpec> buttons)
        {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            float width = 0.0f;
            for (const ButtonSpec& button : buttons) {
                width += UI::Bar::ButtonWidth(button.label) + (width > 0.0f ? spacing : 0.0f);
            }
            InspectorLayout::AlignToRight(width);
            bool first = true;
            for (const ButtonSpec& button : buttons) {
                if (!first) {
                    ImGui::SameLine();
                }
                first = false;
                if (UI::Bar::Button(button.label, button.on, button.tooltip, button.enabled) && button.onPress) {
                    button.onPress();
                }
            }
        }

        /// @brief 時刻を「12:30（12.50 h）」の書式にする
        void TimeFormat(char* out, size_t size, float hours)
        {
            const int minutes = static_cast<int>(std::floor(hours * 60.0f + 0.5f)) % (24 * 60);
            std::snprintf(out, size, "%02d:%02d（%%.2f h）", minutes / 60, minutes % 60);
        }
    }

    void AtmosphereEditor::Initialize(EngineSystem& engine)
    {
        engine_ = &engine;
        controller_.Initialize(engine);
        sceneGizmo_.Initialize(controller_);
        inspector_ = Editor::ComponentInspectors::Register(kComponentTypeName, {
            .displayName = "空と大気散乱",
            .drawBody = [this](IComponent&) { DrawContent(); return false; },
            });
    }

    void AtmosphereEditor::DrawContent()
    {
        controller_.SyncFromLights();
        controller_.Tick(ImGui::GetIO().DeltaTime);

        ImGui::PushID("Atmosphere");
        DrawPlacementSection();
        DrawTimeSection();
        DrawAmbientSection();
        DrawAtmosphereSection();
        DrawDiagnosticsSection();
        ImGui::PopID();

        // 太陽と月を変えた操作を離したら、1 件だけ履歴へ積む
        controller_.CommitTrackedEdit();
    }

    // ==================== 太陽と月 ====================

    void AtmosphereEditor::DrawPlacementSection()
    {
        if (!InspectorLayout::Foldout("太陽と月", true)) {
            return;
        }
        ImGui::PushID("Placement");

        // 図の切り替えと Game ビューの印
        const int view = cvSkyMapView.Get();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Theme::kTextDim, "空の図");
        ImGui::SameLine();
        RightButtons({
            { "真上から", "真上から見た空（中心が天頂・円が地平線・上が北）", true,
              [] { cvSkyMapView.Set(0); }, view == 0 },
            { "展開図", "地平線をぐるりと広げた空（横が方位・縦が高度・中央が南）", true,
              [] { cvSkyMapView.Set(1); }, view != 0 },
            { "Game ビューに出す",
              "空の物か太陽・月のライトを選んでいる間、Game ビューに太陽と月と軌道を描く（印をドラッグで動かせる）", true,
              [] { SunMoonSceneGizmo::SetEnabled(!SunMoonSceneGizmo::IsEnabled()); }, SunMoonSceneGizmo::IsEnabled() },
        });

        ImGui::Spacing();
        if (!controller_.HasSun()) {
            InspectorLayout::HelpBox(
                "このシーンには大気の太陽がありません。ディレクショナルライトを置いて「大気の太陽」を入れると、空が明るくなります。",
                InspectorLayout::Message::Warning);
        }

        skyMap_.Draw(controller_, view == 0 ? SkyMapWidget::View::Dome : SkyMapWidget::View::Panorama);
        InspectorLayout::Hint("左ドラッグで太陽、右ドラッグで月を動かす（月の印は左でも掴める）。Ctrl を押すと 5° ごとに止まる。"
            "水色の枠は Game ビューのカメラが見ている範囲。");

        // まとめて置く
        ImGui::PushID("Presets");
        InspectorLayout::BeginRow("プリセット", Theme::kTextDim);
        const auto preset = [this](const char* label, const char* tooltip, const std::function<void()>& apply) {
            if (UI::Bar::Button(label, false, tooltip)) {
                controller_.BeginEdit();
                apply();
                controller_.EndEdit();
            }
        };
        preset("朝", "6:40 の太陽", [this] { controller_.ApplyDaytimePreset(6.7f); });
        ImGui::SameLine();
        preset("正午", "12:00 の太陽", [this] { controller_.ApplyDaytimePreset(12.0f); });
        ImGui::SameLine();
        preset("夕暮れ", "17:36 の太陽", [this] { controller_.ApplyDaytimePreset(17.6f); });
        ImGui::SameLine();
        preset("夜（満月）", "0:00 にして満月を出し、自動露出を入れる", [this] { controller_.ApplyNightPreset(); });
        ImGui::PopID();

        if (controller_.IsTimeDriven()) {
            InspectorLayout::HelpBox(
                "このシーンには時刻の機能（TimeOfDay）があり、太陽と月を時刻で動かしています。"
                "太陽を動かすと時刻が、月を動かすと月齢が変わり、どちらも軌道の上のいちばん近い所へ行きます。");
        }

        ImGui::SeparatorText("太陽");
        DrawSunRows();

        ImGui::SeparatorText("月");
        DrawMoonRows();

        ImGui::PopID();
    }

    void AtmosphereEditor::DrawSunRows()
    {
        ImGui::PushID("Sun");
        if (!controller_.HasSun()) {
            InspectorLayout::Hint("大気の太陽がありません。");
            ImGui::PopID();
            return;
        }

        const SunSettings sun = controller_.GetSun();
        const bool timeDriven = controller_.IsTimeDriven();

        ImGui::BeginDisabled(timeDriven);
        LightFloatRow(controller_, "Elevation", "高度", sun.angles.elevationDeg,
            { SunMoonController::kMinElevationDeg, 90.0f, "%.1f°" },
            [this](float v) { controller_.MoveTo(SkyBody::Sun, SkyAngles{ v, controller_.GetSun().angles.azimuthDeg }); });
        LightAzimuthRow(controller_, "Azimuth", sun.angles.azimuthDeg,
            [this](float v) { controller_.MoveTo(SkyBody::Sun, SkyAngles{ controller_.GetSun().angles.elevationDeg, v }); });
        ImGui::EndDisabled();

        LightFloatRow(controller_, "Sky", "空の明るさ", sun.skyIntensity,
            { 0.0f, 100.0f, "%.1f", false, kSunDefaults.skyIntensity },
            [this](float v) { SunSettings s = controller_.GetSun(); s.skyIntensity = v; controller_.ApplySun(s); });
        LightFloatRow(controller_, "Lux", "照度", sun.illuminanceLux,
            { 0.0f, 150000.0f, "%.0f lx", true, kSunDefaults.illuminanceLux },
            [this](float v) { SunSettings s = controller_.GetSun(); s.illuminanceLux = v; controller_.ApplySun(s); });
        LightColorRow(controller_, "Color", sun.color, kSunDefaults.color,
            [this](const Vector3& v) { SunSettings s = controller_.GetSun(); s.color = v; controller_.ApplySun(s); });
        CVarUI::DrawRow(FindCVar("r.Atmosphere.TransmittanceOnLight"), "夕日で光を赤くする");

        RightButtons({
            { "カメラの正面へ", "Game ビューのカメラが向いている方向へ置く",
              controller_.GetViewCamera() != nullptr && controller_.CanMove(SkyBody::Sun),
              [this] { controller_.BeginEdit(); controller_.PlaceAtView(SkyBody::Sun); controller_.EndEdit(); } },
            { "ライトを選ぶ", "太陽のライトを持つオブジェクトを選ぶ", true,
              [this] { controller_.SelectLightObject(SkyBody::Sun); } },
        });
        ImGui::PopID();
    }

    void AtmosphereEditor::DrawMoonRows()
    {
        ImGui::PushID("Moon");
        const MoonSettings moon = controller_.GetMoon();
        const bool timeDriven = controller_.IsTimeDriven();

        if (timeDriven) {
            // 時刻の機能があるときは、その機能の月の設定を並べる
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.Moon"), "月を出す");
            if (controller_.IsShown(SkyBody::Moon)) {
                InfoRow("高度", "%.1f°%s", moon.angles.elevationDeg, moon.angles.elevationDeg > 0.0f ? "" : "（地平線の下なので光らない）");
                InfoRow("方位", "%.1f°（%s）", AzimuthForDisplay(moon.angles.azimuthDeg), CompassName(moon.angles.azimuthDeg));

                ImGui::PushID("Age");
                InspectorLayout::BeginRow("月齢", Theme::kTextDim);
                float age = controller_.GetMoonAge();
                const bool edited = InspectorLayout::FilledField(age / kMoonMonthDays, [&] {
                    return ImGui::DragFloat("##value", &age, 0.05f, 0.0f, kMoonMonthDays, "%.1f 日",
                        ImGuiSliderFlags_AlwaysClamp);
                });
                if (edited) {
                    controller_.SetMoonAge(age);
                }
                ImGui::PopID();

                ImGui::PushID("Phases");
                InspectorLayout::BeginRow("満ち欠け", Theme::kTextDim);
                static constexpr struct { const char* label; float fraction; } kPhases[] = {
                    { "新月", 0.0f }, { "上弦", 0.25f }, { "満月", 0.5f }, { "下弦", 0.75f },
                };
                bool first = true;
                for (const auto& phase : kPhases) {
                    if (!first) {
                        ImGui::SameLine();
                    }
                    first = false;
                    if (UI::Bar::Button(phase.label, false)) {
                        controller_.SetMoonAge(kMoonMonthDays * phase.fraction);
                    }
                }
                ImGui::PopID();
            }
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.StartMoonAge"), "始まりの月齢", "%.1f 日");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.AdvanceMoonAge"), "月齢を日ごとに進める");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.MoonSkyRatio"), "空の明るさ（太陽の何倍）", "%.4f");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.MoonSurfaceIntensity"), "照度", "%.1f lx");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.MoonColor"), "色", "%.2f", kRgbLabels);
            RightButtons({
                { "満月にする", "月齢を満月にして、太陽のちょうど反対へ置く", controller_.IsShown(SkyBody::Moon),
                  [this] { controller_.PlaceMoonOppositeSun(); } },
                { "カメラの正面へ", "Game ビューのカメラが向いている方向にいちばん近い月齢にする",
                  controller_.GetViewCamera() != nullptr && controller_.CanMove(SkyBody::Moon),
                  [this] { controller_.PlaceAtView(SkyBody::Moon); } },
                { "ライトを選ぶ", "月のライトを持つオブジェクトを選ぶ", controller_.FindLight(SkyBody::Moon) != nullptr,
                  [this] { controller_.SelectLightObject(SkyBody::Moon); } },
            });
            InspectorLayout::Hint("時刻の機能の月は、太陽と同じ軌道を月齢の分だけ遅れて通る。月を動かすと月齢が変わる（今の月齢は保存しない）。");
            ImGui::PopID();
            return;
        }

        ImGui::PushID("Enabled");
        InspectorLayout::BeginRow("月を出す", Theme::kTextDim);
        bool enabled = moon.enabled;
        if (ImGui::Checkbox("##value", &enabled)) {
            controller_.BeginEdit();
            MoonSettings settings = moon;
            settings.enabled = enabled;
            controller_.ApplyMoon(settings);
            controller_.EndEdit();
        }
        ImGui::PopID();

        if (moon.enabled) {
            LightFloatRow(controller_, "Elevation", "高度", moon.angles.elevationDeg,
                { SunMoonController::kMinElevationDeg, 90.0f, "%.1f°" },
                [this](float v) { controller_.MoveTo(SkyBody::Moon, SkyAngles{ v, controller_.GetMoon().angles.azimuthDeg }); });
            LightAzimuthRow(controller_, "Azimuth", moon.angles.azimuthDeg,
                [this](float v) { controller_.MoveTo(SkyBody::Moon, SkyAngles{ controller_.GetMoon().angles.elevationDeg, v }); });
            LightFloatRow(controller_, "Sky", "空の明るさ", moon.skyIntensity,
                { 0.0f, 1.0f, "%.4f", true, kMoonDefaults.skyIntensity },
                [this](float v) { MoonSettings s = controller_.GetMoon(); s.skyIntensity = v; controller_.ApplyMoon(s); });
            LightFloatRow(controller_, "Lux", "照度", moon.illuminanceLux,
                { 0.0f, 2000.0f, "%.1f lx", true, kMoonDefaults.illuminanceLux },
                [this](float v) { MoonSettings s = controller_.GetMoon(); s.illuminanceLux = v; controller_.ApplyMoon(s); });
            LightColorRow(controller_, "Color", moon.color, kMoonDefaults.color,
                [this](const Vector3& v) { MoonSettings s = controller_.GetMoon(); s.color = v; controller_.ApplyMoon(s); });
        }
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MoonPhaseFromSun"), "満ち欠けを太陽の位置から決める");

        if (moon.enabled) {
            RightButtons({
                { "太陽の反対へ", "太陽のちょうど反対（満月の位置）へ置く", true,
                  [this] { controller_.BeginEdit(); controller_.PlaceMoonOppositeSun(); controller_.EndEdit(); } },
                { "カメラの正面へ", "Game ビューのカメラが向いている方向へ置く", controller_.GetViewCamera() != nullptr,
                  [this] { controller_.BeginEdit(); controller_.PlaceAtView(SkyBody::Moon); controller_.EndEdit(); } },
                { "ライトを選ぶ", "月のライトを持つオブジェクトを選ぶ", controller_.FindLight(SkyBody::Moon) != nullptr,
                  [this] { controller_.SelectLightObject(SkyBody::Moon); } },
            });
        }
        ImGui::PopID();
    }

    // ==================== 時刻 ====================

    void AtmosphereEditor::DrawTimeSection()
    {
        if (!InspectorLayout::Foldout("時刻から置く", true)) {
            return;
        }
        ImGui::PushID("Time");

        const bool timeDriven = controller_.IsTimeDriven();

        // 時刻
        {
            ImGui::PushID("Hour");
            const bool onPath = timeDriven || controller_.IsSunOnTimePath();
            InspectorLayout::BeginRow("時刻", onPath ? Theme::kTextDim : Theme::kTextMute);
            float hours = controller_.GetTimeOfDay();
            char format[48];
            TimeFormat(format, sizeof(format), hours);
            const bool edited = InspectorLayout::FilledField(hours / 24.0f, [&] {
                return ImGui::DragFloat("##value", &hours, 0.02f, 0.0f, 24.0f, format, ImGuiSliderFlags_AlwaysClamp);
            });
            controller_.TrackItem();
            if (edited) {
                controller_.SetTimeOfDay(hours);
            }
            ImGui::PopID();
        }

        if (timeDriven) {
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.Advance"), "時刻を進める");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.DayLengthSeconds"), "1 日の長さ", "%.0f s");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.StartHour"), "始まりの時刻", "%.2f h");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.LatitudeDeg"), "緯度", "%.1f°");
            CVarUI::DrawRow(FindCVar("r.TimeOfDay.SunAzimuthOffsetDeg"), "軌道の向き", "%.1f°");
            InspectorLayout::Hint("時刻の機能の設定はシーンに保存する。今の時刻は保存せず、再生を始めると「始まりの時刻」から進む。");
            ImGui::PopID();
            return;
        }

        LightFloatRow(controller_, "Latitude", "緯度", controller_.GetLatitudeDeg(),
            { -89.0f, 89.0f, "%.1f°" }, [this](float v) { controller_.SetLatitudeDeg(v); });
        LightFloatRow(controller_, "Orbit", "軌道の向き", controller_.GetOrbitOffsetDeg(),
            { -180.0f, 180.0f, "%.1f°" }, [this](float v) { controller_.SetOrbitOffsetDeg(v); });

        ImGui::PushID("Advance");
        InspectorLayout::BeginRow("時刻を進める", Theme::kTextDim);
        bool advancing = controller_.IsAutoAdvancing();
        if (ImGui::Checkbox("##value", &advancing)) {
            controller_.SetAutoAdvancing(advancing);
        }
        ImGui::PopID();

        if (controller_.IsAutoAdvancing()) {
            ImGui::PushID("Speed");
            InspectorLayout::BeginRow("進める速さ", Theme::kTextDim);
            float speed = controller_.GetAdvanceSpeed();
            if (ImGui::DragFloat("##value", &speed, 0.03f, 0.01f, 12.0f, "%.2f h/s",
                    ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
                controller_.SetAdvanceSpeed(speed);
            }
            ImGui::PopID();
        }

        if (!controller_.IsSunOnTimePath()) {
            InspectorLayout::Hint("太陽を手で動かしたので、時刻の位置から外れている。時刻・緯度・軌道の向きを動かすと軌道の上へ戻る。");
        }
        InspectorLayout::Hint("時刻・緯度・軌道の向きはエディタの中だけの値で、シーンには太陽の向きが残る。"
            "シーンで時刻を動かすには、時刻の機能（TimeOfDay）を入れる。");
        ImGui::PopID();
    }

    // ==================== 環境光 ====================

    void AtmosphereEditor::DrawAmbientSection()
    {
        if (!InspectorLayout::Foldout("環境光", true)) {
            return;
        }
        ImGui::PushID("Ambient");
        const AtmosphereManager* const atmosphere = controller_.GetAtmosphereManager();

        ICVar* const ambient = FindCVar("r.Atmosphere.SkyAmbientEnabled");
        CVarUI::DrawRow(ambient, "空の環境光");
        const bool ambientOn = ambient && ambient->AsBool() && *ambient->AsBool();
        if (ambientOn) {
            CVarUI::DrawRow(FindCVar("r.Atmosphere.SkyAmbientScale"), "環境光の強さ", "%.3f");
            if (atmosphere) {
                InfoRow("環境光の用意", "%s", atmosphere->IsSkyAmbientReady() ? "できている" : "作っている");
            }

            ICVar* const specular = FindCVar("r.Atmosphere.SkySpecularEnabled");
            CVarUI::DrawRow(specular, "空と雲の映り込み");
            if (atmosphere && specular && specular->AsBool() && *specular->AsBool()) {
                InfoRow("映り込みの用意", "%s", atmosphere->IsSkyEnvironmentReady() ? "できている" : "作っている");
            }
        }
        InspectorLayout::Hint("空の色から物を照らす。昼は青く、夕方は橙に、夜はほとんど暗くなる。切ると、ライトの色の簡単な環境光になる。");
        ImGui::PopID();
    }

    // ==================== 大気 ====================

    void AtmosphereEditor::DrawAtmosphereSection()
    {
        if (!InspectorLayout::Foldout("大気", false)) {
            return;
        }
        ImGui::PushID("Parameters");

        ImGui::SeparatorText("惑星");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.PlanetRadius"), "地表の半径", "%.0f km", nullptr, kMeterToKm);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.TopRadius"), "大気の上端の半径", "%.0f km", nullptr, kMeterToKm);
        if (AtmosphereManager* const atmosphere = controller_.GetAtmosphereManager()) {
            // 地表の高さはシーンの大気の値へ直接書く
            ImGui::PushID("GroundLevel");
            InspectorLayout::BeginRow("地表の高さ（シーン）", Theme::kTextDim);
            AtmosphereParameters& parameters = atmosphere->GetParametersMutable();
            if (ImGui::DragFloat("##value", &parameters.groundLevelY, 0.5f, -1000.0f, 1000.0f, "%.1f m",
                    ImGuiSliderFlags_AlwaysClamp)) {
                atmosphere->MarkLUTDirty();
            }
            ImGui::PopID();
        }

        ImGui::SeparatorText("レイリー散乱（空の青さ）");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.RayleighScattering"), "散乱係数", "%.2f", kRgbLabels, kPerMegaMeter);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.RayleighScaleHeight"), "薄くなる高さ", "%.1f km", nullptr, kMeterToKm);

        ImGui::SeparatorText("ミー散乱（かすみ）");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MieScattering"), "散乱係数", "%.2f", nullptr, kPerMegaMeter);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MieAbsorption"), "吸収係数", "%.2f", nullptr, kPerMegaMeter);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MieScaleHeight"), "薄くなる高さ", "%.2f km", nullptr, kMeterToKm);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MiePhaseG"), "前へ散る強さ", "%.2f");

        ImGui::SeparatorText("オゾン（薄明の青紫）");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.OzoneAbsorption"), "吸収係数", "%.3f", kRgbLabels, kPerMegaMeter);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.OzoneLayerCenter"), "層の中心の高さ", "%.1f km", nullptr, kMeterToKm);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.OzoneLayerHalfWidth"), "層の厚さの半分", "%.1f km", nullptr, kMeterToKm);
        InspectorLayout::Hint("散乱係数と吸収係数の単位は ×10⁻⁶ /m。");

        ImGui::SeparatorText("地面と多重散乱");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.GroundAlbedo"), "地面の反射率", "%.2f", kRgbLabels);
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MultiScatteringFactor"), "多重散乱の強さ", "%.2f");

        ImGui::SeparatorText("太陽と月の円盤");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.SunDiskAngularRadius"), "太陽の見かけの半径", "%.3f°");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.SunDiskLuminanceScale"), "太陽の円盤の明るさ", "%.1f");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MoonDiskAngularRadius"), "月の見かけの半径", "%.3f°");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.MoonDiskLuminanceScale"), "月の円盤の明るさ", "%.1f");

        ImGui::SeparatorText("星と遠くのかすみ");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.StarIntensity"), "星の明るさ", "%.2f");
        CVarUI::DrawRow(FindCVar("r.Atmosphere.AerialPerspectiveKmPerSlice"), "かすみの刻み", "%.1f km");

        ImGui::Spacing();
        RightButtons({
            { "既定に戻す", "この節の値をすべて既定値へ戻す（Ctrl+Z で 1 回で戻せる）", true, [] {
                auto& undoStack = CVarUndoStack::Get();
                undoStack.BeginBatch();
                for (const char* name : kAtmosphereCVars) {
                    if (ICVar* const cvar = FindCVar(name)) {
                        undoStack.BeginEdit(cvar);
                        cvar->ResetToDefault();
                        undoStack.CommitEdit(cvar);
                    }
                }
                undoStack.EndBatch();
                CVarRegistry::Get().NotifyCommit();
            } },
        });
        ImGui::PopID();
    }

    // ==================== 診断 ====================

    void AtmosphereEditor::DrawDiagnosticsSection()
    {
        if (!InspectorLayout::Foldout("診断", false)) {
            return;
        }
        ImGui::PushID("Diagnostics");
        const AtmosphereManager* const atmosphere = controller_.GetAtmosphereManager();
        if (!atmosphere) {
            InspectorLayout::HelpBox("大気散乱の管理が見つかりません。", InspectorLayout::Message::Error);
            ImGui::PopID();
            return;
        }

        ImGui::SeparatorText("太陽");
        if (atmosphere->HasSunLight()) {
            const Vector3& direction = atmosphere->GetSunDirection();
            const Vector3& transmittance = atmosphere->GetSunTransmittance();
            InfoRow("光の向き", "(%.3f, %.3f, %.3f)", direction.x, direction.y, direction.z);
            InfoRow("空の明るさ", "%.2f", atmosphere->GetSunIntensity());
            InfoRow("地表に届く割合", "(%.3f, %.3f, %.3f)", transmittance.x, transmittance.y, transmittance.z);
        } else {
            InfoRow("太陽", "なし");
        }

        ImGui::SeparatorText("月");
        if (atmosphere->HasMoonLight()) {
            const Vector3& direction = atmosphere->GetMoonDirection();
            const Vector3& transmittance = atmosphere->GetMoonTransmittance();
            InfoRow("光の向き", "(%.3f, %.3f, %.3f)", direction.x, direction.y, direction.z);
            InfoRow("空の明るさ", "%.4f", atmosphere->GetMoonIntensity());
            InfoRow("満ち欠けの明るさ", "%.1f%%%s", atmosphere->GetMoonPhaseBrightness() * 100.0f,
                atmosphere->IsMoonPhaseFromSun() ? "" : "（常に満月）");
            InfoRow("地表に届く割合", "(%.3f, %.3f, %.3f)", transmittance.x, transmittance.y, transmittance.z);
        } else {
            InfoRow("月", "なし");
        }

        ImGui::SeparatorText("カメラ");
        InfoRow("地表からの高さ", "%.2f m", atmosphere->GetCameraHeightAboveGround());
        InfoRow("惑星の中心から", "%.1f km", atmosphere->GetDistanceFromPlanetCenter() * kMeterToKm);
        InfoRow("空の表", "%s", atmosphere->IsLUTDirty() ? "作り直しを待っている" : "最新");
        ImGui::PopID();
    }
}

#endif // CORE_EDITOR
