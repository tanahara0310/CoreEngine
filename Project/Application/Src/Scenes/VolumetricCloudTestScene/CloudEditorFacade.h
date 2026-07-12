#pragma once

namespace CoreEngine {
    class EngineSystem;
    class VolumetricCloudManager;
}

/// @brief Cloud UI と Engine 内部 VolumetricCloudManager の仲介を担当する facade
/// @details UI はこの facade だけを通してボリューメトリック雲関連の設定を取得・適用する。
///          （AtmosphereTestScene の AtmosphereEditorFacade と同じ役割分担）
class CloudEditorFacade {
public:
    /// @brief facade の参照先を初期化する
    void Initialize(CoreEngine::EngineSystem& engine);

    /// @brief ボリューメトリック雲の ImGui 編集パネルを描画する（USE_IMGUI 時のみ動作）
    void DrawImGui();

private:
    CoreEngine::VolumetricCloudManager* GetVolumetricCloudManager() const;

    CoreEngine::EngineSystem* engine_ = nullptr;
};
