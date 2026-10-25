#pragma once

#include "Graphics/Water/RayTracing/WaterRefractionRayTracingManager.h"
#include "Graphics/Water/RayTracing/WaterCausticsRayTracingManager.h"
#include "Graphics/Render/RenderingTechnique/Lighting/WaterCausticsTechnique.h"
#include "Graphics/Water/FFTOceanManager.h"

namespace CoreEngine { class WaterRenderFeature; }

namespace CoreEngine {
    class EngineSystem;
}

/// @brief DXR 屈折のデバッグ表示（保存しない）
struct WaterEditorRefractionDebug {
    bool available = false;
    float displayScale = 1.0f;
    int viewMode = 0;
    bool logEnabled = false;
};

/// @brief コースティクスのデバッグ表示と診断（保存しない）
struct WaterEditorCausticsDebug {
    bool techniqueAvailable = false;
    bool rtManagerAvailable = false;
    float displayScale = 1.0f;
    int viewMode = 0;
    bool logEnabled = false;
    float rtDisplayScale = 1.0f;
    int rtViewMode = 0;
    bool rtLogEnabled = false;
};

/// @brief 水面のインスペクタと、エンジン側の manager / technique の仲介
/// @details 保存する値は CVar（r.Water.*）が持ち、インスペクタが直接読み書きする。
///          ここで扱うのは、保存しないデバッグ表示と診断だけ。
class WaterEditorFacade {
public:
    /// @brief 参照先を初期化する
    void Initialize(CoreEngine::WaterRenderFeature& runtimeController, CoreEngine::EngineSystem& engine);

    /// @brief FFT の海の manager（無ければ nullptr）
    CoreEngine::FFTOceanManager* GetFFTOceanManager() const;

    /// @brief DXR 屈折のデバッグ表示を取得する
    WaterEditorRefractionDebug GetRefractionDebug() const;

    /// @brief DXR 屈折のデバッグ表示を適用する
    void ApplyRefractionDebug(const WaterEditorRefractionDebug& debug);

    /// @brief コースティクスのデバッグ表示と診断を取得する
    WaterEditorCausticsDebug GetCausticsDebug() const;

    /// @brief コースティクスのデバッグ表示を適用する
    void ApplyCausticsDebug(const WaterEditorCausticsDebug& debug);

private:
    CoreEngine::WaterRefractionRayTracingManager* GetWaterRefractionManager() const;
    CoreEngine::WaterCausticsRayTracingManager* GetWaterCausticsManager() const;
    CoreEngine::WaterCausticsTechnique* GetWaterCausticsTechnique() const;

    CoreEngine::WaterRenderFeature* runtimeController_ = nullptr;
    CoreEngine::EngineSystem* engine_ = nullptr;
};
