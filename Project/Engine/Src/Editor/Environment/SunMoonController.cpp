#include "pch.h"
#include "Editor/Environment/SunMoonController.h"

#ifdef CORE_EDITOR

#include "Editor/Command/EditorCommand.h"
#include "Editor/Command/EditorCommandStack.h"
#include "Editor/Scene/EditorSceneAccess.h"
#include "Editor/Scene/SceneDebugEditor.h"
#include "EngineSystem/EngineSystem.h"
#include "EngineSystem/PlaybackState.h"
#include "GameObject/GameObject.h"
#include "GameObject/GameObjectManager.h"
#include "GameObject/Component/Light/LightComponent.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Graphics/Render/RenderDomainContext.h"
#include "Math/MathCore.h"
#include "Scene/Feature/TimeOfDayFeature.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace CoreEngine
{
    namespace
    {
        constexpr float kDegToRad = MathCore::Constants::kDegToRad;
        constexpr float kRadToDeg = MathCore::Constants::kRadToDeg;

        /// 時刻の機能の緯度と軌道の向きの CVar
        constexpr const char* kTimeOfDayLatitudeCVar = "r.TimeOfDay.LatitudeDeg";
        constexpr const char* kTimeOfDayOrbitOffsetCVar = "r.TimeOfDay.SunAzimuthOffsetDeg";

        /// 時刻の機能が月を出すかの CVar
        constexpr const char* kTimeOfDayMoonCVar = "r.TimeOfDay.Moon";

        /// 時刻の機能の始まりの時刻と月齢の CVar
        constexpr const char* kTimeOfDayStartHourCVar = "r.TimeOfDay.StartHour";
        constexpr const char* kTimeOfDayStartMoonAgeCVar = "r.TimeOfDay.StartMoonAge";

        /// 太陽が時刻の位置にあるとみなす角度 [deg]
        constexpr float kOnPathToleranceDeg = 0.5f;

        /// シーンビューのカメラを覚えておくフレーム数
        constexpr int kViewCameraLifetimeFrames = 3;

        /// 夜のプリセットの月の位置
        constexpr SkyAngles kNightMoonAngles{ 40.0f, 180.0f };

        float NormalizeHours(float hours)
        {
            hours = std::fmod(hours, 24.0f);
            return hours < 0.0f ? hours + 24.0f : hours;
        }

        float ReadFloatCVar(const char* name, float fallback)
        {
            const ICVar* const cvar = CVarRegistry::Get().Find(name);
            const float* const value = cvar ? cvar->AsFloat() : nullptr;
            return value ? *value : fallback;
        }

        bool ReadBoolCVar(const char* name, bool fallback)
        {
            const ICVar* const cvar = CVarRegistry::Get().Find(name);
            const bool* const value = cvar ? cvar->AsBool() : nullptr;
            return value ? *value : fallback;
        }

        /// @brief float の CVar へ書く（無いか型が違えば何もしない）
        void WriteFloatCVar(const char* name, float value)
        {
            ICVar* const cvar = CVarRegistry::Get().Find(name);
            if (cvar && cvar->AsFloat()) {
                cvar->SetFromPointer(&value);
            }
        }

        /// @brief 月齢を 0 以上・朔望月未満へ収める
        float NormalizeMoonAge(float days)
        {
            days = std::fmod(days, TimeOfDayFeature::kSynodicMonthDays);
            return days < 0.0f ? days + TimeOfDayFeature::kSynodicMonthDays : days;
        }

        /// @brief 高度を空の図の範囲へ収める
        SkyAngles ClampAngles(const SkyAngles& angles)
        {
            return SkyAngles{
                std::clamp(angles.elevationDeg, SunMoonController::kMinElevationDeg, 90.0f),
                WrapAzimuth(angles.azimuthDeg) };
        }

        /// @brief 今のシーンへ月のオブジェクトを作り、そのライトを返す（作れなければ nullptr）
        LightComponent* CreateMoonLightObject(EngineSystem* engine)
        {
            SceneManager* const sceneManager = engine ? engine->GetSceneManager() : nullptr;
            GameObjectManager* const objects =
                sceneManager ? sceneManager->GetCurrentGameObjectManager() : nullptr;
            if (!objects) {
                return nullptr;
            }

            auto owned = std::make_unique<GameObject>();
            owned->SetName("Moon");
            GameObject* const object = objects->AddObject(std::move(owned));
            LightComponent* const component =
                object ? object->AddComponent<LightComponent>(LightType::Directional) : nullptr;
            if (!component) {
                return nullptr;
            }

            component->Get().isAtmosphereMoon = true;
            component->SyncWithManager();

            if (!component->GetLight()) {
                // ディレクショナルライトが上限で実体を作れなかった
                object->Destroy();
                return nullptr;
            }
            return component;
        }

        /// @brief ライトへ値と有効を戻して、実体へ写す
        void RestoreLight(LightComponent* light, const Light& values, bool enabled)
        {
            if (!light) {
                return;
            }
            light->Get() = values;
            light->SetEnabled(enabled);
            light->SyncWithManager();
        }

        /// @brief パネルで触る値が同じか
        /// @param compareDirection 向きも比べるか（時刻の機能が向きを決めるときは比べない）
        bool SameLightValues(const Light& a, const Light& b, bool compareDirection)
        {
            return (!compareDirection || a.direction == b.direction) && a.color == b.color
                && a.intensity == b.intensity && a.atmosphereIntensity == b.atmosphereIntensity;
        }
    }

    // ==================== 角度の計算 ====================

    const char* CompassName(float azimuthDeg)
    {
        static constexpr const char* kNames[] = { "北", "北東", "東", "南東", "南", "南西", "西", "北西" };
        float a = std::fmod(azimuthDeg, 360.0f);
        if (a < 0.0f) {
            a += 360.0f;
        }
        const int index = static_cast<int>(std::floor((a + 22.5f) / 45.0f)) % 8;
        return kNames[index];
    }

    float WrapAzimuth(float azimuthDeg)
    {
        float a = std::fmod(azimuthDeg + 180.0f, 360.0f);
        if (a < 0.0f) {
            a += 360.0f;
        }
        return a - 180.0f;
    }

    float AzimuthForDisplay(float azimuthDeg)
    {
        float a = std::fmod(azimuthDeg, 360.0f);
        if (a < 0.0f) {
            a += 360.0f;
        }
        return a >= 359.95f ? 0.0f : a;
    }

    Vector3 SkyAnglesToDirection(const SkyAngles& angles)
    {
        const float elevation = angles.elevationDeg * kDegToRad;
        const float azimuth = angles.azimuthDeg * kDegToRad;
        return Vector3{
            std::cos(elevation) * std::sin(azimuth),
            std::sin(elevation),
            std::cos(elevation) * std::cos(azimuth) };
    }

    SkyAngles DirectionToSkyAngles(const Vector3& toBody, float fallbackAzimuthDeg)
    {
        const Vector3 direction = Normalize(toBody);
        SkyAngles angles;
        angles.elevationDeg = std::asin(std::clamp(direction.y, -1.0f, 1.0f)) * kRadToDeg;
        angles.azimuthDeg = (std::abs(direction.x) > 1.0e-5f || std::abs(direction.z) > 1.0e-5f)
            ? std::atan2(direction.x, direction.z) * kRadToDeg
            : fallbackAzimuthDeg;
        return angles;
    }

    float AngleBetween(const SkyAngles& a, const SkyAngles& b)
    {
        const float cosine = Dot(SkyAnglesToDirection(a), SkyAnglesToDirection(b));
        return std::acos(std::clamp(cosine, -1.0f, 1.0f)) * kRadToDeg;
    }

    // ==================== 太陽と月の Undo ====================

    namespace
    {
        using LightSnapshot = SunMoonController::LightSnapshot;

        /// @brief 空と大気散乱のパネルとシーンビューで変えた太陽と月の値を戻す操作
        class SunMoonEditCommand final : public Editor::IEditorCommand
        {
        public:
            SunMoonEditCommand(const LightSnapshot& before, const LightSnapshot& after)
                : before_(before), after_(after) {}

            void Undo() override { Restore(before_, after_); }
            void Redo() override { Restore(after_, before_); }
            std::string GetLabel() const override { return "太陽と月の変更"; }

            bool References(const void* target) const override
            {
                return target != nullptr && (target == before_.sun || target == before_.moon
                    || target == after_.sun || target == after_.moon);
            }

        private:
            /// @brief 控えた状態へ戻す（月が無かった状態へ戻すときは、今ある月を消す）
            static void Restore(const LightSnapshot& target, const LightSnapshot& other)
            {
                RestoreLight(target.sun, target.sunValues, target.sunEnabled);
                if (target.moon) {
                    RestoreLight(target.moon, target.moonValues, target.moonEnabled);
                } else if (other.moon) {
                    RestoreLight(other.moon, other.moonValues, false);
                }
            }

            LightSnapshot before_;
            LightSnapshot after_;
        };
    }

    // ==================== 読み書き ====================

    void SunMoonController::Initialize(EngineSystem& engine)
    {
        engine_ = &engine;
    }

    void SunMoonController::SyncFromLights()
    {
        LightManager* const lightManager = GetLightManager();
        if (!lightManager) {
            return;
        }

        if (const Light* sun = lightManager->GetAtmosphereSunLight()) {
            sun_.angles = DirectionToSkyAngles(-sun->direction, sun_.angles.azimuthDeg);
            sun_.skyIntensity = sun->atmosphereIntensity;
            sun_.illuminanceLux = sun->intensity;
            sun_.color = sun->color;
        }

        if (const Light* moon = lightManager->GetAtmosphereMoonLight()) {
            moon_.enabled = moon->enabled;
            moon_.angles = DirectionToSkyAngles(-moon->direction, moon_.angles.azimuthDeg);
            moon_.skyIntensity = moon->atmosphereIntensity;
            moon_.illuminanceLux = moon->intensity;
            moon_.color = moon->color;
        } else {
            // 月のライトが無ければ出していない扱いにし、色などの値はそのまま残す
            moon_.enabled = false;
        }
    }

    bool SunMoonController::HasSun() const
    {
        return FindLight(SkyBody::Sun) != nullptr;
    }

    void SunMoonController::ApplySun(const SunSettings& settings)
    {
        sun_ = settings;
        sun_.angles = ClampAngles(settings.angles);

        if (LightComponent* const sun = FindLight(SkyBody::Sun)) {
            Light& values = sun->Get();
            values.direction = ComputeSunLightDirection(sun_.angles.elevationDeg, sun_.angles.azimuthDeg);
            values.atmosphereIntensity = sun_.skyIntensity;
            values.intensity = sun_.illuminanceLux;
            values.color = sun_.color;
            sun->SyncWithManager();
        }
    }

    void SunMoonController::ApplyMoon(const MoonSettings& settings)
    {
        moon_ = settings;
        moon_.angles = ClampAngles(settings.angles);

        LightComponent* moon = FindLight(SkyBody::Moon);
        if (!moon && settings.enabled) {
            moon = CreateMoonLightObject(engine_);
        }
        if (!moon) {
            return;
        }

        Light& values = moon->Get();
        values.direction = ComputeSunLightDirection(moon_.angles.elevationDeg, moon_.angles.azimuthDeg);
        values.color = moon_.color;
        values.intensity = moon_.illuminanceLux;
        values.atmosphereIntensity = moon_.skyIntensity;
        moon->SetEnabled(moon_.enabled);
        moon->SyncWithManager();
    }

    SkyAngles SunMoonController::GetAngles(SkyBody body) const
    {
        return body == SkyBody::Sun ? sun_.angles : moon_.angles;
    }

    bool SunMoonController::IsShown(SkyBody body) const
    {
        if (body == SkyBody::Sun) {
            return HasSun();
        }
        // 時刻の機能があるときは、その機能の「月を出す」の設定で決める
        if (IsTimeDriven()) {
            return ReadBoolCVar(kTimeOfDayMoonCVar, false) && FindLight(SkyBody::Moon) != nullptr;
        }
        return moon_.enabled;
    }

    bool SunMoonController::CanMove(SkyBody body) const
    {
        return IsShown(body);
    }

    void SunMoonController::MoveTo(SkyBody body, const SkyAngles& angles)
    {
        if (!CanMove(body)) {
            return;
        }
        if (const TimeOfDayFeature* const timeOfDay = FindTimeOfDay()) {
            // 太陽は時刻を、月は太陽から遅れる時間（月齢）を変えて、軌道の上のいちばん近い所へ動かす
            const float hours = NearestTimeOnPath(angles);
            if (body == SkyBody::Sun) {
                SetTimeOfDay(hours);
            } else {
                const float lagHours = NormalizeHours(timeOfDay->GetTimeOfDay() - hours);
                SetMoonAge(lagHours / 24.0f * TimeOfDayFeature::kSynodicMonthDays);
            }
            return;
        }
        if (body == SkyBody::Sun) {
            SunSettings settings = sun_;
            settings.angles = angles;
            ApplySun(settings);
            return;
        }
        MoonSettings settings = moon_;
        settings.angles = angles;
        ApplyMoon(settings);
    }

    // ==================== 時刻 ====================

    TimeOfDayFeature* SunMoonController::FindTimeOfDay() const
    {
        SceneManager* const sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
        const Scene* const scene = sceneManager ? dynamic_cast<const Scene*>(sceneManager->GetCurrentScene()) : nullptr;
        return scene ? scene->GetFeature<TimeOfDayFeature>() : nullptr;
    }

    float SunMoonController::GetTimeOfDay() const
    {
        const TimeOfDayFeature* const timeOfDay = FindTimeOfDay();
        return timeOfDay ? timeOfDay->GetTimeOfDay() : timeOfDay_;
    }

    void SunMoonController::SetTimeOfDay(float hours)
    {
        if (TimeOfDayFeature* const timeOfDay = FindTimeOfDay()) {
            timeOfDay->SetTimeOfDay(hours);
            // 停止中は始まりの時刻にも書いて、シーンに残す
            if (WritesStartValues()) {
                WriteFloatCVar(kTimeOfDayStartHourCVar, NormalizeHours(hours));
            }
            return;
        }
        timeOfDay_ = NormalizeHours(hours);
        ApplyLocalTime();
    }

    float SunMoonController::GetLatitudeDeg() const
    {
        return IsTimeDriven() ? ReadFloatCVar(kTimeOfDayLatitudeCVar, latitudeDeg_) : latitudeDeg_;
    }

    void SunMoonController::SetLatitudeDeg(float latitudeDeg)
    {
        if (IsTimeDriven()) {
            return;
        }
        latitudeDeg_ = std::clamp(latitudeDeg, -89.0f, 89.0f);
        ApplyLocalTime();
    }

    float SunMoonController::GetOrbitOffsetDeg() const
    {
        return IsTimeDriven() ? ReadFloatCVar(kTimeOfDayOrbitOffsetCVar, orbitOffsetDeg_) : orbitOffsetDeg_;
    }

    void SunMoonController::SetOrbitOffsetDeg(float offsetDeg)
    {
        if (IsTimeDriven()) {
            return;
        }
        orbitOffsetDeg_ = WrapAzimuth(offsetDeg);
        ApplyLocalTime();
    }

    SkyAngles SunMoonController::SunAnglesAt(float hours) const
    {
        SkyAngles angles;
        TimeOfDayFeature::ComputeSunAngles(hours, GetLatitudeDeg(), angles.elevationDeg, angles.azimuthDeg);
        angles.azimuthDeg = WrapAzimuth(angles.azimuthDeg + GetOrbitOffsetDeg());
        return angles;
    }

    float SunMoonController::GetMoonAge() const
    {
        const TimeOfDayFeature* const timeOfDay = FindTimeOfDay();
        return timeOfDay ? timeOfDay->GetMoonAge() : -1.0f;
    }

    void SunMoonController::SetMoonAge(float days)
    {
        if (TimeOfDayFeature* const timeOfDay = FindTimeOfDay()) {
            timeOfDay->SetMoonAge(days);
            // 停止中は始まりの月齢にも書いて、シーンに残す
            if (WritesStartValues()) {
                WriteFloatCVar(kTimeOfDayStartMoonAgeCVar, NormalizeMoonAge(days));
            }
        }
    }

    bool SunMoonController::WritesStartValues() const
    {
        return IsTimeDriven() && PlaybackStateManager::GetInstance().IsEditing();
    }

    SunMoonController::StartValues SunMoonController::ReadStartValues() const
    {
        return StartValues{
            ReadFloatCVar(kTimeOfDayStartHourCVar, 0.0f),
            ReadFloatCVar(kTimeOfDayStartMoonAgeCVar, 0.0f) };
    }

    bool SunMoonController::IsSunOnTimePath() const
    {
        return AngleBetween(sun_.angles, ClampAngles(SunAnglesAt(GetTimeOfDay()))) <= kOnPathToleranceDeg;
    }

    void SunMoonController::Tick(float deltaSeconds)
    {
        const int frame = ImGui::GetFrameCount();
        if (frame == lastTickFrame_) {
            return;
        }
        lastTickFrame_ = frame;
        if (!autoAdvance_ || IsTimeDriven()) {
            return;
        }
        timeOfDay_ = NormalizeHours(timeOfDay_ + advanceHoursPerSecond_ * deltaSeconds);
        ApplyLocalTime();
    }

    void SunMoonController::ApplyLocalTime()
    {
        SunSettings settings = sun_;
        settings.angles = SunAnglesAt(timeOfDay_);
        ApplySun(settings);
    }

    float SunMoonController::NearestTimeOnPath(const SkyAngles& angles) const
    {
        // 5 分おきに探してから、前後 5 分を 15 秒おきに詰める
        constexpr int kCoarseSteps = 24 * 12;
        constexpr float kCoarseStep = 24.0f / kCoarseSteps;
        float bestHours = 0.0f;
        float bestAngle = 1.0e9f;
        for (int i = 0; i < kCoarseSteps; ++i) {
            const float hours = kCoarseStep * static_cast<float>(i);
            const float angle = AngleBetween(angles, SunAnglesAt(hours));
            if (angle < bestAngle) {
                bestAngle = angle;
                bestHours = hours;
            }
        }
        constexpr int kFineSteps = 40;
        const float start = bestHours - kCoarseStep;
        for (int i = 0; i <= kFineSteps; ++i) {
            const float hours = start + 2.0f * kCoarseStep * static_cast<float>(i) / kFineSteps;
            const float angle = AngleBetween(angles, SunAnglesAt(hours));
            if (angle < bestAngle) {
                bestAngle = angle;
                bestHours = hours;
            }
        }
        return NormalizeHours(bestHours);
    }

    // ==================== まとめて置く ====================

    void SunMoonController::ApplyDaytimePreset(float hour)
    {
        SetTimeOfDay(hour);
        if (!IsTimeDriven()) {
            RestoreAutoExposure();
        }
    }

    void SunMoonController::ApplyNightPreset()
    {
        // 時刻の機能があれば、月と自動露出はその機能が受け持つ
        if (IsTimeDriven()) {
            SetTimeOfDay(0.0f);
            return;
        }

        SetTimeOfDay(0.0f);

        MoonSettings moon = moon_;
        moon.enabled = true;
        moon.angles = kNightMoonAngles;
        ApplyMoon(moon);

        if (ToneMapping* const toneMapping = GetToneMapping()) {
            // 何度押しても、最初に控えた値（押す前の設定）を残す
            if (!autoExposureSaved_) {
                autoExposureBeforeNight_ = toneMapping->IsAutoExposureEnabled();
                autoExposureSaved_ = true;
            }
            toneMapping->SetAutoExposureEnabled(true);
        }
    }

    void SunMoonController::RestoreAutoExposure()
    {
        if (!autoExposureSaved_) {
            return;
        }
        if (ToneMapping* const toneMapping = GetToneMapping()) {
            // 夜の間に自分で切っていれば、そのままにする
            if (toneMapping->IsAutoExposureEnabled()) {
                toneMapping->SetAutoExposureEnabled(autoExposureBeforeNight_);
            }
        }
        autoExposureSaved_ = false;
    }

    void SunMoonController::PlaceMoonOppositeSun()
    {
        // 時刻の機能の月は、満月（太陽から 12 時間遅れ）でちょうど反対に来る
        if (IsTimeDriven()) {
            SetMoonAge(TimeOfDayFeature::kSynodicMonthDays * 0.5f);
            return;
        }
        MoonSettings moon = moon_;
        moon.enabled = true;
        // 0 から引いて、太陽が地平線にあるときに -0 を作らない
        moon.angles = SkyAngles{ 0.0f - sun_.angles.elevationDeg, sun_.angles.azimuthDeg + 180.0f };
        ApplyMoon(moon);
    }

    bool SunMoonController::PlaceAtView(SkyBody body)
    {
        const SkyViewCamera* const camera = GetViewCamera();
        if (!camera || !CanMove(body)) {
            return false;
        }
        MoveTo(body, DirectionToSkyAngles(camera->forward, GetAngles(body).azimuthDeg));
        return true;
    }

    // ==================== 履歴 ====================

    SunMoonController::LightSnapshot SunMoonController::Capture() const
    {
        LightSnapshot state;
        state.sun = FindLight(SkyBody::Sun);
        state.moon = FindLight(SkyBody::Moon);
        if (state.sun) {
            state.sunValues = state.sun->Get();
            state.sunEnabled = state.sun->IsEnabled();
        }
        if (state.moon) {
            state.moonValues = state.moon->Get();
            state.moonEnabled = state.moon->IsEnabled();
        }
        return state;
    }

    void SunMoonController::BeginEdit()
    {
        if (!editBefore_) {
            editBefore_ = Capture();
            if (WritesStartValues()) {
                startBefore_ = ReadStartValues();
            }
        }
    }

    void SunMoonController::EndEdit()
    {
        editEnded_ = false;
        if (!editBefore_) {
            return;
        }
        const LightSnapshot before = *editBefore_;
        editBefore_.reset();
        const std::optional<StartValues> startBefore = startBefore_;
        startBefore_.reset();

        // 始まりの時刻と月齢が変わっていれば、2 つまとめて 1 件積む（CVar の値なのでシーンの未保存には数えない）
        if (startBefore) {
            const StartValues startAfter = ReadStartValues();
            if (startAfter.hour != startBefore->hour || startAfter.moonAgeDays != startBefore->moonAgeDays) {
                const auto apply = [](const StartValues& values) {
                    WriteFloatCVar(kTimeOfDayStartHourCVar, values.hour);
                    WriteFloatCVar(kTimeOfDayStartMoonAgeCVar, values.moonAgeDays);
                };
                Editor::EditorCommandStack::Get().Push(std::make_unique<Editor::FunctionCommand>(
                    "時刻と月齢の変更",
                    [apply, values = *startBefore] { apply(values); },
                    [apply, values = startAfter] { apply(values); },
                    false, true));
            }
        }

        // 時刻の機能があるときは、太陽の向き以外の値だけを比べる（月は比べない）
        const LightSnapshot after = Capture();
        const bool timeDriven = IsTimeDriven();
        const bool sunSame = before.sun == after.sun && (!after.sun
            || (SameLightValues(before.sunValues, after.sunValues, !timeDriven) && before.sunEnabled == after.sunEnabled));
        const bool moonSame = timeDriven || (before.moon == after.moon && (!after.moon
            || (SameLightValues(before.moonValues, after.moonValues, true) && before.moonEnabled == after.moonEnabled)));
        if (sunSame && moonSame) {
            return;
        }

        Editor::EditorCommandStack::Get().Push(std::make_unique<SunMoonEditCommand>(before, after));
    }

    void SunMoonController::TrackItem()
    {
        if (ImGui::IsItemActivated()) {
            BeginEdit();
        }
        if (ImGui::IsItemDeactivated()) {
            editEnded_ = true;
        }
    }

    void SunMoonController::CommitTrackedEdit()
    {
        if (editEnded_) {
            EndEdit();
        }
    }

    // ==================== シーンビューのカメラ ====================

    void SunMoonController::SetViewCamera(const SkyViewCamera& camera)
    {
        viewCamera_ = camera;
        viewCameraFrame_ = ImGui::GetFrameCount();
    }

    const SkyViewCamera* SunMoonController::GetViewCamera() const
    {
        return ImGui::GetFrameCount() - viewCameraFrame_ <= kViewCameraLifetimeFrames ? &viewCamera_ : nullptr;
    }

    // ==================== オブジェクト ====================

    LightComponent* SunMoonController::FindLight(SkyBody body) const
    {
        const LightManager* const lightManager = GetLightManager();
        if (!lightManager) {
            return nullptr;
        }
        return LightComponent::Find(body == SkyBody::Sun
            ? lightManager->GetAtmosphereSunHandle()
            : lightManager->GetAtmosphereMoonHandle());
    }

    bool SunMoonController::IsSkyLightObject(const GameObject* object) const
    {
        if (!object) {
            return false;
        }
        for (const SkyBody body : { SkyBody::Sun, SkyBody::Moon }) {
            const LightComponent* const light = FindLight(body);
            if (light && light->GetOwner() == object) {
                return true;
            }
        }
        return false;
    }

    void SunMoonController::SelectLightObject(SkyBody body) const
    {
        const LightComponent* const light = FindLight(body);
        GameObject* const owner = light ? light->GetOwner() : nullptr;
        SceneDebugEditor* const editor = Editor::SceneAccess::SceneEditor();
        if (owner && editor) {
            editor->SelectFromViewport(owner);
        }
    }

    AtmosphereManager* SunMoonController::GetAtmosphereManager() const
    {
        if (!engine_ || !engine_->GetRenderDomainContext()) {
            return nullptr;
        }
        return engine_->GetRenderDomainContext()->GetAtmosphereManager();
    }

    LightManager* SunMoonController::GetLightManager() const
    {
        return engine_ ? engine_->GetService<LightManager>() : nullptr;
    }

    ToneMapping* SunMoonController::GetToneMapping() const
    {
        PostEffectManager* const postEffect = engine_ ? engine_->GetService<PostEffectManager>() : nullptr;
        return postEffect ? postEffect->GetEffect<ToneMapping>(PostEffectNames::ToneMapping) : nullptr;
    }
}

#endif // CORE_EDITOR
