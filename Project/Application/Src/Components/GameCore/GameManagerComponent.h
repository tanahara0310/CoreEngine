#pragma once

#include "GameObject/Component/Core/IComponent.h"

namespace CoreEngine {
    class SceneManager;
}

namespace GameComponents
{
    class TrainMovementComponent;
    class RailBuilderComponent;
    class HungerComponent;

    // ゲームの進行を管理するコンポーネント
    class GameManagerComponent final
        : public CoreEngine::IComponent {
    public:
        enum class Phase { Playing, Ending, Transitioning };

        explicit GameManagerComponent(CoreEngine::SceneManager* sceneManager = nullptr)
            : sceneManager_(sceneManager) {}
        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "GameManager";
        }
        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "ゲーム進行"; }
        bool DrawInspector() override;
#endif
        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;
        // 全オブジェクトの移動後にゲーム進行を更新する
        void LateUpdate() override;

        // ゲームオーバーを要求する。時間は終了演出後の待機秒数。
        void RequestGameOver(float changeDelayTime = -1.0f);
        // ゲームクリアを要求する。時間はカメラ演出完了後の待機秒数。
        void RequestGameClear(float changeDelayTime = -1.0f);

        // GameScene の構築時に接続する。描画・カメラ更新は終了演出中も止めない。
        void SetGameplayComponents(
            TrainMovementComponent* train,
            RailBuilderComponent* builder,
            HungerComponent* hunger = nullptr);

        Phase GetPhase() const { return phase_; }
        bool IsGameOver() const { return isGameOver_; }
        bool IsGameClear() const { return isGameClear_; }

    private:
        // 終了通知の入口。カメラ演出開始もここから指示する。
        void BeginEnding(bool isClear, float changeDelayTime);
        // 終了演出の待機とシーン遷移を分離する。
        void UpdateEnding();
        void TransitionToResult();

        CoreEngine::SceneManager* sceneManager_ = nullptr;
        TrainMovementComponent* train_ = nullptr;
        RailBuilderComponent* builder_ = nullptr;
        HungerComponent* hunger_ = nullptr;
        Phase phase_ = Phase::Playing;
        bool isGameClear_ = false;
        bool isGameOver_ = false;

        float changeDelayTimer_ = 0.0f;
        float defaultChangeDelay_ = 1.0f;

        // 列車へ寄るカメラリグへ繋ぎ終えるまでの残り秒数
        float closeUpTimer_ = 0.0f;
    };
}
