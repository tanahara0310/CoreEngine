#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace CoreEngine
{
    class ParticleSystem;
    class TransformComponent;
}

namespace GameComponents {
    class RailPathComponent;
    class GameManagerComponent;
    class HungerComponent;
}

namespace GameComponents
{
    // レールの上を移動するコンポーネント,レールパスが必要
    class TrainMovementComponent final
        : public CoreEngine::IComponent {
    public:
        explicit TrainMovementComponent(
            float gridSize = 5.0f, float moveSpeed = 0.5f,
            int32_t gridX = 0, int32_t gridZ = 0,
            GameComponents::RailPathComponent* railPath = nullptr,
            GameManagerComponent* gameManager = nullptr,
            HungerComponent* hunger = nullptr)
            : railPath_(railPath), gameManager_(gameManager), hunger_(hunger), gridSize_(gridSize),
              initialMoveSpeed_(moveSpeed), moveSpeed_(moveSpeed),
              initialGridX_(gridX), initialGridZ_(gridZ), gridX_(gridX), gridZ_(gridZ) {
        }

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "TrainMovement";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "列車移動"; }
        bool DrawInspector() override;
#endif

        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;
        // 毎フレーム呼ばれる
        void Update() override;

        // 発車後に進めるレールがなくなったか
        bool IsGameOver() const;

        float GetMoveSpeed() const { return moveSpeed_; }
        CoreEngine::Vector3 GetWorldPosition() const;
        // サルの番号から、そのサルが乗っている車両の Transform を引く。
        // 0 は先頭車両で、1 以降は連結した順。まだ連結していない番号なら nullptr。
        // 車両の上へ何かを置く演出のための読み取り口で、値は書き換えないこと。
        const CoreEngine::TransformComponent* GetMonkeyTransform(std::size_t monkeyIndex) const;
        // 指定位置に重なる先頭・後続車両の上へ矢印を置くための高さ補正。
        float GetCursorHeightOffsetAt(float worldX, float worldZ) const;
        uint32_t GetHorizontalProgressBlocks() const { return horizontalProgressBlocks_; }
        float GetMinMoveSpeed() const { return minMoveSpeed_; }
        float GetSpeedRatio() const {
            return initialMoveSpeed_ > 0.0f ? moveSpeed_ / initialMoveSpeed_ : 1.0f;
        }

        // グリッドサイズを設定する
        void SetGridSize(float size);
        // 最後尾に車両を連結する。車両は親を持たず、通過済みレールを1マス間隔で追従する。
        // scaleMultiplier を渡すと、毎フレームその値を掛けたスケールで描く（出現演出用）。
        // 参照先はこの列車より長く生きること。
        void AddCarriage(CoreEngine::TransformComponent* carriageTransform,
            const CoreEngine::Vector3* scaleMultiplier = nullptr);
        // ゲームオーバー時に車両から切り離して飛ばすサルを登録する。
        // launchTrail はシーン初期化時に作っておいた飛行中の軌跡用パーティクル。
        void AddMonkey(CoreEngine::TransformComponent* monkeyTransform,
            CoreEngine::ParticleSystem* launchTrail = nullptr);
        // 岩破壊の投石中だけ列車の移動を停止・再開する
        void SetRockBreakPaused(bool paused) { isPausedForRockBreak_ = paused; }
        // 投石開始時に、その場でのジャンプを再生する
        void PlayRockThrowJump();
        // ゲームオーバー時に、列車に乗っているサルだけを最終レール方向へ飛ばす
        void PlayGameOverLaunch();

    private:
        // 終端検知を一か所に集め、終了処理は GameManager に委譲する。
        void NotifyGameOver();
        // 未確定レールを次の目的地として保存し、そのレールを確定する
        bool BeginNextSegment();
        // 現在の移動進捗を Transform に反映する
        void SyncTransformToProgress();
        void SyncCarriageTransforms();
        // マス中央への到着を記録し、最後尾が駅の次のマス中央へ着いたら連結する。
        void ProcessCarriageArrival(bool stationActivated);
        void UpdateRockThrowJump(float deltaTime);
        float GetRockThrowJumpOffset() const;
        // 移動方向に合わせて Y 軸回転を更新する
        void UpdateRotation();
        // 曲がり角を中心に、前後のマスへまたがって向きを補間した Y 軸回転を求める。
        // entryTurnProgress は、このマスに入った時点で直前の曲がりがどこまで進んでいたか。
        float EvaluateTurnYaw(float previousYaw, float headingYaw, float nextYaw,
            float progress, float entryTurnProgress) const;

        CoreEngine::TransformComponent* transform_ = nullptr;
        GameComponents::RailPathComponent* railPath_ = nullptr;
        GameManagerComponent* gameManager_ = nullptr;
        HungerComponent* hunger_ = nullptr;
        float gridSize_ = 5.0f;

        float initialMoveSpeed_ = 0.5f; // 基準最低速度（初期速度・駅到着時のリセット速度）
        float moveSpeed_ = 0.5f; // 移動速度（グリッド単位/秒）
        int32_t initialGridX_ = 0;
        int32_t initialGridZ_ = 0;
        int32_t gridX_ = 0; // 現在のグリッドX座標
        int32_t gridZ_ = 0; // 現在のグリッドZ座標
        int32_t destinationGridX_ = 0;
        int32_t destinationGridZ_ = 0;

        float movementProgress_ = 0.0f;
        // 曲がり角の前後で向きを補間するための進行方向（ラジアン）
        float previousHeadingYaw_ = 0.0f; // 直前のマスで向いていた方向
        float headingYaw_ = 0.0f; // 現在走っているマスの方向
        float nextHeadingYaw_ = 0.0f; // 先読みした次のマスの方向
        // マスに入った時点で直前の曲がりが進んでいた割合。角の手前で曲がり始めていれば 0.5
        float entryTurnProgress_ = 0.0f;
        std::vector<CoreEngine::TransformComponent*> carriageTransforms_;
        // carriageTransforms_ と同じ添字。null なら等倍で描く
        std::vector<const CoreEngine::Vector3*> carriageScaleMultipliers_;
        std::vector<CoreEngine::TransformComponent*> monkeyTransforms_;
        // monkeyTransforms_ と同じ添字。null なら軌跡を出さずに飛ばす。
        std::vector<CoreEngine::ParticleSystem*> monkeyLaunchTrails_;
        std::deque<std::pair<int32_t, int32_t>> traveledCells_;
        std::deque<std::size_t> pendingStationSteps_;
        std::size_t traveledBlockCount_ = 0;
        uint32_t horizontalProgressBlocks_ = 0;
        float rockThrowJumpHeight_ = 0.6f;
        float rockThrowJumpDuration_ = 0.35f;
        float rockThrowJumpElapsed_ = 0.0f;
        bool isMoving_ = false;
        bool hasHeading_ = false; // 発車直後の1マス目は直前の向きがないため補間しない
        bool isRockThrowJumping_ = false;
        bool gameOverLaunchStarted_ = false;
        bool hasStarted_ = false;
        bool isGameOver_ = false;
        bool isPausedForRockBreak_ = false;

        float minMoveSpeed_ = 0.5f; // 線路長から計算された現在の最低移動速度
        float minimumSpeedIncreasePerRail_ = 0.05f; // レール1マスあたりの最低速度増加量
        float minimumSpeedMonkeyBonusRate_ = 0.05f; // サル1匹追加ごとの最低速度補正率
        float acceleration_ = 0.5f; // 毎秒の加速度（速度/秒）
        float accelerationMonkeyBonusRate_ = 0.05f; // サル1匹追加ごとの加速度増加率
        float maximumMoveSpeed_ = 8.0f;
        float turnBlendRatio_ = 0.35f; // 曲がり角の前後で向きを補間する幅（マス比、0～0.5）
        std::size_t requiredRailCount_ = 5;
    };
}
