#include "pch.h"
#include "TimeOfDayFeature.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/FrameRate/Time.h"

#ifdef USE_IMGUI
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#include "Utility/Debug/GameDebugUI.h"
#include "Editor/ImGui/CVarPanel.h"
#include "Editor/ImGui/ImGuiAll.h"
#endif

#include <algorithm>
#include <cmath>

namespace
{
    using namespace CoreEngine;

    constexpr float kDegToRad = MathCore::Constants::kDegToRad;
    constexpr float kRadToDeg = MathCore::Constants::kRadToDeg;

    /// パネルが扱う CVar の接頭辞
    constexpr const char* kCVarPrefix = "r.TimeOfDay";

#ifdef USE_IMGUI
    /// 設定パネルの編集対象（GroundFeature と同じ流儀。ドロワーは何もキャプチャしない）
    TimeOfDayFeature* s_activeTimeOfDay = nullptr;
#endif

    // ───────────────────────────────────────────────────────────────
    // 昼夜サイクルのパラメータ（CVar。Engine Settings と Saved JSON に自動で載る）
    // ───────────────────────────────────────────────────────────────

    CVar<bool> cvAdvance{
        "r.TimeOfDay.Advance", true,
        "時刻を自動で進める（切ると SetTimeOfDay() やスライダーの値で止まる）" };

    CVar<float> cvDayLengthSeconds{
        "r.TimeOfDay.DayLengthSeconds", 240.0f,
        "ゲーム内の 24 時間を何秒で 1 周するか [s]",
        CVarRange{ 5.0f, 3600.0f } };

    CVar<float> cvStartHour{
        "r.TimeOfDay.StartHour", 12.0f,
        "シーン開始時の時刻 [h]（0=深夜 6=日の出 12=正午 18=日没）",
        CVarRange{ 0.0f, 24.0f } };

    CVar<float> cvLatitudeDeg{
        "r.TimeOfDay.LatitudeDeg", 35.0f,
        "観測地の緯度 [deg]。太陽の弧の傾きと昼の長さが変わる（既定は東京相当）",
        CVarRange{ -89.0f, 89.0f } };

    CVar<float> cvSunAzimuthOffsetDeg{
        "r.TimeOfDay.SunAzimuthOffsetDeg", 90.0f,
        "太陽・月の軌道を方位方向へ回すオフセット [deg]。"
        "既定 0 だと日の出が +X・日没が -X なので、+X 方向へ伸びるステージでは"
        "日没の太陽が画面端に居座って地面をオレンジに焼く。90 で日没がカメラの背後へ回る",
        CVarRange{ -180.0f, 180.0f } };

    CVar<float> cvTwilightDeg{
        "r.TimeOfDay.TwilightDeg", 6.0f,
        "GetNightFactor() が 1 になる太陽高度の深さ [deg]（6 = 市民薄明）",
        CVarRange{ 0.5f, 18.0f } };

    CVar<bool> cvMoon{
        "r.TimeOfDay.Moon", true,
        "夜に月（第2大気ライト）を出す。太陽の 12 時間ずれ＝満月の軌道を通る" };

    CVar<Vector3> cvMoonColor{
        "r.TimeOfDay.MoonColor", { 0.55f, 0.65f, 0.85f },
        "月光色（知覚的な青白さの美術値）" };

    CVar<float> cvMoonSurfaceIntensity{
        "r.TimeOfDay.MoonSurfaceIntensity", 80.0f,
        "月光のサーフェス直接光 [lx]（物理の満月 0.25 lx では見えないため美術値）。"
        "下げると月に照らされた面だけが暗くなる＝ポイントライトの灯りが相対的に立つ",
        CVarRange{ 0.0f, 1000.0f } };

    CVar<float> cvNightMaxAutoEV{
        "r.TimeOfDay.NightMaxAutoEV", 7.5f,
        "サイクル中の自動露出の上限 EV（r.AutoExposure.MaxEV を借りて上書きする）。"
        "夜はこの上限に張り付くので、下げた分だけ夜が暗くなる（昼は +2 EV 程度なので影響しない）",
        CVarRange{ 0.0f, 12.0f } };

    CVar<float> cvNightSkyAmbientMul{
        "r.TimeOfDay.NightSkyAmbientMul", 0.8f,
        "夜に空アンビエント（r.Atmosphere.SkyAmbientScale）へ掛ける倍率。"
        "1 で昼と同じ。下げると影の中の回り込みが減って夜のコントラストが上がる",
        CVarRange{ 0.0f, 1.0f } };

    CVar<float> cvMoonSkyRatio{
        "r.TimeOfDay.MoonSkyRatio", 0.0013f,
        "月の空（大気散乱）輝度スケールを、太陽の空スケールの何倍にするか。"
        "絶対値で置くとシーンごとの太陽スケールと釣り合わず夜空だけ浮く（既定 1/1000）",
        CVarRange{ 0.0f, 0.1f } };

    CVar<bool> cvAutoExposure{
        "r.TimeOfDay.AutoExposure", true,
        "サイクル中は自動露出を強制的に有効にする（夜が黒く潰れるのを防ぐ）" };

    /// @brief 時刻を [0, 24) へ巻き取る
    float NormalizeHours(float hours)
    {
        hours = std::fmod(hours, 24.0f);
        return (hours < 0.0f) ? hours + 24.0f : hours;
    }
}

namespace CoreEngine
{
    // ==================== ライフサイクル ====================

    void TimeOfDayFeature::Initialize([[maybe_unused]] SceneContext& ctx)
    {
        timeOfDay_ = NormalizeHours(cvStartHour.Get());

#ifdef USE_IMGUI
        // パラメータ UI は CVar から自動生成する。機能ごとにこの登録をしないと
        // どのパネルにも出てこない（全 CVar を一覧する横断パネルは無い設計）
        EnsureSettingsPanelRegistered(ctx.engine);
        SetActiveForSettingsPanel(this);
#endif
    }

    void TimeOfDayFeature::Update(SceneContext& ctx, SceneUpdatePhase phase)
    {
        if (phase != SceneUpdatePhase::FrameStart) {
            return;
        }

        // 前進はゲーム時間で行う。停止ボタン中は Time::DeltaTime() が 0 を返すため、
        // Feature 自体は回りながら時刻だけが止まり、スライダー編集の反映は残る
        if (cvAdvance.Get()) {
            const float dayLength = std::max(cvDayLengthSeconds.Get(), 1.0f);
            timeOfDay_ = NormalizeHours(timeOfDay_ + Time::DeltaTime() * (24.0f / dayLength));
        }

        ApplyToLights(ctx);
        ApplyAutoExposure(ctx);
        ApplyNightDarkness();
    }

    void TimeOfDayFeature::Finalize(SceneContext& ctx)
    {
#ifdef USE_IMGUI
        // シーンと一緒に消えるので、パネルの参照を先に外す
        SetActiveForSettingsPanel(nullptr);
#endif

        // 自動露出を元へ戻す（サイクルが触っていた場合のみ）
        if (autoExposureOverridden_) {
            if (ToneMapping* toneMapping = GetToneMapping(ctx)) {
                toneMapping->SetAutoExposureEnabled(autoExposureBefore_);
            }
            autoExposureOverridden_ = false;
        }

        // 借り物の CVar（露出上限・空アンビエント）を返す。
        // 返さないと、このサイクルの夜向けの値が次のシーンと保存 JSON へ焼き付く
        ReleaseBorrowedCVars();

        auto* lightManager = GetLightManager(ctx);
        if (!lightManager) {
            return;
        }

        if (savedSunValid_) {
            if (Light* sun = lightManager->GetAtmosphereSunLight()) {
                sun->direction = savedSunDirection_;
            }
            savedSunValid_ = false;
        }

        if (createdMoon_.IsValid()) {
            // 自分で足した月は片付ける（借り物のシーンに月を増やして返さない）
            lightManager->DestroyLight(createdMoon_);
            createdMoon_ = {};
        } else if (savedMoonValid_) {
            if (Light* moon = lightManager->GetAtmosphereMoonLight()) {
                moon->enabled = savedMoonEnabled_;
                moon->direction = savedMoonDirection_;
                moon->color = savedMoonColor_;
                moon->intensity = savedMoonIntensity_;
                moon->atmosphereIntensity = savedMoonSkyIntensity_;
            }
            savedMoonValid_ = false;
        }
    }

    // ==================== 時刻 ====================

    void TimeOfDayFeature::SetTimeOfDay(float hours)
    {
        timeOfDay_ = NormalizeHours(hours);
    }

    void TimeOfDayFeature::SetAdvancing(bool advancing)
    {
        cvAdvance.Set(advancing);
    }

    bool TimeOfDayFeature::IsAdvancing() const
    {
        return cvAdvance.Get();
    }

    void TimeOfDayFeature::SetDayLengthSeconds(float seconds)
    {
        cvDayLengthSeconds.Set(std::max(seconds, 1.0f));
    }

    float TimeOfDayFeature::GetDayLengthSeconds() const
    {
        return cvDayLengthSeconds.Get();
    }

    // ==================== 太陽軌道 ====================

    void TimeOfDayFeature::ComputeSunAngles(float hours, float latitudeDeg,
        float& outElevationDeg, float& outAzimuthDeg)
    {
        // 春分（赤緯 0）の太陽軌道: 時角 H = (時刻-12)×15°、緯度 φ。
        // sin(高度) = cosφ·cosH。方位は南基準 A = atan2(sinH, cosH·sinφ) を北（+Z）基準へ直す。
        // 正午に南（北半球）、6 時に東（+X）から昇り、18 時に西へ沈む弧になる
        // （AtmosphereEditor::ApplyTimeOfDay と同式。エディタと同じ空を再現するため）。
        const float H = (NormalizeHours(hours) - 12.0f) * 15.0f * kDegToRad;
        const float phi = std::clamp(latitudeDeg, -89.0f, 89.0f) * kDegToRad;

        const float sinElevation = std::cos(phi) * std::cos(H);
        outElevationDeg = std::asin(std::clamp(sinElevation, -1.0f, 1.0f)) * kRadToDeg;

        float azimuthDeg = 180.0f + std::atan2(std::sin(H), std::cos(H) * std::sin(phi)) * kRadToDeg;
        if (azimuthDeg > 180.0f) {
            azimuthDeg -= 360.0f;
        }
        outAzimuthDeg = azimuthDeg;
    }

    Vector3 TimeOfDayFeature::ComputeLightDirection(float elevationDeg, float azimuthDeg)
    {
        const float elevation = elevationDeg * kDegToRad;
        const float azimuth = azimuthDeg * kDegToRad;

        // 地表から光源を見る方向
        const Vector3 toLight = {
            std::cos(elevation) * std::sin(azimuth),
            std::sin(elevation),
            std::cos(elevation) * std::cos(azimuth),
        };

        // ライト方向は光の進行方向（光源 → 地表）なので逆ベクトル
        return Normalize(-toLight);
    }

    // ==================== 反映 ====================

    void TimeOfDayFeature::ApplyToLights(SceneContext& ctx)
    {
        auto* lightManager = GetLightManager(ctx);
        if (!lightManager) {
            return;
        }

        const float latitudeDeg = cvLatitudeDeg.Get();
        const float azimuthOffset = cvSunAzimuthOffsetDeg.Get();
        ComputeSunAngles(timeOfDay_, latitudeDeg, sunElevationDeg_, sunAzimuthDeg_);
        // 軌道ごと方位方向へ回す。ステージの向きに対して日の出・日没が
        // どの方角に来るかを決める（高度＝時刻との対応は変わらない）
        sunAzimuthDeg_ += azimuthOffset;

        // 夜の度合い。地平線（0°）から薄明の深さまで smoothstep で 1 へ向かう
        const float depth = std::clamp(
            -sunElevationDeg_ / std::max(cvTwilightDeg.Get(), 0.5f), 0.0f, 1.0f);
        nightFactor_ = depth * depth * (3.0f - 2.0f * depth);

        // ---- 太陽（明るさは触らない。空・薄明・地表の減光は大気散乱が向きから導く）----
        // 月の空スケールを合わせる基準として、太陽の空スケールを控えておく。
        // AtmosphereManager::Update() と同じフォールバック規則で求めること
        //（atmosphereIntensity=0 のシーンは照度からの換算値が使われるため、
        // ここで 20 を仮定すると夜空だけ 20 倍明るくなる）。
        float sunSkyScale = 1.0f;
        if (Light* sun = lightManager->GetAtmosphereSunLight()) {
            if (!savedSunValid_) {
                savedSunDirection_ = sun->direction;
                savedSunValid_ = true;
            }
            sun->direction = ComputeLightDirection(sunElevationDeg_, sunAzimuthDeg_);
            sunSkyScale = (sun->atmosphereIntensity > 0.0f)
                ? sun->atmosphereIntensity : LightUnits::LuxToShader(sun->intensity);
        }

        // ---- 月（太陽の 12 時間ずれ＝満月の軌道。太陽が沈んでいる間だけ昇っている）----
        const bool wantMoon = cvMoon.Get();
        Light* moon = lightManager->GetAtmosphereMoonLight();
        if (!moon) {
            if (!wantMoon) {
                return;
            }
            // 月はオプトイン。初回に第2ディレクショナルライトとして生成する
            createdMoon_ = lightManager->CreateLight(LightType::Directional, "Moon");
            moon = lightManager->GetLight(createdMoon_);
            if (!moon) {
                return;  // ディレクショナルライトが上限（4 本）で作れなかった
            }
            moon->isAtmosphereMoon = true;
        }

        if (!savedMoonValid_ && !createdMoon_.IsValid()) {
            // シーンが元から持っていた月は、借りている間の変更を Finalize で返す
            savedMoonEnabled_ = moon->enabled;
            savedMoonDirection_ = moon->direction;
            savedMoonColor_ = moon->color;
            savedMoonIntensity_ = moon->intensity;
            savedMoonSkyIntensity_ = moon->atmosphereIntensity;
            savedMoonValid_ = true;
        }

        float moonElevationDeg = 0.0f;
        float moonAzimuthDeg = 0.0f;
        ComputeSunAngles(timeOfDay_ + 12.0f, latitudeDeg, moonElevationDeg, moonAzimuthDeg);
        moonAzimuthDeg += azimuthOffset;  // 月も同じだけ回して太陽の真反対を保つ

        // 昼間に月を光らせない（地平線下でも大気の透過率でほぼ消えるが、明示的に落とす）
        moon->enabled = wantMoon && moonElevationDeg > 0.0f;
        moon->direction = ComputeLightDirection(moonElevationDeg, moonAzimuthDeg);
        moon->color = cvMoonColor.Get();
        moon->intensity = cvMoonSurfaceIntensity.Get();
        // 空は太陽スケールとの比で置く。地表の月光/日光比（114 lx / 57,143 lx ≒ 1/500）と
        // 桁を揃えないと、地面は暗いのに空だけ明るい夜になる
        moon->atmosphereIntensity = sunSkyScale * cvMoonSkyRatio.Get();
    }

    void TimeOfDayFeature::ApplyAutoExposure(SceneContext& ctx)
    {
        ToneMapping* toneMapping = GetToneMapping(ctx);
        if (!toneMapping) {
            return;
        }

        // 露出は「今どれだけ持ち上げているか」が分からないと調整できないので毎フレーム拾う
        currentAutoEV_ = toneMapping->GetAutoExposureEV();

        const bool want = cvAutoExposure.Get();
        if (want == autoExposureOverridden_) {
            return;  // 既に望みの状態（強制中 or 手を出していない）
        }

        if (want) {
            // 月光は太陽の 1/1000 のため、露出補正なしでは夜がほぼ黒になる
            autoExposureBefore_ = toneMapping->IsAutoExposureEnabled();
            autoExposureOverridden_ = true;
            toneMapping->SetAutoExposureEnabled(true);
        } else {
            toneMapping->SetAutoExposureEnabled(autoExposureBefore_);
            autoExposureOverridden_ = false;
        }
    }

    // ==================== 夜の暗さ（借り物 CVar） ====================

    void TimeOfDayFeature::BorrowFloatCVar(BorrowedCVar& slot, const char* name)
    {
        if (slot.held) {
            return;  // 既に借りている（original は最初に見た値のまま保つ）
        }
        slot.cvar = CVarRegistry::Get().Find(name);
        const float* current = slot.cvar ? slot.cvar->AsFloat() : nullptr;
        if (!current) {
            slot.cvar = nullptr;  // 名前違い・型違い。黙って手を出さない
            return;
        }
        slot.original = *current;
        slot.held = true;
    }

    void TimeOfDayFeature::WriteFloatCVar(BorrowedCVar& slot, float value)
    {
        if (slot.held && slot.cvar) {
            // CVar 側が同値の書き込みを弾くので、毎フレーム呼んでも通知は走らない
            slot.cvar->SetFromPointer(&value);
        }
    }

    void TimeOfDayFeature::ReleaseFloatCVar(BorrowedCVar& slot)
    {
        if (slot.held && slot.cvar) {
            slot.cvar->SetFromPointer(&slot.original);
        }
        slot = {};
    }

    void TimeOfDayFeature::ApplyNightDarkness()
    {
        // 夜の代表輝度は星明かりの下限でクランプされるため、自動 EV は上限に張り付く。
        // つまりこの上限がそのまま「夜の明るさ」になる（昼は +2 EV 程度で上限に届かない）。
        BorrowFloatCVar(maxAutoEV_, "r.AutoExposure.MaxEV");
        WriteFloatCVar(maxAutoEV_, cvNightMaxAutoEV.Get());

        // 空アンビエントは夜だけ絞る。借りた時点の値（作者が調整したもの）を基準に
        // 夜の度合いで倍率を掛けるので、昼の見た目は変わらない
        BorrowFloatCVar(skyAmbientScale_, "r.Atmosphere.SkyAmbientScale");
        const float ambientMul =
            1.0f + (cvNightSkyAmbientMul.Get() - 1.0f) * nightFactor_;
        WriteFloatCVar(skyAmbientScale_, skyAmbientScale_.original * ambientMul);
    }

    void TimeOfDayFeature::ReleaseBorrowedCVars()
    {
        ReleaseFloatCVar(maxAutoEV_);
        ReleaseFloatCVar(skyAmbientScale_);
    }

    // ==================== サービス取得 ====================

    LightManager* TimeOfDayFeature::GetLightManager(SceneContext& ctx)
    {
        return ctx.engine ? ctx.engine->GetService<LightManager>() : nullptr;
    }

    ToneMapping* TimeOfDayFeature::GetToneMapping(SceneContext& ctx)
    {
        auto* postEffect = ctx.engine ? ctx.engine->GetService<PostEffectManager>() : nullptr;
        return postEffect
            ? postEffect->GetEffect<ToneMapping>(PostEffectNames::ToneMapping)
            : nullptr;
    }

    // ==================== 設定パネル ====================

#ifdef USE_IMGUI
    void TimeOfDayFeature::EnsureSettingsPanelRegistered(EngineSystem* engine)
    {
        static bool registered = false;
        if (registered || !engine) {
            return;
        }

        auto* debug = engine->GetDebugSubsystem();
        auto* gameDebugUI = debug ? debug->GetGameDebugUI() : nullptr;
        if (!gameDebugUI) {
            return;
        }

        // ドロワーは何もキャプチャしない（ファイルスコープの s_activeTimeOfDay を読むだけ）
        gameDebugUI->RegisterEnginePanel("Time of Day", [] {
            if (s_activeTimeOfDay) {
                s_activeTimeOfDay->DrawSettingsImGui();
            } else {
                ImGui::TextDisabled("(このシーンには昼夜サイクルがありません)");
            }
        });

        registered = true;
    }

    void TimeOfDayFeature::SetActiveForSettingsPanel(TimeOfDayFeature* feature)
    {
        s_activeTimeOfDay = feature;
    }

    void TimeOfDayFeature::DrawSettingsImGui()
    {
        const int hour = static_cast<int>(timeOfDay_);
        const int minute = static_cast<int>((timeOfDay_ - static_cast<float>(hour)) * 60.0f);
        ImGui::Text("%02d:%02d", hour, minute);
        ImGui::Text("太陽  高度 %.1f°  方位 %.1f°", sunElevationDeg_, sunAzimuthDeg_);
        ImGui::Text("夜の度合い %.2f%s", nightFactor_, IsNight() ? "（地平線下）" : "");

        // 借りている CVar の実効値。夜の暗さはこの 2 つと月光 [lx] で決まる
        const float ambientMul = 1.0f + (cvNightSkyAmbientMul.Get() - 1.0f) * nightFactor_;
        const bool evClamped = currentAutoEV_ >= cvNightMaxAutoEV.Get() - 0.01f;
        ImGui::Text("自動露出 %+.2f EV%s（上限 %.1f）",
            currentAutoEV_, evClamped ? " ← 上限に張り付き" : "", cvNightMaxAutoEV.Get());
        ImGui::Text("空アンビエント %.2f（元 %.2f）",
            skyAmbientScale_.original * ambientMul, skyAmbientScale_.original);

        ImGui::Spacing();

        float hours = timeOfDay_;
        if (ImGui::SliderFloat("時刻 [h]", &hours, 0.0f, 24.0f, "%.2f")) {
            SetTimeOfDay(hours);
        }

        struct Preset { const char* label; float hour; };
        constexpr Preset kPresets[] = {
            { "夜明け 5:00", 5.0f }, { "正午 12:00", 12.0f },
            { "夕暮れ 18:00", 18.0f }, { "深夜 0:00", 0.0f },
        };
        bool first = true;
        for (const Preset& preset : kPresets) {
            if (!first) {
                ImGui::SameLine();
            }
            first = false;
            if (ImGui::SmallButton(preset.label)) {
                SetTimeOfDay(preset.hour);
            }
        }

        ImGui::Spacing();

        // パラメータ UI は CVar から自動生成される（値は Update が毎フレーム取り込む）
        CVarUI::DrawTree(kCVarPrefix);
    }
#endif
}
