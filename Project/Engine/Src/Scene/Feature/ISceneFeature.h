#pragma once

#include <cstdint>

namespace CoreEngine
{
    class EngineSystem;
    class GameObjectManager;
    class CameraManager;
    class SceneManager;
    class SceneSaveSystem;
    class ICamera;

    /// @brief BaseScene::Update 内の Feature ディスパッチ位置
    /// @details 従来 BaseScene::Update に暗黙の順序として埋まっていた更新タイミングを
    ///          明示化した論理フック。同一フェーズ内は priority（小さいほど先）、
    ///          同 priority は登録順（RenderPassPhase と同じ規約）。
    enum class SceneUpdatePhase : uint32_t {
        FrameStart = 0,   ///< カメラ更新後・OnUpdate() 前（ライト/影・エディタ等のフレーム前処理）
        PreObjectUpdate,  ///< OnUpdate() 後・GameObject 更新前（床のカメラ追従など位置の事前確定）
        PostObjectUpdate, ///< GameObject 更新後・OnLateUpdate() 前（コリジョンなど結果の収集・判定）
        PostLogic,        ///< OnLateUpdate() 後（大気→雲など全ロジック確定後の反映）
    };

    /// @brief Feature へ渡すシーン側コンテキスト
    /// @details BaseScene が所有し、各ディスパッチ直前に gameViewCamera3D を再解決する
    ///          （カメラオーバーライドがフレーム中に切り替わっても最新を参照させるため）。
    struct SceneContext {
        EngineSystem* engine = nullptr;
        GameObjectManager* gameObjectManager = nullptr;
        CameraManager* cameraManager = nullptr;
        SceneManager* sceneManager = nullptr;
        SceneSaveSystem* saveSystem = nullptr;
        ICamera* gameViewCamera3D = nullptr;
    };

    /// @brief シーン横断機能（ライト・コリジョン・環境等）の基底クラス
    /// @details BaseScene::AddFeature() で登録すると、シーンのライフサイクルに合わせて
    ///          各フックが呼ばれる。エンジン機能の追加は Feature の追加のみで完結し、
    ///          BaseScene 本体の編集を不要にする（RenderPipeline::AddPass と同じ思想）。
    class ISceneFeature {
    public:
        virtual ~ISceneFeature() = default;

        /// @brief Feature 名（ログ・デバッグ用）
        virtual const char* GetName() const = 0;

        /// @brief シーン初期化時（派生シーンの OnInitialize() より前）
        virtual void Initialize(SceneContext&) {}

        /// @brief OnInitialize() 完了後・LoadObjectsFromJson() 前
        /// @details シーンが生成したオブジェクトを見て挙動を決める Feature 用
        ///          （SkyBox / 無限床の採用判定など）。
        virtual void PostSceneInitialize(SceneContext&) {}

        /// @brief 毎フレーム更新（フェーズごとに 1 回ずつ呼ばれる）
        virtual void Update(SceneContext&, SceneUpdatePhase) {}

        /// @brief シーン終了時（登録の逆順で呼ばれる）
        virtual void Finalize(SceneContext&) {}
    };
}
