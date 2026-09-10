#include "pch.h"
#include "TrainMovementComponent.h"

#include "Audio/AudioSystem.h"
#include "Camera/Shake/CameraShake.h"
#include "Camera/Shake/CameraShakePresets.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Math/Easing/EasingUtil.h"
#include "Components/Rail/RailPathComponent.h"
#include "Components/GameCore/GameManagerComponent.h"
#include "Components/GameCore/HungerComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "Particle/ParticleSystem.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Tween/Tween.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

namespace {
    // 曲がり角の緩急。始めと終わりが緩やかで、角の真上で最も速く回る。
    constexpr EasingUtil::Type kTurnEasing = EasingUtil::Type::EaseInOutSine;
    // 進行方向は 90 度刻みなので、一致判定はこの程度の誤差で足りる。
    constexpr float kYawEpsilon = 1e-4f;

    // ゲームオーバー時にサルを最終レールの先へ飛ばす時間と距離。
    constexpr float kGameOverLaunchRiseDuration = 0.26f;
    constexpr float kGameOverLaunchFlightDuration = 0.78f;
    constexpr float kGameOverLaunchDistance = 8.0f;
    constexpr float kGameOverLaunchRiseHeight = 5.5f;
    constexpr float kGameOverLaunchSpin = 5.5f;

    // サルが飛び出す瞬間だけトロッコを傾け、すぐ元の姿勢へ戻す。
    constexpr float kGameOverTrolleyTiltInDuration = 0.10f;
    constexpr float kGameOverTrolleyTiltOutDuration = 0.36f;
    constexpr float kGameOverTrolleyTiltX = 0.30f;
    constexpr float kGameOverTrolleyTiltZ = 0.46f;

    constexpr const char* kGameOverSePath =
        "Application/Assets/Sounds/SE/gameover.mp3";
    constexpr const char* kGameOverMonkeyVoicePath =
        "Application/Assets/Sounds/SE/guaaaaaaaaaaa.mp3";

    // モデルの正面が -Z のため、進行方向のマス差分から Y 軸回転を求める。
    // 差分がなければ今の向きを保つ。
    float HeadingYawFromDelta(int32_t deltaX, int32_t deltaZ, float fallbackYaw) {
        if (deltaX > 0) {
            return -std::numbers::pi_v<float> * 0.5f;
        }
        if (deltaX < 0) {
            return std::numbers::pi_v<float> * 0.5f;
        }
        if (deltaZ < 0) {
            return 0.0f;
        }
        if (deltaZ > 0) {
            return std::numbers::pi_v<float>;
        }
        return fallbackYaw;
    }

    // ゲーム開始時は、まだ次のレールがないため右方向（+X）を向けておく。
    constexpr float kInitialHeadingYaw = -std::numbers::pi_v<float> * 0.5f;
}

json GameComponents::TrainMovementComponent::OnSerialize() const {
    return {
        { "gridSize", gridSize_ },
        { "initialMoveSpeed", initialMoveSpeed_ },
        { "initialGridX", initialGridX_ },
        { "initialGridZ", initialGridZ_ },
        { "minimumSpeedIncreasePerRail", minimumSpeedIncreasePerRail_ },
        { "minimumSpeedMonkeyBonusRate", minimumSpeedMonkeyBonusRate_ },
        { "acceleration", acceleration_ },
        { "accelerationMonkeyBonusRate", accelerationMonkeyBonusRate_ },
        { "maximumMoveSpeed", maximumMoveSpeed_ },
        { "turnBlendRatio", turnBlendRatio_ },
        { "rockThrowJumpHeight", rockThrowJumpHeight_ },
        { "rockThrowJumpDuration", rockThrowJumpDuration_ },
        { "requiredRailCount", requiredRailCount_ }
    };
}

void GameComponents::TrainMovementComponent::OnDeserialize(const json& j) {
    gridSize_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "gridSize", gridSize_));
    initialMoveSpeed_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "initialMoveSpeed", initialMoveSpeed_));
    initialGridX_ = std::max(0, JsonManager::SafeGet<int32_t>(j, "initialGridX", initialGridX_));
    initialGridZ_ = std::max(0, JsonManager::SafeGet<int32_t>(j, "initialGridZ", initialGridZ_));
    minimumSpeedIncreasePerRail_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "minimumSpeedIncreasePerRail", minimumSpeedIncreasePerRail_));
    // 旧シーンには最低速度用の個別設定がないため、従来の共通補正率を引き継ぐ。
    acceleration_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "acceleration", acceleration_));
    accelerationMonkeyBonusRate_ = std::max(0.0f,
        JsonManager::SafeGet<float>(
            j, "accelerationMonkeyBonusRate", accelerationMonkeyBonusRate_));
    minimumSpeedMonkeyBonusRate_ = std::max(0.0f,
        JsonManager::SafeGet<float>(
            j, "minimumSpeedMonkeyBonusRate", accelerationMonkeyBonusRate_));
    maximumMoveSpeed_ = std::max(initialMoveSpeed_,
        JsonManager::SafeGet<float>(j, "maximumMoveSpeed", maximumMoveSpeed_));
    turnBlendRatio_ = std::clamp(
        JsonManager::SafeGet<float>(j, "turnBlendRatio", turnBlendRatio_), 0.0f, 0.5f);
    rockThrowJumpHeight_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "rockThrowJumpHeight", rockThrowJumpHeight_));
    rockThrowJumpDuration_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "rockThrowJumpDuration", rockThrowJumpDuration_));
    requiredRailCount_ = std::max<std::size_t>(1,
        JsonManager::SafeGet<std::size_t>(j, "requiredRailCount", requiredRailCount_));
    // 最低速度は敷設レール数から毎フレーム再計算するため、読込直後は基準値に戻す。
    minMoveSpeed_ = initialMoveSpeed_;
    moveSpeed_ = initialMoveSpeed_;
    gridX_ = initialGridX_;
    gridZ_ = initialGridZ_;
}

#ifdef USE_IMGUI
bool GameComponents::TrainMovementComponent::DrawInspector() {
    bool changed = false;

    ImGui::SeparatorText("走行");
    changed |= ImGui::DragFloat("グリッドサイズ", &gridSize_, 0.05f, 0.01f, 20.0f);
    if (ImGui::DragFloat("基準最低速度（初期・駅リセット）", &initialMoveSpeed_, 0.01f, 0.0f, 20.0f)) {
        maximumMoveSpeed_ = std::max(maximumMoveSpeed_, initialMoveSpeed_);
        moveSpeed_ = std::max(moveSpeed_, initialMoveSpeed_);
        changed = true;
    }
    changed |= ImGui::DragFloat(
        "最低速度の増加量（レール1マス）", &minimumSpeedIncreasePerRail_, 0.001f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat(
        "サル1匹追加ごとの最低速度補正率",
        &minimumSpeedMonkeyBonusRate_, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("加速度（速度/秒）", &acceleration_, 0.01f, 0.0f, 20.0f);
    changed |= ImGui::DragFloat(
        "サル1匹追加ごとの加速度補正率", &accelerationMonkeyBonusRate_, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("最高速度", &maximumMoveSpeed_, 0.01f, 0.01f, 100.0f);
    maximumMoveSpeed_ = std::max(maximumMoveSpeed_, initialMoveSpeed_);
    moveSpeed_ = std::min(moveSpeed_, maximumMoveSpeed_);
    ImGui::TextDisabled("現在の最低速度: %.3f", minMoveSpeed_);
    // 0 にすると従来どおり曲がり角で 1 フレームで向きが変わる。
    changed |= ImGui::DragFloat(
        "カーブ補間幅（マス比）", &turnBlendRatio_, 0.01f, 0.0f, 0.5f);
    changed |= ImGui::DragFloat(
        "投石ジャンプ高さ", &rockThrowJumpHeight_, 0.05f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat(
        "投石ジャンプ時間", &rockThrowJumpDuration_, 0.01f, 0.0f, 5.0f);

    ImGui::SeparatorText("配置");
    ImGui::TextDisabled("列車の接地高さ: %.3f", BlockModelLayout::GetRailTopHeight(gridSize_));
    int required = static_cast<int>(requiredRailCount_);
    if (ImGui::DragInt("発車に必要なレール数", &required, 1.0f, 1, 100)) {
        requiredRailCount_ = static_cast<std::size_t>(std::max(required, 1));
        changed = true;
    }
    changed |= ImGui::DragInt("初期X", &initialGridX_, 1.0f, 0, 500);
    changed |= ImGui::DragInt("初期Z", &initialGridZ_, 1.0f, 0, 100);
    ImGui::TextDisabled("現在速度: %.3f", moveSpeed_);
    return changed;
}
#endif

void GameComponents::TrainMovementComponent::Start() {
    transform_ = Sibling<TransformComponent>();
    // RailPathComponent がアタッチされていない場合は処理を中断する
    if (!transform_ || !railPath_ || !gameManager_ || !hunger_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "TrainMovementComponent: Transform、RailPath、GameManager または Hunger が未設定です");
        SetEnabled(false);
        return;
    }

    // 初期位置が RailPathComponent の範囲外であればエラーを出して無効化する
    if (gridX_ < 0 || gridZ_ < 0 ||
        gridZ_ >= static_cast<int32_t>(railPath_->GetMapSizeZ())) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "TrainMovementComponent: 初期位置が範囲外です ({}, {})",
            gridX_, gridZ_);
        SetEnabled(false);
        return;
    }

    // 初期位置を TransformComponent に反映する
    const float modelScale = BlockModelLayout::GetScale(gridSize_);
    transform_->Get().scale = { modelScale, modelScale, modelScale };
    transform_->Get().translate.x = static_cast<float>(gridX_) * gridSize_;
    transform_->Get().translate.y = BlockModelLayout::GetRailTopHeight(gridSize_);
    transform_->Get().translate.z = static_cast<float>(gridZ_) * gridSize_;
    previousHeadingYaw_ = kInitialHeadingYaw;
    headingYaw_ = kInitialHeadingYaw;
    nextHeadingYaw_ = kInitialHeadingYaw;
    transform_->Get().rotate.y = kInitialHeadingYaw;
    hasHeading_ = false;
    entryTurnProgress_ = 0.0f;
    traveledCells_.clear();
    traveledCells_.emplace_back(gridX_, gridZ_);
    pendingStationSteps_.clear();
    traveledBlockCount_ = 0;
}

void GameComponents::TrainMovementComponent::Update() {
    if (!transform_ || !railPath_ || isGameOver_) {
        return;
    }

    const float deltaTime = Time::DeltaTime();
    UpdateRockThrowJump(deltaTime);
    const float modelScale = BlockModelLayout::GetScale(gridSize_);
    transform_->Get().scale = { modelScale, modelScale, modelScale };
    transform_->Get().translate.y =
        BlockModelLayout::GetRailTopHeight(gridSize_) + GetRockThrowJumpOffset();
    SyncCarriageTransforms();

    // 投石キューが空になるまでは移動せず、その場で投石ジャンプだけ再生する。
    if (isPausedForRockBreak_) {
        return;
    }

    // 発車前は、プレイヤーが未確定レールを指定マス敷くまで待機する。
    if (!hasStarted_) {
        if (railPath_->GetUnconfirmedRailCount() < requiredRailCount_) {
            return;
        }
        hasStarted_ = true;
    }

    if (deltaTime <= 0.0f) {
        return;
    }

    const std::size_t laidRailCount = railPath_->GetLaidRailCount();
    const std::size_t monkeyCount = hunger_->GetMonkeyCount();
    const float additionalMonkeyCount =
        static_cast<float>(monkeyCount > 0 ? monkeyCount - 1 : 0);
    // 最低速度と加速度は別々の補正率で調整できる。
    const float monkeySpeedBonus = 1.0f +
        additionalMonkeyCount * minimumSpeedMonkeyBonusRate_;
    const float monkeyAccelerationBonus = 1.0f +
        additionalMonkeyCount * accelerationMonkeyBonusRate_;
    const float effectiveMinimumSpeedIncrease =
        minimumSpeedIncreasePerRail_ * monkeySpeedBonus;
    const float dynamicMinimum = initialMoveSpeed_ +
        static_cast<float>(laidRailCount) * effectiveMinimumSpeedIncrease;
    minMoveSpeed_ = std::min(dynamicMinimum, maximumMoveSpeed_);
    const float effectiveAcceleration = acceleration_ * monkeyAccelerationBonus;
    // 駅で最低速度へ戻した後、猿数に応じた加速度で最高速度まで徐々に加速する。
    moveSpeed_ = std::clamp(
        moveSpeed_ + effectiveAcceleration * deltaTime,
        minMoveSpeed_, maximumMoveSpeed_);

    // 移動量を計算する前に進行方向を確定し、曲がり角なら減速を反映する。
    if (!isMoving_ && !BeginNextSegment()) {
        NotifyGameOver();
        return;
    }
    // DeltaTime に応じて移動進捗を加算する。
    float remainingProgress = moveSpeed_ * deltaTime;
    if (remainingProgress <= 0.0f) {
        return;
    }

    // 大きな DeltaTime でも目的地を飛び越さないよう、余った進捗を次のマスへ持ち越す。
    while (remainingProgress > 0.0f && !isGameOver_) {
        if (!isMoving_) {
            if (!BeginNextSegment()) {
                NotifyGameOver();
                break;
            }
        }

        const float progressToDestination = 1.0f - movementProgress_;
        const float appliedProgress = std::min(remainingProgress, progressToDestination);
        movementProgress_ += appliedProgress;
        remainingProgress -= appliedProgress;
        SyncTransformToProgress();

        if (movementProgress_ < 1.0f) {
            break;
        }

        // 誤差を残さず、到着したマスの中央へ固定する。
        gridX_ = destinationGridX_;
        gridZ_ = destinationGridZ_;
        movementProgress_ = 0.0f;
        isMoving_ = false;
        transform_->Get().translate.x = static_cast<float>(gridX_) * gridSize_;
        transform_->Get().translate.z = static_cast<float>(gridZ_) * gridSize_;

        horizontalProgressBlocks_ = std::max(horizontalProgressBlocks_,
            static_cast<uint32_t>(std::max(0, gridX_ - initialGridX_)));
        const bool stationActivated = hunger_->OnTrainEnteredCell(gridX_, gridZ_);
        if (stationActivated) {
            // 駅では一時減速せず、現在の最低速度を次の加速の開始速度にする。
            moveSpeed_ = minMoveSpeed_;
        }
        ProcessCarriageArrival(stationActivated);

        // 発車後に終端へ到着した時点でゲームオーバーにする。
        if (railPath_->GetUnconfirmedRailCount() == 0) {
            NotifyGameOver();
        }
    }
}

void GameComponents::TrainMovementComponent::NotifyGameOver() {
    if (isGameOver_) {
        return;
    }

    isGameOver_ = true;
    if (transform_) {
        transform_->Get().translate.y = BlockModelLayout::GetRailTopHeight(gridSize_);
    }
    PlayGameOverLaunch();
    if (gameManager_) {
        gameManager_->RequestGameOver();
    }
}

bool GameComponents::TrainMovementComponent::IsGameOver() const {
    return isGameOver_;
}

Vector3 GameComponents::TrainMovementComponent::GetWorldPosition() const {
    if (transform_) {
        return transform_->Get().translate;
    }
    return {
        static_cast<float>(gridX_) * gridSize_,
        BlockModelLayout::GetRailTopHeight(gridSize_),
        static_cast<float>(gridZ_) * gridSize_
    };
}

float GameComponents::TrainMovementComponent::GetCursorHeightOffsetAt(
    float worldX, float worldZ) const {
    float heightOffset = 0.0f;
    const auto considerVehicle = [&](const Vector3& position) {
        // マス間の移動中も、矢印と車両の幅が重なる範囲では高く保つ。
        if (std::abs(position.x - worldX) < gridSize_ &&
            std::abs(position.z - worldZ) < gridSize_) {
            const float jumpOffset = std::max(
                0.0f, position.y - BlockModelLayout::GetRailTopHeight(gridSize_));
            heightOffset = std::max(heightOffset, gridSize_ + jumpOffset);
        }
    };
    considerVehicle(GetWorldPosition());
    for (const auto* carriage : carriageTransforms_) {
        considerVehicle(carriage->Get().translate);
    }
    return heightOffset;
}

void GameComponents::TrainMovementComponent::SetGridSize(float size) {
    if (size > 0.0f) {
        gridSize_ = size;
    }
}

bool GameComponents::TrainMovementComponent::BeginNextSegment() {
    std::pair<int32_t, int32_t> destination{};
    if (!railPath_->TryGetNextUnconfirmedRail(destination)) {
        return false;
    }

    const int32_t deltaX = destination.first - gridX_;
    const int32_t deltaZ = destination.second - gridZ_;
    if (std::abs(deltaX) + std::abs(deltaZ) != 1) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "TrainMovementComponent: レールが連続していません ({}, {}) -> ({}, {})",
            gridX_, gridZ_, destination.first, destination.second);
        return false;
    }

    // 確定でキューから消える前に、移動先をコンポーネント内へ保存する。
    destinationGridX_ = destination.first;
    destinationGridZ_ = destination.second;
    if (!railPath_->ConfirmNextRailPlacement()) {
        return false;
    }

    movementProgress_ = 0.0f;
    isMoving_ = true;

    // 直前のマスで先読みした向きと一致していれば、角の手前で既に半分曲がり終えている。
    const float headingYaw = HeadingYawFromDelta(deltaX, deltaZ, headingYaw_);
    entryTurnProgress_ =
        (hasHeading_ && std::abs(nextHeadingYaw_ - headingYaw) < kYawEpsilon) ? 0.5f : 0.0f;
    previousHeadingYaw_ = hasHeading_ ? headingYaw_ : headingYaw;
    headingYaw_ = headingYaw;
    hasHeading_ = true;

    // 確定でキューの先頭は目的地の次のマスになる。そこから曲がり終わりの向きを先読みする。
    nextHeadingYaw_ = headingYaw_;
    std::pair<int32_t, int32_t> lookahead{};
    if (railPath_->TryGetNextUnconfirmedRail(lookahead)) {
        const int32_t nextDeltaX = lookahead.first - destinationGridX_;
        const int32_t nextDeltaZ = lookahead.second - destinationGridZ_;
        if (std::abs(nextDeltaX) + std::abs(nextDeltaZ) == 1) {
            nextHeadingYaw_ = HeadingYawFromDelta(nextDeltaX, nextDeltaZ, headingYaw_);
        }
    }

    UpdateRotation();
    return true;
}

void GameComponents::TrainMovementComponent::SyncTransformToProgress() {
    // 移動中のマスの中央から、目的地のマスの中央までの線形補間
    const float startX = static_cast<float>(gridX_) * gridSize_;
    const float startZ = static_cast<float>(gridZ_) * gridSize_;
    const float destinationX = static_cast<float>(destinationGridX_) * gridSize_;
    const float destinationZ = static_cast<float>(destinationGridZ_) * gridSize_;
    // 進捗に応じて TransformComponent の位置を更新する
    transform_->Get().translate.x =
        startX + (destinationX - startX) * movementProgress_;
    transform_->Get().translate.z =
        startZ + (destinationZ - startZ) * movementProgress_;

    transform_->Get().translate.y =
        BlockModelLayout::GetRailTopHeight(gridSize_) + GetRockThrowJumpOffset();
    UpdateRotation();
    SyncCarriageTransforms();
}

const CoreEngine::TransformComponent*
GameComponents::TrainMovementComponent::GetMonkeyTransform(std::size_t monkeyIndex) const {
    // 先頭のサルは機関車に乗っているので、車両の配列には入っていない。
    if (monkeyIndex == 0) {
        return transform_;
    }
    const std::size_t carriageIndex = monkeyIndex - 1;
    return carriageIndex < carriageTransforms_.size()
        ? carriageTransforms_[carriageIndex]
        : nullptr;
}

void GameComponents::TrainMovementComponent::AddCarriage(
    TransformComponent* carriageTransform, const Vector3* scaleMultiplier) {
    if (!carriageTransform || traveledCells_.size() < carriageTransforms_.size() + 2) {
        return;
    }
    carriageTransforms_.push_back(carriageTransform);
    carriageScaleMultipliers_.push_back(scaleMultiplier);
    SyncCarriageTransforms();
    // 生成フレームは TransformComponent::Update() がまだ走らない。ここで転送しないと
    // 連結した最初の1フレームだけ、生成時の既定行列（原点・等倍）で描かれる。
    carriageTransform->Get().TransferMatrix();
}

void GameComponents::TrainMovementComponent::AddMonkey(
    TransformComponent* monkeyTransform, ParticleSystem* launchTrail) {
    if (monkeyTransform) {
        monkeyTransforms_.push_back(monkeyTransform);
        monkeyLaunchTrails_.push_back(launchTrail);
    }
}

void GameComponents::TrainMovementComponent::ProcessCarriageArrival(bool stationActivated) {
    ++traveledBlockCount_;
    traveledCells_.emplace_back(gridX_, gridZ_);
    // 先頭と同じ進捗で各車両もマス中央に到着する。経路上の到着マスで回復を判定する。
    for (std::size_t index = 0; index < carriageTransforms_.size(); ++index) {
        const std::size_t offset = index + 1;
        if (traveledCells_.size() <= offset) {
            break;
        }
        const auto& [carriageX, carriageZ] = traveledCells_[traveledCells_.size() - 1 - offset];
        hunger_->OnMonkeyEnteredCell(offset, carriageX, carriageZ);
    }
    if (stationActivated) {
        pendingStationSteps_.push_back(traveledBlockCount_);
    }

    // 最後尾は先頭から後続車両数だけ遅れている。駅の次の中央へ着くまで待つ。
    // 連続する駅でも、先の駅で増えた車両を含めた最後尾で毎回判定する。
    while (!pendingStationSteps_.empty() &&
        traveledBlockCount_ - pendingStationSteps_.front() >= carriageTransforms_.size() + 1) {
        pendingStationSteps_.pop_front();
        hunger_->AddMonkey();
    }

    // 次の連結用に最後尾の1マス後ろまで残し、走行距離に比例して履歴を増やさない。
    while (traveledCells_.size() > carriageTransforms_.size() + 2) {
        traveledCells_.pop_front();
    }
    SyncCarriageTransforms();
}

void GameComponents::TrainMovementComponent::SyncCarriageTransforms() {
    const float modelScale = BlockModelLayout::GetScale(gridSize_);
    const std::size_t cellCount = traveledCells_.size();
    for (std::size_t index = 0; index < carriageTransforms_.size(); ++index) {
        const std::size_t offset = index + 1;
        if (cellCount <= offset) {
            break;
        }
        const std::size_t cellIndex = cellCount - 1 - offset;
        const auto& [startX, startZ] = traveledCells_[cellIndex];
        const auto& [endX, endZ] = traveledCells_[cellIndex + 1];
        auto& carriage = carriageTransforms_[index]->Get();
        // 連結直後の出現演出など、外から渡された拡縮を掛ける。
        // ここで掛けないと、TransformComponent::Update() がワールド行列を焼いた
        // 後の書き込みになり、そのフレームの描画へ届かない。
        const Vector3* scaleMultiplier = index < carriageScaleMultipliers_.size()
            ? carriageScaleMultipliers_[index]
            : nullptr;
        carriage.scale = scaleMultiplier
            ? Vector3{
                modelScale * scaleMultiplier->x,
                modelScale * scaleMultiplier->y,
                modelScale * scaleMultiplier->z }
            : Vector3{ modelScale, modelScale, modelScale };
        carriage.translate = {
            (static_cast<float>(startX) + static_cast<float>(endX - startX) * movementProgress_) * gridSize_,
            BlockModelLayout::GetRailTopHeight(gridSize_),
            (static_cast<float>(startZ) + static_cast<float>(endZ - startZ) * movementProgress_) * gridSize_
        };

        // 先頭車両と同じく、通過済みの履歴から前後の向きを引いて曲がり角をまたいで補間する。
        const float headingYaw =
            HeadingYawFromDelta(endX - startX, endZ - startZ, carriage.rotate.y);
        float previousYaw = headingYaw;
        if (cellIndex > 0) {
            const auto& [beforeX, beforeZ] = traveledCells_[cellIndex - 1];
            previousYaw = HeadingYawFromDelta(startX - beforeX, startZ - beforeZ, headingYaw);
        }
        float nextYaw = headingYaw;
        if (cellIndex + 2 < cellCount) {
            const auto& [afterX, afterZ] = traveledCells_[cellIndex + 2];
            nextYaw = HeadingYawFromDelta(afterX - endX, afterZ - endZ, headingYaw);
        } else if (isMoving_) {
            // 先頭車両の次のマスはまだ履歴にないので、機関車の目的地を使う。
            nextYaw = HeadingYawFromDelta(
                destinationGridX_ - endX, destinationGridZ_ - endZ, headingYaw);
        }
        // 車両は履歴から前後が分かるので、常に角の手前から曲がり始めている。
        carriage.rotate.y =
            EvaluateTurnYaw(previousYaw, headingYaw, nextYaw, movementProgress_, 0.5f);
    }
}

void GameComponents::TrainMovementComponent::PlayRockThrowJump() {
    rockThrowJumpElapsed_ = 0.0f;
    isRockThrowJumping_ = rockThrowJumpDuration_ > 0.0f && rockThrowJumpHeight_ > 0.0f;
}

void GameComponents::TrainMovementComponent::PlayGameOverLaunch() {
    if (gameOverLaunchStarted_) {
        return;
    }
    gameOverLaunchStarted_ = true;

    CameraShakeParams gameOverShake = CameraShakePresets::HeavyHit();
    gameOverShake.positionAmplitude = gameOverShake.positionAmplitude * 2.0f;
    gameOverShake.rotationAmplitude = gameOverShake.rotationAmplitude * 2.0f;
    gameOverShake.duration = 1.2f;
    gameOverShake.timeMode = ShakeTimeMode::Unscaled;
    CameraShake::Play(gameOverShake);

    if (GameObject* owner = GetOwner()) {
        if (EngineSystem* engine = owner->GetEngineSystem()) {
            if (auto* audioSystem = engine->GetService<AudioSystem>()) {
                audioSystem->PlayOneShot(
                    kGameOverSePath,
                    { .bus = AudioBus::SE });
                audioSystem->PlayOneShot(
                    kGameOverMonkeyVoicePath,
                    { .bus = AudioBus::SE });
            }
        }
    }

    // ゲームオーバー判定は列車移動の途中で発生するため、直前に更新された
    // ローカル座標をワールド行列へ反映してから、サルの現在位置を取得する。
    if (transform_) {
        transform_->Get().TransferMatrix();
    }
    for (auto* carriageTransform : carriageTransforms_) {
        if (carriageTransform) {
            carriageTransform->Get().TransferMatrix();
        }
    }

    // headingYaw_ は直前に走っていた最終レールの向き。まだ一度も発車して
    // いない場合だけ、履歴の最後の2マスから向きを復元する。
    float finalYaw = hasHeading_ ? headingYaw_ : kInitialHeadingYaw;
    if (!hasHeading_ && traveledCells_.size() >= 2) {
        const auto& [startX, startZ] = traveledCells_[traveledCells_.size() - 2];
        const auto& [endX, endZ] = traveledCells_.back();
        finalYaw = HeadingYawFromDelta(endX - startX, endZ - startZ, finalYaw);
    }

    // 列車モデルの正面は -Z。HeadingYawFromDelta と同じ座標系で、
    // 最終レールの進行方向へサルを飛ばす。
    const Vector3 launchDirection{
        -std::sin(finalYaw),
        0.0f,
        -std::cos(finalYaw) };

    const auto playTrolleyTilt = [](GameObject* trolley,
        TransformComponent* trolleyTransform, std::size_t trolleyIndex) {
            if (!trolley || !trolleyTransform) {
                return;
            }

            const Vector3 originalRotation = trolleyTransform->Get().rotate;
            Vector3 tiltedRotation = originalRotation;
            tiltedRotation.x += kGameOverTrolleyTiltX;
            tiltedRotation.z -= kGameOverTrolleyTiltZ;

            TweenSequence tilt;
            tilt
                .Append(
                    Tween::RotateTo(
                        trolley,
                        tiltedRotation,
                        kGameOverTrolleyTiltInDuration)
                    .SetEase(EasingUtil::Type::EaseOutQuad))
                .Append(
                    Tween::RotateTo(
                        trolley,
                        originalRotation,
                        kGameOverTrolleyTiltOutDuration)
                    .SetEase(EasingUtil::Type::EaseInOutSine))
                .SetLink(trolley)
                .SetUpdateType(TweenUpdate::Unscaled)
                .SetId(
                    std::string("game_over_trolley_tilt_")
                    + std::to_string(trolleyIndex));
        };

    // サルがいないケースでは、トロッコだけが傾かないようにする。
    if (!monkeyTransforms_.empty()) {
        playTrolleyTilt(GetOwner(), transform_, 0);
        for (std::size_t index = 0; index < carriageTransforms_.size(); ++index) {
            playTrolleyTilt(
                carriageTransforms_[index] ? carriageTransforms_[index]->GetOwner() : nullptr,
                carriageTransforms_[index],
                index + 1);
        }
    }

    const auto launchMonkey = [this, &launchDirection](
        TransformComponent* monkeyTransform,
        std::size_t monkeyIndex) {
            if (!monkeyTransform || !monkeyTransform->GetOwner()) {
                return;
            }

            auto& monkeyWorld = monkeyTransform->Get();
            monkeyWorld.TransferMatrix();

            // 親の車両を外しても見た目が跳ねないよう、現在のワールド姿勢を
            // サル自身のローカル値へ焼き直してから独立させる。
            Vector3 startScale{};
            Vector3 startRotation{};
            Vector3 startPosition{};
            MathCore::Matrix::DecomposeToSRT(
                monkeyWorld.GetWorldMatrix(), startScale, startRotation, startPosition);
            monkeyWorld.SetParent(nullptr);
            monkeyWorld.scale = startScale;
            monkeyWorld.rotate = startRotation;
            monkeyWorld.translate = startPosition;

            GameObject* monkey = monkeyTransform->GetOwner();
            const float distance =
                kGameOverLaunchDistance + static_cast<float>(monkeyIndex) * 0.35f;
            const Vector3 apexPosition{
                startPosition.x + launchDirection.x * distance * 0.42f,
                startPosition.y + kGameOverLaunchRiseHeight,
                startPosition.z + launchDirection.z * distance * 0.42f };
            const Vector3 endPosition{
                startPosition.x + launchDirection.x * distance,
                // 2 区間目も最高点の高さを保ち、落下させずに飛び続ける。
                apexPosition.y,
                startPosition.z + launchDirection.z * distance };
            const Vector3 midRotation{
                startRotation.x + kGameOverLaunchSpin * 0.4f,
                startRotation.y - kGameOverLaunchSpin * 0.3f,
                startRotation.z + kGameOverLaunchSpin * 0.5f };
            const Vector3 endRotation{
                startRotation.x + kGameOverLaunchSpin,
                startRotation.y - kGameOverLaunchSpin * 0.75f,
                startRotation.z + kGameOverLaunchSpin * 0.9f };
            ParticleSystem* launchTrail = monkeyIndex < monkeyLaunchTrails_.size()
                ? monkeyLaunchTrails_[monkeyIndex]
                : nullptr;
            if (launchTrail) {
                launchTrail->SetEmitterPosition(startPosition);
                launchTrail->Clear();
                launchTrail->GetMainModule().Restart();
                launchTrail->GetEmissionModule().Play();
            }

            TweenSequence launch;
            launch
                .Append(
                    Tween::MoveTo(
                        monkey,
                        apexPosition,
                        kGameOverLaunchRiseDuration)
                    .SetEase(EasingUtil::Type::EaseOutQuad))
                .Join(
                    Tween::RotateTo(
                        monkey,
                        midRotation,
                        kGameOverLaunchRiseDuration)
                    .SetEase(EasingUtil::Type::EaseOutCubic))
                .Append(
                    Tween::MoveTo(
                        monkey,
                        endPosition,
                        kGameOverLaunchFlightDuration)
                    .SetEase(EasingUtil::Type::EaseInCubic))
                .Join(
                    Tween::RotateTo(
                        monkey,
                        endRotation,
                        kGameOverLaunchFlightDuration)
                    .SetEase(EasingUtil::Type::EaseInCubic))
                .SetLink(monkey)
                .SetUpdateType(TweenUpdate::Unscaled)
                .SetId(
                    std::string("game_over_monkey_launch_")
                    + std::to_string(monkeyIndex));
            launch.Handle().OnUpdate([launchTrail, monkeyTransform](float) {
                if (launchTrail && monkeyTransform) {
                    // エミッターはサルに追従させ、生成済みパーティクルはワールド空間で残す。
                    launchTrail->SetEmitterPosition(monkeyTransform->Get().translate);
                }
            });
            launch.OnComplete([launchTrail]() {
                if (launchTrail) {
                    // 生きている粒はそのまま残し、以降の放出だけ止める。
                    launchTrail->Stop();
                }
            });
        };

    for (std::size_t index = 0; index < monkeyTransforms_.size(); ++index) {
        launchMonkey(monkeyTransforms_[index], index);
    }
}

void GameComponents::TrainMovementComponent::UpdateRockThrowJump(float deltaTime) {
    if (!isRockThrowJumping_) {
        return;
    }

    rockThrowJumpElapsed_ += std::max(deltaTime, 0.0f);
    if (rockThrowJumpElapsed_ >= rockThrowJumpDuration_) {
        rockThrowJumpElapsed_ = rockThrowJumpDuration_;
        isRockThrowJumping_ = false;
    }
}

float GameComponents::TrainMovementComponent::GetRockThrowJumpOffset() const {
    if (!isRockThrowJumping_ || rockThrowJumpDuration_ <= 0.0f) {
        return 0.0f;
    }

    const float progress = std::clamp(
        rockThrowJumpElapsed_ / rockThrowJumpDuration_, 0.0f, 1.0f);
    return rockThrowJumpHeight_ * 4.0f * progress * (1.0f - progress);
}

void GameComponents::TrainMovementComponent::UpdateRotation() {
    // 進行方向に応じた Y 軸回転を、曲がり角の前後をまたいで補間しながら設定する。
    transform_->Get().rotate.y = EvaluateTurnYaw(
        previousHeadingYaw_, headingYaw_, nextHeadingYaw_,
        movementProgress_, entryTurnProgress_);
}

float GameComponents::TrainMovementComponent::EvaluateTurnYaw(
    float previousYaw, float headingYaw, float nextYaw,
    float progress, float entryTurnProgress) const {
    // 曲がり角の中心を境に、前後 turnBlendRatio_ マス分をかけて向きを変える。
    const float ratio = std::clamp(turnBlendRatio_, 0.0f, 0.5f);
    if (ratio <= 0.0f) {
        return headingYaw;
    }

    const float clampedProgress = std::clamp(progress, 0.0f, 1.0f);
    // 直前のマスから続く曲がりの後半。入った時点の進み具合から曲がり切るまで回し続ける。
    if (clampedProgress < ratio) {
        const float entry = std::clamp(entryTurnProgress, 0.0f, 1.0f);
        const float turnProgress = entry + (1.0f - entry) * (clampedProgress / ratio);
        return EasingUtil::LerpAngle(previousYaw, headingYaw, turnProgress, kTurnEasing);
    }
    // 次のマスへ続く曲がりの前半。角に着く前から向きを変え始める。
    if (clampedProgress > 1.0f - ratio) {
        const float turnProgress = 0.5f * ((clampedProgress - (1.0f - ratio)) / ratio);
        return EasingUtil::LerpAngle(headingYaw, nextYaw, turnProgress, kTurnEasing);
    }
    return headingYaw;
}
