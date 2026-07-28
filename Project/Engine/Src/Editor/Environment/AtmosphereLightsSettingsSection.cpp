#include "pch.h"
#include "AtmosphereLightsSettingsSection.h"

#include "EngineSystem/EngineSystem.h"
#include "Graphics/Light/LightManager.h"
#include "Utility/JsonManager/JsonManager.h"

namespace CoreEngine
{
    void AtmosphereLightsSettingsSection::Serialize(json& out) const
    {
        auto* lightManager = engine_ ? engine_->GetComponent<LightManager>() : nullptr;
        if (!lightManager) {
            return;
        }

        if (const Light* sun = lightManager->GetAtmosphereSunLight()) {
            out["sunDirection"] = JsonManager::Vector3ToJson(sun->direction);
            out["sunAtmosphereIntensity"] = sun->atmosphereIntensity;
        }

        const Light* moon = lightManager->GetAtmosphereMoonLight();
        out["moonEnabled"] = moon ? moon->enabled : false;
        if (moon) {
            out["moonDirection"] = JsonManager::Vector3ToJson(moon->direction);
            out["moonColor"] = JsonManager::Vector3ToJson(moon->color);
            out["moonSurfaceIntensity"] = moon->intensity;
            out["moonAtmosphereIntensity"] = moon->atmosphereIntensity;
        }
    }

    void AtmosphereLightsSettingsSection::Deserialize(const json& in)
    {
        auto* lightManager = engine_ ? engine_->GetComponent<LightManager>() : nullptr;
        if (!lightManager) {
            return;
        }

        // ゼロベクトル等の不正値を弾いて正規化するヘルパー
        const auto safeDirection = [](const Vector3& dir, const Vector3& fallback) {
            const float lengthSq = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
            if (lengthSq < 1e-8f) {
                return fallback;
            }
            return MathCore::Vector::Normalize(dir);
        };

        // ===== 太陽（LightingFeature の既定ライトへのフォールバック込みで取得） =====
        if (Light* sun = lightManager->GetAtmosphereSunLight()) {
            sun->direction = safeDirection(
                JsonManager::SafeGetVector3(in, "sunDirection", sun->direction), sun->direction);
            sun->atmosphereIntensity =
                JsonManager::SafeGet(in, "sunAtmosphereIntensity", sun->atmosphereIntensity);
            // 変化は AtmosphereManager::Update() が自動検知して Sky-View LUT を再生成する
        }

        // ===== 月（オプトイン。有効で保存されていればライトを生成して復元） =====
        const bool moonEnabled = JsonManager::SafeGet(in, "moonEnabled", false);
        Light* moon = lightManager->GetAtmosphereMoonLight();
        if (!moon && moonEnabled) {
            // AtmosphereEditor::ApplyMoonSettings と同じ手順で第2ディレクショナルライトを生成する
            LightHandle moonHandle = lightManager->CreateLight(LightType::Directional, "Moon");
            moon = lightManager->GetLight(moonHandle);
            if (moon) {
                moon->isAtmosphereMoon = true;
            }
        }
        if (moon) {
            moon->enabled = moonEnabled;
            moon->direction = safeDirection(
                JsonManager::SafeGetVector3(in, "moonDirection", moon->direction), moon->direction);
            moon->color = JsonManager::SafeGetVector3(in, "moonColor", moon->color);
            moon->intensity = JsonManager::SafeGet(in, "moonSurfaceIntensity", moon->intensity);
            moon->atmosphereIntensity =
                JsonManager::SafeGet(in, "moonAtmosphereIntensity", moon->atmosphereIntensity);
        }
    }
}
