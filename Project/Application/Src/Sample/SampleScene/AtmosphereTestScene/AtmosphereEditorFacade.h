#pragma once

#include "Math/MathCore.h"

namespace CoreEngine {
    class EngineSystem;
    class AtmosphereManager;
    class LightManager;
}

/// @brief Atmosphere UI から扱う太陽設定の読み書きモデル
struct AtmosphereEditorSunSettings {
    float elevationDeg = 30.0f; ///< 太陽高度角 [deg]（0=地平線, 90=天頂, 負値=地平線下）
    float azimuthDeg = 0.0f;    ///< 太陽方位角 [deg]（0=+Z方向, 時計回り）
    float intensity = 20.0f;    ///< 太陽光強度（大気散乱の輝度スケール。空の明るさに直結）
};

/// @brief Atmosphere UI と Engine 内部 AtmosphereManager / LightManager の仲介を担当する facade
/// @details UI はこの facade だけを通して大気散乱関連の設定を取得・適用する。
///          （WaterTestScene の WaterEditorFacade と同じ役割分担）
class AtmosphereEditorFacade {
public:
    /// @brief facade の参照先を初期化する
    void Initialize(CoreEngine::EngineSystem& engine);

    /// @brief 現在の太陽設定を取得する
    AtmosphereEditorSunSettings GetSunSettings() const { return sunSettings_; }

    /// @brief 太陽設定を適用する（太陽ライトの方向・強度へ反映し、LUT再計算を要求）
    void ApplySunSettings(const AtmosphereEditorSunSettings& settings);

    /// @brief 大気散乱の ImGui 編集パネルを描画する（USE_IMGUI 時のみ動作）
    void DrawImGui();

    /// @brief 高度角・方位角から太陽光の進行方向ベクトルを計算する
    /// @return 正規化済みのライト方向（太陽から地表へ向かう方向）
    static CoreEngine::Vector3 ComputeSunLightDirection(float elevationDeg, float azimuthDeg);

private:
    CoreEngine::AtmosphereManager* GetAtmosphereManager() const;
    CoreEngine::LightManager* GetLightManager() const;

    AtmosphereEditorSunSettings sunSettings_{};
    CoreEngine::EngineSystem* engine_ = nullptr;

    // 太陽の自動旋回（昼夜遷移の確認用）
    bool autoSunCycle_ = false;
    float sunCycleSpeedDegPerSec_ = 5.0f;
};
