#include "pch.h"
#include "AtmosphereEditorFacade.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/RenderDomainContext.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#endif

#include <cmath>

using namespace CoreEngine;

namespace {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    constexpr const char* kEditorLabel = "Sky Atmosphere";
}

void AtmosphereEditorFacade::Initialize(EngineSystem& engine)
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

AtmosphereEditorFacade::~AtmosphereEditorFacade()
{
#ifdef USE_IMGUI
    // シーン破棄後にドロワーがダングリングしないよう登録を解除する
    if (engine_) {
        if (auto* debug = engine_->GetDebugSubsystem()) {
            if (auto* ui = debug->GetGameDebugUI()) {
                ui->UnregisterEnvironmentEditor(kEditorLabel, this);
            }
        }
    }
#endif
}

Vector3 AtmosphereEditorFacade::ComputeSunLightDirection(float elevationDeg, float azimuthDeg)
{
    const float elevation = elevationDeg * kDegToRad;
    const float azimuth = azimuthDeg * kDegToRad;

    // 太陽の位置方向（地表から太陽を見る方向）
    const Vector3 toSun = {
        std::cos(elevation) * std::sin(azimuth),
        std::sin(elevation),
        std::cos(elevation) * std::cos(azimuth),
    };

    // ライト方向は光の進行方向（太陽 → 地表）なので逆ベクトル
    return MathCore::Vector::Normalize({ -toSun.x, -toSun.y, -toSun.z });
}

void AtmosphereEditorFacade::ApplySunSettings(const AtmosphereEditorSunSettings& settings)
{
    sunSettings_ = settings;

    if (auto* lightManager = GetLightManager()) {
        if (DirectionalLightData* sun = lightManager->GetAtmosphereSunLight()) {
            sun->direction = ComputeSunLightDirection(settings.elevationDeg, settings.azimuthDeg);
            // UI の「強度」は空（大気散乱）の輝度スケール。サーフェスの直接光（sun->intensity）は
            // 単位系が別なので触らない（同じ値を入れると明るいアルベドが ACES の飽和域に入る）。
            sun->atmosphereIntensity = settings.intensity;
        }
    }

    // 太陽方向・強度の変化は AtmosphereManager::Update() が自動検知して
    // Sky-View LUT のみを再生成するため、ここで MarkLUTDirty()（全LUT再生成）は呼ばない
}

void AtmosphereEditorFacade::DrawContent()
{
#ifdef USE_IMGUI
    // ===== 太陽設定 =====
    if (ImGui::CollapsingHeader("太陽", ImGuiTreeNodeFlags_DefaultOpen)) {
        AtmosphereEditorSunSettings settings = sunSettings_;
        bool changed = false;
        changed |= ImGui::SliderFloat("高度角 [deg]", &settings.elevationDeg, -20.0f, 90.0f, "%.1f");
        changed |= ImGui::SliderFloat("方位角 [deg]", &settings.azimuthDeg, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("強度", &settings.intensity, 0.0f, 100.0f, "%.2f");

        // 昼夜遷移の確認用: 高度角を自動で動かす（90°→-20°を往復）
        ImGui::Checkbox("太陽を自動で動かす", &autoSunCycle_);
        if (autoSunCycle_) {
            ImGui::SliderFloat("旋回速度 [deg/s]", &sunCycleSpeedDegPerSec_, 1.0f, 45.0f, "%.1f");
            static float cycleDirection = -1.0f;
            settings.elevationDeg += cycleDirection * sunCycleSpeedDegPerSec_ * ImGui::GetIO().DeltaTime;
            if (settings.elevationDeg < -20.0f) { settings.elevationDeg = -20.0f; cycleDirection = 1.0f; }
            if (settings.elevationDeg > 90.0f)  { settings.elevationDeg = 90.0f;  cycleDirection = -1.0f; }
            changed = true;
        }

        if (changed) {
            ApplySunSettings(settings);
        }
    }

    // ===== 大気パラメータ =====
    if (ImGui::CollapsingHeader("大気パラメータ")) {
        if (auto* atmosphereManager = GetAtmosphereManager()) {
            auto& params = atmosphereManager->GetParametersMutable();
            bool paramsChanged = false;

            ImGui::SeparatorText("レイリー散乱（青空の成分）");
            float rayleigh[3] = {
                params.rayleighScattering.x * 1e6f,
                params.rayleighScattering.y * 1e6f,
                params.rayleighScattering.z * 1e6f };
            if (ImGui::DragFloat3("散乱係数 [1e-6/m]##rayleigh", rayleigh, 0.1f, 0.0f, 200.0f, "%.2f")) {
                params.rayleighScattering = { rayleigh[0] * 1e-6f, rayleigh[1] * 1e-6f, rayleigh[2] * 1e-6f };
                paramsChanged = true;
            }
            paramsChanged |= ImGui::DragFloat("スケールハイト [m]##rayleigh", &params.rayleighScaleHeight, 50.0f, 1000.0f, 20000.0f, "%.0f");

            ImGui::SeparatorText("ミー散乱（霞・太陽周りのハロ）");
            float mieScattering = params.mieScattering * 1e6f;
            if (ImGui::DragFloat("散乱係数 [1e-6/m]##mie", &mieScattering, 0.1f, 0.0f, 200.0f, "%.2f")) {
                params.mieScattering = mieScattering * 1e-6f;
                paramsChanged = true;
            }
            float mieAbsorption = params.mieAbsorption * 1e6f;
            if (ImGui::DragFloat("吸収係数 [1e-6/m]##mie", &mieAbsorption, 0.1f, 0.0f, 100.0f, "%.2f")) {
                params.mieAbsorption = mieAbsorption * 1e-6f;
                paramsChanged = true;
            }
            paramsChanged |= ImGui::DragFloat("スケールハイト [m]##mie", &params.mieScaleHeight, 10.0f, 100.0f, 5000.0f, "%.0f");
            paramsChanged |= ImGui::SliderFloat("位相 g（前方散乱度）", &params.miePhaseG, 0.0f, 0.99f, "%.2f");

            ImGui::SeparatorText("オゾン吸収（夕暮れの色再現）");
            float ozone[3] = {
                params.ozoneAbsorption.x * 1e6f,
                params.ozoneAbsorption.y * 1e6f,
                params.ozoneAbsorption.z * 1e6f };
            if (ImGui::DragFloat3("吸収係数 [1e-6/m]##ozone", ozone, 0.05f, 0.0f, 20.0f, "%.3f")) {
                params.ozoneAbsorption = { ozone[0] * 1e-6f, ozone[1] * 1e-6f, ozone[2] * 1e-6f };
                paramsChanged = true;
            }
            paramsChanged |= ImGui::DragFloat("層中心高度 [m]##ozone", &params.ozoneLayerCenter, 100.0f, 5000.0f, 60000.0f, "%.0f");
            paramsChanged |= ImGui::DragFloat("層半幅 [m]##ozone", &params.ozoneLayerHalfWidth, 100.0f, 1000.0f, 30000.0f, "%.0f");

            ImGui::SeparatorText("地表・その他");
            float albedo[3] = { params.groundAlbedo.x, params.groundAlbedo.y, params.groundAlbedo.z };
            if (ImGui::ColorEdit3("地表アルベド", albedo)) {
                params.groundAlbedo = { albedo[0], albedo[1], albedo[2] };
                paramsChanged = true;
            }
            paramsChanged |= ImGui::DragFloat("地表とみなすY座標 [m]", &params.groundLevelY, 1.0f, -1000.0f, 1000.0f, "%.1f");

            ImGui::SeparatorText("太陽ディスク");
            paramsChanged |= ImGui::SliderFloat("視半径 [deg]", &params.sunDiskAngularRadiusDeg, 0.05f, 2.0f, "%.3f");
            paramsChanged |= ImGui::DragFloat("輝度スケール", &params.sunDiskLuminanceScale, 1.0f, 0.0f, 1000.0f, "%.1f");

            ImGui::Spacing();
            if (ImGui::Button("大気パラメータをリセット")) {
                const float groundLevelY = params.groundLevelY; // シーン設定は維持する
                params = CoreEngine::AtmosphereParameters{};
                params.groundLevelY = groundLevelY;
                paramsChanged = true;
            }

            if (paramsChanged) {
                atmosphereManager->MarkLUTDirty();
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "AtmosphereManager が見つかりません");
        }
    }

    // ===== 診断情報（AtmosphereManager の計算結果） =====
    if (ImGui::CollapsingHeader("診断", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* atmosphereManager = GetAtmosphereManager()) {
            const Vector3& sunDir = atmosphereManager->GetSunDirection();
            ImGui::Text("太陽高度角（UI設定値）: %.1f deg", sunSettings_.elevationDeg);
            ImGui::Text("太陽ライト方向: (%.3f, %.3f, %.3f)", sunDir.x, sunDir.y, sunDir.z);
            ImGui::Text("太陽ライト有効: %s", atmosphereManager->HasSunLight() ? "true" : "false");
            ImGui::Text("太陽光強度: %.2f", atmosphereManager->GetSunIntensity());
            ImGui::Text("カメラ高度（地表基準）: %.2f m", atmosphereManager->GetCameraHeightAboveGround());
            ImGui::Text("惑星中心距離: %.1f m", atmosphereManager->GetDistanceFromPlanetCenter());
            ImGui::Text("LUT再計算要求: %s", atmosphereManager->IsLUTDirty() ? "true" : "false");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "AtmosphereManager が見つかりません");
        }
    }
#endif
}

AtmosphereManager* AtmosphereEditorFacade::GetAtmosphereManager() const
{
    if (!engine_ || !engine_->GetRenderDomainContext()) {
        return nullptr;
    }
    return engine_->GetRenderDomainContext()->GetAtmosphereManager();
}

LightManager* AtmosphereEditorFacade::GetLightManager() const
{
    if (!engine_) {
        return nullptr;
    }
    return engine_->GetComponent<LightManager>();
}
