#pragma once

#include "GameObject/Component/Core/IComponent.h"
#include "Math/Vector/Vector3.h"

#include <cstdint>
#include <deque>
#include <functional>

namespace CoreEngine
{
    class Camera;
    class TransformComponent;
}

namespace GameComponents
{
    class RailPathComponent;
    class MapGeneratorComponent;
    class TrainMovementComponent;
    class HungerComponent;
    class RockThrowComponent;
}   

namespace GameComponents
{
    // 入力に応じてグリッド単位で移動するコンポーネント
    class RailBuilderComponent final
        : public CoreEngine::IComponent {
    public:
        explicit RailBuilderComponent(
            float gridSize = 5.0f, int32_t gridPosX = 0,int32_t gridPosZ = 0,
            GameComponents::RailPathComponent* railPath = nullptr,
            GameComponents::MapGeneratorComponent* mapGenerator = nullptr,
            GameComponents::TrainMovementComponent* trainMovement = nullptr,
            GameComponents::HungerComponent* hunger = nullptr,
            GameComponents::RockThrowComponent* rockThrow = nullptr,
            std::function<void()> OnBuildSE = nullptr,
            std::function<void()> OnUndoSE = nullptr,
            std::function<void()> OnFailureSE = nullptr)
            : gridSize_(gridSize), initialGridPosX_(gridPosX), initialGridPosZ_(gridPosZ),
              gridPosX_(gridPosX), gridPosZ_(gridPosZ),
              railPath_(railPath),
              mapGenerator_(mapGenerator), trainMovement_(trainMovement),
              hunger_(hunger), rockThrow_(rockThrow),
              OnBuildSE_(OnBuildSE), OnUndoSE_(OnUndoSE), OnFailureSE_(OnFailureSE) {
        }

        // コンポーネントを識別する名前。必須
        const char* GetTypeName() const override {
            return "RailBuilder";
        }

        json OnSerialize() const override;
        void OnDeserialize(const json& j) override;

#ifdef USE_IMGUI
        const char* GetInspectorName() const override { return "レールビルダー"; }
        bool DrawInspector() override;
#endif

        // 最初の更新直前に一度だけ呼ばれる
        void Start() override;
        // 毎フレーム呼ばれる
        void Update() override;
        // 全車両の移動後に、矢印が重ならない高さへ更新する。
        void LateUpdate() override;

        // グリッドサイズを設定する
        void SetGridSize(float size);
        // 水平方向優先かどうかを設定する
        void SetHorizontalPrioritize(bool prioritize);
        // 画面外への移動を止めるために覗くカメラ（ゲーム視点）を渡す。
        // 渡さなければ制限は掛からず、従来どおりどこまでも先へ進める。
        void SetViewCamera(CoreEngine::Camera* camera);
        void SetInsufficientFeedback(std::function<void()> onStaminaInsufficient);

        /// @brief 進行方向へ 1 マス敷いた場合のスタミナ消費量を返す（スタミナゲージの予告表示用）
        /// @details 向きは入力が来るまで決まらないため、優先方向（HorizontalPrioritize）の
        ///          1 マス先を見る。橋や岩のマスでは通常のレールより高い値になる。
        /// @return 消費量。参照先が未設定の場合は 0
        float GetNextPlacementCost() const;

    private:
        // 論理グリッド座標を Transform のワールド座標へ反映する
        void SyncTransformToGrid();
        /// @brief そのワールド座標が、余白のぶん内側まで画面に映っているか
        /// @details カメラを渡されていなければ常に true（制限しない）。
        bool IsInsideScreen(const CoreEngine::Vector3& worldPosition) const;
        /// @brief そのマスへカーソルを動かしても画面に映ったままか
        bool IsCellInsideScreen(int32_t gridX, int32_t gridZ) const;
        // 最後に置いたレールを撤去して、消費したレールを回収する
        bool TryUndoLastRail();
        // キュー先頭の岩へ投石を開始する
        void StartNextRockThrow();
        // 投石の着弾時に岩を地面へ変え、カーソルを通常位置へ戻す
        void CompleteRockBreak();
        void NotifyStaminaInsufficient();

        struct RockBreakRequest {
            int32_t gridX = 0;
            int32_t gridZ = 0;
        };

        CoreEngine::TransformComponent* transform_ = nullptr;
        GameComponents::RailPathComponent* railPath_ = nullptr;
        GameComponents::MapGeneratorComponent* mapGenerator_ = nullptr;
        GameComponents::TrainMovementComponent* trainMovement_ = nullptr;
        GameComponents::HungerComponent* hunger_ = nullptr;
        GameComponents::RockThrowComponent* rockThrow_ = nullptr;
        // 画面外への移動を止める判定に使うゲーム視点カメラ（非所有）
        CoreEngine::Camera* viewCamera_ = nullptr;

        // 左・後ろ方向へ移動したときの符号なし整数アンダーフローを避ける
        int32_t initialGridPosX_ = 0;
        int32_t initialGridPosZ_ = 0;
        int32_t gridPosX_ = 0;
        int32_t gridPosZ_ = 0;

        float gridSize_ = 5.0f;
        bool HorizontalPrioritize = true;

        float undoPushTimer_ = 0.0f;
        float undoPushMaxTime_ = 0.3f;
        float undoInterval_ = 0.05f;
        float undoIntervalTimer_ = 0.0f;

        float buildPushTimer_ = 0.0f;
        float buildPushMaxTime_ = 0.3f;
        float buildInterval_ = 0.05f;
        float buildIntervalTimer_ = 0.0f;

        float timer_ = 0.0f;
        float height_ = 1.0f;
        float pulseBaseScale_ = 0.8f;
        float pulseAmplitude_ = 0.2f;
        float pulseSpeed_ = 5.0f;
        float rotationSpeed_ = 2.0f;
        float rockCursorHeightOffset_ = 1.0f;
        float rockThrowStartHeight_ = 0.5f;
        float rockImpactHeight_ = 0.7f;

        // ===== 画面端で止める位置 =====
        // 判定するのはマスの中心なので、カーソルの見た目の大きさぶんだけ内側で止める。
        // 1マス（gridSize）に対する半径の割合で、0.5 なら矢印の外形が画面の端に
        // ちょうど触れる位置まで進める。0 にすると中心が端に来るまで進めるので、
        // 矢印は半分はみ出す。大きくすると手前で止まる。
        //
        // 画面上での大きさはカメラとの距離で変わるので、割合ではなくその場で測る
        // （IsInsideScreen を見ること）。カメラが引けば止まる位置も自動で端へ寄る。
        float cursorEdgeRadiusRatio_ = 0.5f;
        bool isBreakingRock_ = false;
        bool isCursorAboveRock_ = false;
        // 画面端で止めた直後かどうか。長押し中にログを流し続けないための印
        bool isScreenLimited_ = false;
        std::deque<RockBreakRequest> rockBreakQueue_;

        std::function<void()> OnBuildSE_ = nullptr;
        std::function<void()> OnUndoSE_ = nullptr;
        std::function<void()> OnFailureSE_ = nullptr;
        std::function<void()> OnStaminaInsufficient_ = nullptr;
    };
}
