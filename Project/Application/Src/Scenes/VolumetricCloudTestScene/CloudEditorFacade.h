#pragma once

namespace CoreEngine {
    class EngineSystem;
    class VolumetricCloudManager;
}

/// @brief Cloud UI と Engine 内部 VolumetricCloudManager の仲介を担当する facade
/// @details UI はこの facade だけを通してボリューメトリック雲関連の設定を取得・適用する。
///          Initialize で GameDebugUI の環境エディタ（Hierarchy の Environment ツリー）へ
///          自己登録し、選択時に Inspector 内へ編集パネルを描画する。
///          （AtmosphereTestScene の AtmosphereEditorFacade と同じ役割分担）
class CloudEditorFacade {
public:
    /// @brief 環境エディタの登録を解除する
    ~CloudEditorFacade();

    /// @brief facade の参照先を初期化し、環境エディタとして登録する
    void Initialize(CoreEngine::EngineSystem& engine);

private:
    /// @brief ボリューメトリック雲の編集パネル内容を描画する（Inspector 内に埋め込み）
    void DrawContent();

    CoreEngine::VolumetricCloudManager* GetVolumetricCloudManager() const;

    CoreEngine::EngineSystem* engine_ = nullptr;
};
