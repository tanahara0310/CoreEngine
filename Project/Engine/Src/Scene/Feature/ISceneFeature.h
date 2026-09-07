#pragma once

#include <cstdint>

namespace CoreEngine
{
    class EngineSystem;
    class GameObjectManager;
    class CameraManager;
    class SceneManager;
    class SceneSaveSystem;
    class Camera;

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

    /// @brief フェーズ内で最初に回したい Feature 用の priority
    /// @details 既定 Feature はすべて priority 0（＝登録順）で回るため、この値を使うと
    ///          同フェーズの他の Feature より確実に先になる。
    ///          「他の Feature が前提にする状態を先に確定させる」用途に使うこと
    ///          （カメラ姿勢の更新など）。
    inline constexpr int kEarlyFeaturePriority = -1000;

    /// @brief フェーズ内で最後に回したい Feature 用の priority
    /// @details 既定 Feature はすべて priority 0（＝登録順）で回るため、この値を使うと
    ///          同フェーズの他の Feature より確実に後になる。
    ///          「そのフェーズの全処理が終わってから 1 回だけ動かす」用途に使うこと
    ///          （トゥイーンの前進・イベントの一括配信など）。
    inline constexpr int kLateFeaturePriority = 1000;

    /// @brief Feature へ渡すシーン側コンテキスト
    /// @details BaseScene が所有し、各ディスパッチ直前に gameViewCamera3D を再解決する
    ///          （カメラオーバーライドがフレーム中に切り替わっても最新を参照させるため）。
    struct SceneContext {
        EngineSystem* engine = nullptr;
        GameObjectManager* gameObjectManager = nullptr;
        CameraManager* cameraManager = nullptr;
        SceneManager* sceneManager = nullptr;
        SceneSaveSystem* saveSystem = nullptr;
        Camera* gameViewCamera3D = nullptr;
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

        /// @brief OnInitialize() 完了後・シーン JSON からの復元より前
        /// @details シーンが生成したオブジェクトを見て挙動を決める Feature 用
        ///          （SkyBox / 無限床の採用判定など）。
        virtual void PostSceneInitialize(SceneContext&) {}

        /// @brief 毎フレーム更新（フェーズごとに 1 回ずつ呼ばれる）
        virtual void Update(SceneContext&, SceneUpdatePhase) {}

        /// @brief メニューバーの停止ボタンで止めている間も Update を回すか
        /// @details 既定は false。衝突判定・トゥイーン・イベント配信・カメラ演出など、
        ///          ゲームの進行そのものである Feature はそのまま止める。
        /// @note エディタから触るもの（カメラ・ライト・グリッド・ギズモ・床・大気）は
        ///       true を返すこと。止めると停止中にパラメータを変えても画面が変わらず、
        ///       「編集はできるが更新は進まない」という停止の意味が壊れる。
        virtual bool RunsWhileStopped() const { return false; }

        /// @brief シーン終了時（登録の逆順で呼ばれる）
        /// @note この時点ではシーンの GameObject はまだ生きている。
        virtual void Finalize(SceneContext&) {}

        /// @brief シーンの GameObject が全て破棄された後（登録の逆順で呼ばれる）
        /// @details Finalize() の対になるフックで、PostSceneInitialize() と対称の位置にある。
        ///          「オブジェクトの破棄で解除されるはずの登録」を取りこぼしなく畳むための場所。
        ///          購読やトゥイーンの後始末は Finalize() ではなくここで行うこと
        ///          （Finalize() の時点で畳むと、その後に破棄される GameObject の
        ///          解除処理が空振りして順序が逆転する）。
        virtual void PostSceneFinalize(SceneContext&) {}
    };
}
