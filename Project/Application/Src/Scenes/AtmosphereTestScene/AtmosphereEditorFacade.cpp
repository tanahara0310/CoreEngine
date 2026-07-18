#include "pch.h"
#include "AtmosphereEditorFacade.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/PostEffect/Effect/PostEffectManager.h"
#include "Graphics/PostEffect/Effect/PostEffectNames.h"
#include "Graphics/PostEffect/Effect/ToneMapping/ToneMapping.h"
#include "Graphics/Render/RenderDomainContext.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "EngineSystem/Subsystem/DebugSubsystem.h"
#endif

#include <algorithm>
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

void AtmosphereEditorFacade::ApplyMoonSettings(const AtmosphereEditorMoonSettings& settings)
{
    moonSettings_ = settings;

    auto* lightManager = GetLightManager();
    if (!lightManager) {
        return;
    }

    DirectionalLightData* moon = lightManager->GetAtmosphereMoonLight();
    if (!moon && settings.enabled) {
        // 月ライトはオプトイン。初回有効化時に第2ディレクショナルライトとして生成する
        // （LightManager は最大数ぶん reserve 済みのため、追加で既存ポインタは無効化されない）
        moon = lightManager->AddDirectionalLight();
        if (moon) {
            moon->isAtmosphereMoon = true;
        }
    }
    if (!moon) {
        return;
    }

    moon->enabled = settings.enabled;
    moon->direction = ComputeSunLightDirection(settings.elevationDeg, settings.azimuthDeg);
    moon->color = { settings.color.x, settings.color.y, settings.color.z, 1.0f };
    // 太陽と同じく「空の輝度」と「サーフェス直接光」は単位系が別（ACES 飽和域回避）
    moon->intensity = settings.surfaceIntensity;
    moon->atmosphereIntensity = settings.skyIntensity;

    // 月の変化も AtmosphereManager::Update() が自動検知して Sky-View LUT を再生成する
}

void AtmosphereEditorFacade::DrawContent()
{
#ifdef USE_IMGUI
    // ===== 太陽・月の配置（UE 風の直接操作） =====
    if (ImGui::CollapsingHeader("太陽と月の配置", ImGuiTreeNodeFlags_DefaultOpen)) {
        // --- プリセット（1クリックで代表的な時間帯へ） ---
        if (ImGui::Button("正午")) { timeOfDay_ = 12.0f; ApplyTimeOfDay(); }
        ImGui::SameLine();
        if (ImGui::Button("朝")) { timeOfDay_ = 6.7f; ApplyTimeOfDay(); }
        ImGui::SameLine();
        if (ImGui::Button("夕暮れ")) { timeOfDay_ = 17.6f; ApplyTimeOfDay(); }
        ImGui::SameLine();
        if (ImGui::Button("夜（満月）")) {
            timeOfDay_ = 0.0f;
            ApplyTimeOfDay();
            AtmosphereEditorMoonSettings moon = moonSettings_;
            moon.enabled = true;
            moon.elevationDeg = 40.0f;
            moon.azimuthDeg = 180.0f;
            ApplyMoonSettings(moon);
            // 月光は物理準拠の暗さ（太陽の1/1000）のため、露出補正なしではほぼ黒になる。
            // 自動露出（Krawczyk キー＝暗さの絶対感は保持）を有効化して「暗いが見える夜」にする
            if (engine_) {
                if (auto* postEffect = engine_->GetComponent<PostEffectManager>()) {
                    if (auto* toneMapping = postEffect->GetEffect<ToneMapping>(PostEffectNames::ToneMapping)) {
                        toneMapping->SetAutoExposureEnabled(true);
                    }
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("「夜（満月）」は自動露出も有効化します\n（月光は物理準拠の暗さのため露出補正が必要）");
        }

        // --- スカイマップ（太陽・月をドラッグで配置） ---
        DrawSunMoonPlacementWidget();

        // --- 時刻ベースの太陽軌道（UE の Sun Position 相当の簡易版） ---
        ImGui::SeparatorText("時刻から配置");
        bool timeChanged = false;
        timeChanged |= ImGui::SliderFloat("時刻 [h]", &timeOfDay_, 0.0f, 24.0f, "%.2f");
        timeChanged |= ImGui::SliderFloat("緯度 [deg]", &latitudeDeg_, -89.0f, 89.0f, "%.0f");
        ImGui::Checkbox("時刻を自動で進める", &autoTimeCycle_);
        if (autoTimeCycle_) {
            ImGui::SliderFloat("進行速度 [h/s]", &timeSpeedHoursPerSec_, 0.05f, 4.0f, "%.2f");
            timeOfDay_ += timeSpeedHoursPerSec_ * ImGui::GetIO().DeltaTime;
            if (timeOfDay_ >= 24.0f) { timeOfDay_ -= 24.0f; }
            timeChanged = true;
        }
        if (timeChanged) {
            ApplyTimeOfDay();
        }
    }

    // ===== 太陽設定 =====
    if (ImGui::CollapsingHeader("太陽", ImGuiTreeNodeFlags_DefaultOpen)) {
        AtmosphereEditorSunSettings settings = sunSettings_;
        bool changed = false;
        changed |= ImGui::SliderFloat("高度角 [deg]", &settings.elevationDeg, -20.0f, 90.0f, "%.1f");
        changed |= ImGui::SliderFloat("方位角 [deg]", &settings.azimuthDeg, -180.0f, 180.0f, "%.1f");
        changed |= ImGui::SliderFloat("強度", &settings.intensity, 0.0f, 100.0f, "%.2f");
        if (changed) {
            ApplySunSettings(settings);
        }

        // 太陽直接光への大気透過率適用（UE の Transmittance on light color 相当）。
        // OFF にすると従来どおり日没後も地表が昼の明るさのまま照らされる
        if (auto* atmosphereManager = GetAtmosphereManager()) {
            bool transmittanceOnLight = atmosphereManager->IsTransmittanceOnLightEnabled();
            if (ImGui::Checkbox("直接光へ透過率を適用", &transmittanceOnLight)) {
                atmosphereManager->SetTransmittanceOnLightEnabled(transmittanceOnLight);
            }
        }
    }

    // ===== 月（第2大気ライト。UE の Second Atmosphere Light 相当） =====
    if (ImGui::CollapsingHeader("月", ImGuiTreeNodeFlags_DefaultOpen)) {
        AtmosphereEditorMoonSettings settings = moonSettings_;
        bool changed = false;
        changed |= ImGui::Checkbox("月を有効にする", &settings.enabled);
        if (settings.enabled) {
            changed |= ImGui::SliderFloat("高度角 [deg]##moon", &settings.elevationDeg, -20.0f, 90.0f, "%.1f");
            changed |= ImGui::SliderFloat("方位角 [deg]##moon", &settings.azimuthDeg, -180.0f, 180.0f, "%.1f");
            // 空の輝度スケール（太陽 20 に対し既定 0.02 = 1/1000 の美術値）。
            // 微小値を扱うためログスケール
            changed |= ImGui::SliderFloat("強度（空）##moon", &settings.skyIntensity,
                0.0f, 1.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
            // サーフェス直接光（太陽の 1.75 と同じ単位。既定は太陽の空:直接光比に揃えた 0.002。
            // 盛りすぎると自動露出下で「空だけ暗く床だけ明るい」不整合になる）
            changed |= ImGui::SliderFloat("強度（直接光）##moon", &settings.surfaceIntensity,
                0.0f, 0.1f, "%.4f", ImGuiSliderFlags_Logarithmic);
            float moonColor[3] = { settings.color.x, settings.color.y, settings.color.z };
            if (ImGui::ColorEdit3("月光色", moonColor)) {
                settings.color = { moonColor[0], moonColor[1], moonColor[2] };
                changed = true;
            }

            // 満ち欠け: 既定は常に満月（実位相は太陽の配置次第で意図せず新月になるためオプトイン）
            if (auto* atmosphereManager = GetAtmosphereManager()) {
                auto& params = atmosphereManager->GetParametersMutable();
                ImGui::Checkbox("満ち欠けを太陽と連動（実位相）", &params.moonPhaseFromSun);
            }
        }
        if (changed) {
            ApplyMoonSettings(settings);
        }
    }

    // ===== 空アンビエント（Sky Light 相当） =====
    if (ImGui::CollapsingHeader("環境光", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* atmosphereManager = GetAtmosphereManager()) {
            // Sky-View LUT を SH 射影した環境光。昼は青、夕方はオレンジ、夜はほぼゼロと時刻に追従する。
            // OFF にすると従来のハーフランバートアンビエント（ライト色固定）に戻る
            bool skyAmbient = atmosphereManager->IsSkyAmbientEnabled();
            if (ImGui::Checkbox("空アンビエント（大気連動）", &skyAmbient)) {
                atmosphereManager->SetSkyAmbientEnabled(skyAmbient);
            }
            if (skyAmbient) {
                float scale = atmosphereManager->GetSkyAmbientScale();
                if (ImGui::SliderFloat("環境光スケール", &scale, 0.0f, 1.0f, "%.3f")) {
                    atmosphereManager->SetSkyAmbientScale(scale);
                }
                ImGui::Text("SH生成済み: %s", atmosphereManager->IsSkyAmbientReady() ? "true" : "false");

                // 空＋雲キューブマップによる環境反射（Phase 3b）。
                // モデルの roughness に応じたぼけ付きで空・雲が映り込み、
                // 水面は平面反射に雲が合成される
                bool skySpecular = atmosphereManager->IsSkySpecularEnabled();
                if (ImGui::Checkbox("スペキュラIBL（空・雲の映り込み）", &skySpecular)) {
                    atmosphereManager->SetSkySpecularEnabled(skySpecular);
                }
                if (skySpecular) {
                    ImGui::Text("キューブマップ生成済み: %s",
                        atmosphereManager->IsSkyEnvironmentReady() ? "true" : "false");
                }
            }
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

            ImGui::SeparatorText("多重散乱");
            // 等方 Psi_ms 近似は薄明時に反太陽側を過大に持ち上げる。1 未満で抑制（UE の MultiScatteringFactor 相当）
            paramsChanged |= ImGui::SliderFloat("寄与スケール", &params.multiScatteringFactor, 0.0f, 2.0f, "%.2f");

            ImGui::SeparatorText("空気遠近感（Aerial Perspective）");
            // Camera Volume は 32 スライスなので最大適用距離 = 32 × この値（既定 4 → 128km）
            paramsChanged |= ImGui::SliderFloat("距離スケール [km/slice]", &params.apKmPerSlice, 1.0f, 16.0f, "%.1f");

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

            ImGui::SeparatorText("月ディスク");
            paramsChanged |= ImGui::SliderFloat("視半径 [deg]##moonDisk", &params.moonDiskAngularRadiusDeg, 0.05f, 2.0f, "%.3f");
            paramsChanged |= ImGui::DragFloat("輝度スケール##moonDisk", &params.moonDiskLuminanceScale, 0.1f, 0.0f, 100.0f, "%.1f");

            ImGui::SeparatorText("星空");
            // 手続き的星field（テクスチャ不要）。空の明るさマスクにより昼・薄明では自動的に消えるため
            // 0 にする以外の昼夜切り替え操作は不要
            paramsChanged |= ImGui::SliderFloat("星の強度", &params.starIntensity, 0.0f, 5.0f, "%.2f");

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
            const Vector3& sunTrans = atmosphereManager->GetSunTransmittance();
            ImGui::Text("太陽透過率（地表）: (%.3f, %.3f, %.3f)", sunTrans.x, sunTrans.y, sunTrans.z);
            ImGui::Text("月ライト有効: %s", atmosphereManager->HasMoonLight() ? "true" : "false");
            if (atmosphereManager->HasMoonLight()) {
                const Vector3& moonDir = atmosphereManager->GetMoonDirection();
                ImGui::Text("月ライト方向: (%.3f, %.3f, %.3f)", moonDir.x, moonDir.y, moonDir.z);
                ImGui::Text("月光強度（空）: %.3f", atmosphereManager->GetMoonIntensity());
                const Vector3& moonTrans = atmosphereManager->GetMoonTransmittance();
                ImGui::Text("月透過率（地表）: (%.3f, %.3f, %.3f)", moonTrans.x, moonTrans.y, moonTrans.z);
            }
            ImGui::Text("カメラ高度（地表基準）: %.2f m", atmosphereManager->GetCameraHeightAboveGround());
            ImGui::Text("惑星中心距離: %.1f m", atmosphereManager->GetDistanceFromPlanetCenter());
            ImGui::Text("LUT再計算要求: %s", atmosphereManager->IsLUTDirty() ? "true" : "false");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "AtmosphereManager が見つかりません");
        }
    }
#endif
}

void AtmosphereEditorFacade::DrawSunMoonPlacementWidget()
{
#ifdef USE_IMGUI
    // ===== スカイマップ: 上から見た天球の極座標表示 =====
    // 中心=天頂（高度90°）・内側の円=地平線（0°）・外周=高度-20°（スライダーの下限と一致）。
    // 方位はワールド軸準拠で上=+Z（方位角0°）・右=+X（+90°）。
    const float size = 230.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##skyPlacement", ImVec2(size, size));
    const bool active = ImGui::IsItemActive();

    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    const float rMax = size * 0.5f - 10.0f;              // 高度 -20°
    const float rHorizon = rMax * (90.0f / 110.0f);      // 高度 0°

    // 高度・方位 → スクリーン座標（高度は表示上 [-20,90] にクランプ）
    auto toScreen = [&](float elevationDeg, float azimuthDeg) {
        const float e = std::clamp(elevationDeg, -20.0f, 90.0f);
        const float r = (90.0f - e) / 110.0f * rMax;
        const float a = azimuthDeg * kDegToRad;
        return ImVec2(center.x + r * std::sin(a), center.y - r * std::cos(a));
    };

    // --- 背景 ---
    drawList->AddCircleFilled(center, rMax, IM_COL32(24, 26, 34, 255), 64);      // 地平線下の帯
    drawList->AddCircleFilled(center, rHorizon, IM_COL32(46, 66, 100, 255), 64); // 天球（空）
    for (float e = 30.0f; e < 90.0f; e += 30.0f) {                               // 高度リング
        drawList->AddCircle(center, rMax * (90.0f - e) / 110.0f, IM_COL32(255, 255, 255, 24), 64, 1.0f);
    }
    drawList->AddLine(ImVec2(center.x, center.y - rMax), ImVec2(center.x, center.y + rMax), IM_COL32(255, 255, 255, 24));
    drawList->AddLine(ImVec2(center.x - rMax, center.y), ImVec2(center.x + rMax, center.y), IM_COL32(255, 255, 255, 24));
    drawList->AddCircle(center, rHorizon, IM_COL32(170, 190, 220, 200), 64, 2.0f); // 地平線
    drawList->AddText(ImVec2(center.x - 8.0f, origin.y), IM_COL32(200, 210, 230, 200), "+Z");
    drawList->AddText(ImVec2(origin.x + size - 22.0f, center.y - 8.0f), IM_COL32(200, 210, 230, 200), "+X");

    // --- ドラッグ処理 ---
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto distSq = [](const ImVec2& a, const ImVec2& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    };
    ImVec2 sunPos = toScreen(sunSettings_.elevationDeg, sunSettings_.azimuthDeg);
    ImVec2 moonPos = toScreen(moonSettings_.elevationDeg, moonSettings_.azimuthDeg);

    if (ImGui::IsItemActivated()) {
        // ハンドルの近くを掴んだらそれを、空白を掴んだら太陽をその場へ（直接配置）
        constexpr float kGrabRadiusSq = 16.0f * 16.0f;
        const float sunDist = distSq(mouse, sunPos);
        const float moonDist = moonSettings_.enabled ? distSq(mouse, moonPos) : 1e12f;
        if (moonDist <= kGrabRadiusSq && moonDist < sunDist) {
            placementDrag_ = 2;
        } else {
            placementDrag_ = 1;
        }
    }
    if (!active) {
        placementDrag_ = 0;
    }
    if (active && placementDrag_ != 0) {
        const float dx = mouse.x - center.x;
        const float dy = mouse.y - center.y;
        const float r = std::sqrt(dx * dx + dy * dy);
        float azimuthDeg = std::atan2(dx, -dy) / kDegToRad;                  // 上=0°・右=+90°
        float elevationDeg = std::clamp(90.0f - (r / rMax) * 110.0f, -20.0f, 90.0f);
        if (placementDrag_ == 1) {
            AtmosphereEditorSunSettings s = sunSettings_;
            s.elevationDeg = elevationDeg;
            s.azimuthDeg = azimuthDeg;
            ApplySunSettings(s);
        } else {
            AtmosphereEditorMoonSettings s = moonSettings_;
            s.elevationDeg = elevationDeg;
            s.azimuthDeg = azimuthDeg;
            ApplyMoonSettings(s);
        }
        sunPos = toScreen(sunSettings_.elevationDeg, sunSettings_.azimuthDeg);
        moonPos = toScreen(moonSettings_.elevationDeg, moonSettings_.azimuthDeg);
    }

    // --- ハンドル描画（月→太陽の順。重なったら太陽が上） ---
    if (moonSettings_.enabled) {
        const bool moonUp = moonSettings_.elevationDeg >= 0.0f;
        drawList->AddCircleFilled(moonPos, 9.0f, IM_COL32(170, 190, 230, 60));
        drawList->AddCircleFilled(moonPos, 5.5f,
            moonUp ? IM_COL32(205, 220, 245, 255) : IM_COL32(105, 115, 135, 255));
        drawList->AddCircle(moonPos, 6.0f, IM_COL32(20, 25, 40, 200), 0, 1.5f);
    }
    {
        const bool sunUp = sunSettings_.elevationDeg >= 0.0f;
        drawList->AddCircleFilled(sunPos, 11.0f, IM_COL32(255, 200, 70, 60));
        drawList->AddCircleFilled(sunPos, 6.5f,
            sunUp ? IM_COL32(255, 195, 60, 255) : IM_COL32(160, 100, 45, 255));
        drawList->AddCircle(sunPos, 7.0f, IM_COL32(40, 25, 5, 200), 0, 1.5f);
    }

    // --- 読み取り値（ウィジェット右側） ---
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "● 太陽");
    ImGui::Text("高度 %.1f°", sunSettings_.elevationDeg);
    ImGui::Text("方位 %.1f°", sunSettings_.azimuthDeg);
    ImGui::Spacing();
    if (moonSettings_.enabled) {
        ImGui::TextColored(ImVec4(0.8f, 0.86f, 0.96f, 1.0f), "● 月");
        ImGui::Text("高度 %.1f°", moonSettings_.elevationDeg);
        ImGui::Text("方位 %.1f°", moonSettings_.azimuthDeg);
    } else {
        ImGui::TextDisabled("月: 無効");
    }
    ImGui::Spacing();
    ImGui::TextDisabled("ドラッグで配置\n中心=天頂\n円=地平線\n外周=地平線下");
    ImGui::EndGroup();
#endif
}

void AtmosphereEditorFacade::ApplyTimeOfDay()
{
    // 春分（赤緯0）の太陽軌道: 時角 H = (時刻-12)×15°、緯度 φ。
    // sin(高度) = cosφ·cosH。方位は南基準 A = atan2(sinH, cosH·sinφ) を北（+Z）基準へ変換する。
    // 正午に南（北半球）、6時に東（+X）、18時に西へ沈む現実的な弧になる。
    const float H = (timeOfDay_ - 12.0f) * 15.0f * kDegToRad;
    const float phi = std::clamp(latitudeDeg_, -89.0f, 89.0f) * kDegToRad;

    const float sinElevation = std::cos(phi) * std::cos(H);
    const float elevationDeg = std::asin(std::clamp(sinElevation, -1.0f, 1.0f)) / kDegToRad;
    float azimuthDeg = 180.0f + std::atan2(std::sin(H), std::cos(H) * std::sin(phi)) / kDegToRad;
    if (azimuthDeg > 180.0f) {
        azimuthDeg -= 360.0f;
    }

    AtmosphereEditorSunSettings settings = sunSettings_;
    // 高度はエディタの対応範囲（スカイマップ・スライダーと同じ -20°）で頭打ちにする
    settings.elevationDeg = std::max(elevationDeg, -20.0f);
    settings.azimuthDeg = azimuthDeg;
    ApplySunSettings(settings);
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
