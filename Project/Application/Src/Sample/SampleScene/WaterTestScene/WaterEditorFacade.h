#pragma once

#include "Graphics/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Water/FFTOceanManager.h"

class WaterSurfaceRuntimeController;

namespace CoreEngine {
    class EngineSystem;
}

/// @brief Water UI から扱う FFT Ocean 状態の読み書きモデル
struct WaterEditorFFTSettings {
    bool enabled = true;
    bool managerAvailable = false;
    bool initialized = false;
    int resolution = 256;
    float patchLength = 96.0f;
    float amplitudeScale = 1.0f;
    float windDirection[2] = { 0.92f, 0.38f };
    float windSpeed = 24.0f;
    float choppiness = 1.35f;
    int activeComponentCount = 32;
    float gravity = 9.81f;
};

/// @brief Water UI から扱う DXR 屈折設定
struct WaterEditorRayTracingSettings {
    float maxRefractionOffsetPixels = 3.0f;
};

/// @brief Water UI から扱うコースティクス設定
struct WaterEditorCausticsSettings {
    bool techniqueAvailable = false;
    float intensity = 0.35f;
    float depthAttenuation = 1.5f;
    float curvatureScale = 1.5f;
    float surfaceSampleRadius = 0.35f;
    float refractiveIndex = 1.333f;
    float receiverNormalStrength = 1.0f;
    float alignmentPower = 6.0f;
    float debugDisplayScale = 1.0f;
    int debugViewMode = 0;
    bool debugLogEnabled = false;
};

/// @brief Water UI から扱うコースティクス診断情報
struct WaterEditorCausticsDiagnostics {
    bool techniqueAvailable = false;
    uint32_t activeWaveCount = 0;
    bool mainLightEnabled = false;
    uint64_t outputHandle = 0;
    uint64_t worldPositionHandle = 0;
    uint64_t normalHandle = 0;
};

/// @brief Water UI と Engine 内部 manager / technique の仲介を担当する facade
/// @details UI はこの facade だけを通して Water 関連設定を取得・適用する。
class WaterEditorFacade {
public:
    /// @brief facade の参照先を初期化する
    void Initialize(WaterSurfaceRuntimeController& runtimeController, CoreEngine::EngineSystem& engine);

    /// @brief 現在の FFT Ocean 設定を取得する
    WaterEditorFFTSettings GetFFTSettings() const;

    /// @brief FFT Ocean 設定を適用する
    void ApplyFFTSettings(const WaterEditorFFTSettings& settings);

    /// @brief FFT Ocean 設定を既定値へ戻し、その結果を返す
    WaterEditorFFTSettings ResetFFTSettings();

    /// @brief DXR 屈折設定を取得する
    WaterEditorRayTracingSettings GetRayTracingSettings() const;

    /// @brief DXR 屈折設定を適用する
    void ApplyRayTracingSettings(const WaterEditorRayTracingSettings& settings);

    /// @brief コースティクス設定を取得する
    WaterEditorCausticsSettings GetCausticsSettings() const;

    /// @brief コースティクス設定を適用する
    void ApplyCausticsSettings(const WaterEditorCausticsSettings& settings);

    /// @brief コースティクス診断情報を取得する
    WaterEditorCausticsDiagnostics GetCausticsDiagnostics() const;

private:
    CoreEngine::FFTOceanManager* GetFFTOceanManager() const;
    CoreEngine::WaterRefractionRayTracingManager* GetWaterRefractionManager() const;
    CoreEngine::WaterCausticsTechnique* GetWaterCausticsTechnique() const;

    WaterSurfaceRuntimeController* runtimeController_ = nullptr;
    CoreEngine::EngineSystem* engine_ = nullptr;
};
