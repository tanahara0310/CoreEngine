#include "pch.h"
#include "RailBuilderComponent.h"
#include "Components/Utility/BlockModelLayout.h"

#include "Audio/AudioSystem.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "RailPathComponent.h"
#include "Components/Building/MapGeneratorComponent.h"
#include "Components/Building/RockThrowComponent.h"
#include "Components/Camera/RockBreakShakeSettingsComponent.h"
#include "Components/GameCore/HungerComponent.h"
#include "Components/GameCore/GameResultData.h"
#include "Components/GameCore/GameSettingsComponent.h"
#include "Components/Train/TrainMovementComponent.h"
#include "GameObjects/Effect/RockBreakDebris.h"
#include "Camera/Camera.h"
#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Random/RandomGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#include "Editor/ImGui/CVarPanel.h"
#endif

using namespace CoreEngine;

namespace {
    constexpr const char* kRockThrowSePath =
        "Application/Assets/Sounds/SE/rock_throw.mp3";
    constexpr const char* kRockBreakSePath =
        "Application/Assets/Sounds/SE/rock_break.mp3";
    constexpr const char* kBananaBuildSePath =
        "Application/Assets/Sounds/SE/banana_build.mp3";

    void PlayRandomPitchSe(GameObject* owner, const char* path) {
        if (!owner) {
            return;
        }

        EngineSystem* engine = owner->GetEngineSystem();
        if (auto* audioSystem = engine ? engine->GetService<AudioSystem>() : nullptr) {
            audioSystem->PlayOneShot(
                path,
                { .bus = AudioBus::SE,
                  .pitch = RandomGenerator::GetInstance().GetFloat(1.0f, 1.3f) });
        }
    }

    bool IsAdjacentToBananaTree(
        const GameComponents::MapGeneratorComponent* mapGenerator,
        int32_t gridX,
        int32_t gridZ) {
        if (!mapGenerator) {
            return false;
        }

        constexpr std::array<std::pair<int32_t, int32_t>, 4> kDirections = {
            std::pair{ 1, 0 }, std::pair{ -1, 0 },
            std::pair{ 0, 1 }, std::pair{ 0, -1 }
        };
        for (const auto& [offsetX, offsetZ] : kDirections) {
            const int32_t adjacentX = gridX + offsetX;
            const int32_t adjacentZ = gridZ + offsetZ;
            if (adjacentX < 0 || adjacentZ < 0) {
                continue;
            }
            if (mapGenerator->GetMapChip(
                    static_cast<std::size_t>(adjacentX),
                    static_cast<std::size_t>(adjacentZ)) ==
                GameComponents::MapChipType::BananaTree) {
                return true;
            }
        }
        return false;
    }
}

json GameComponents::RailBuilderComponent::OnSerialize() const {
    return {
        { "gridSize", gridSize_ },
        { "initialGridX", initialGridPosX_ },
        { "initialGridZ", initialGridPosZ_ },
        { "horizontalPrioritize", HorizontalPrioritize },
        { "undoHoldTime", undoPushMaxTime_ },
        { "undoInterval", undoInterval_ },
        { "buildHoldTime", buildPushMaxTime_ },
        { "buildInterval", buildInterval_ },
        { "height", height_ },
        { "pulseBaseScale", pulseBaseScale_ },
        { "pulseAmplitude", pulseAmplitude_ },
        { "pulseSpeed", pulseSpeed_ },
        { "rotationSpeed", rotationSpeed_ },
        { "rockCursorHeightOffset", rockCursorHeightOffset_ },
        { "rockThrowStartHeight", rockThrowStartHeight_ },
        { "rockImpactHeight", rockImpactHeight_ },
        { "cursorEdgeRadiusRatio", cursorEdgeRadiusRatio_ }
    };
}

void GameComponents::RailBuilderComponent::OnDeserialize(const json& j) {
    gridSize_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "gridSize", gridSize_));
    initialGridPosX_ = std::max(0, JsonManager::SafeGet<int32_t>(j, "initialGridX", initialGridPosX_));
    initialGridPosZ_ = std::max(0, JsonManager::SafeGet<int32_t>(j, "initialGridZ", initialGridPosZ_));
    HorizontalPrioritize = JsonManager::SafeGet<bool>(j, "horizontalPrioritize", HorizontalPrioritize);
    undoPushMaxTime_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "undoHoldTime", undoPushMaxTime_));
    undoInterval_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "undoInterval", undoInterval_));
    buildPushMaxTime_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "buildHoldTime", buildPushMaxTime_));
    buildInterval_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "buildInterval", buildInterval_));
    height_ = JsonManager::SafeGet<float>(j, "height", height_);
    pulseBaseScale_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "pulseBaseScale", pulseBaseScale_));
    pulseAmplitude_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "pulseAmplitude", pulseAmplitude_));
    pulseSpeed_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "pulseSpeed", pulseSpeed_));
    rotationSpeed_ = JsonManager::SafeGet<float>(j, "rotationSpeed", rotationSpeed_);
    rockCursorHeightOffset_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "rockCursorHeightOffset", rockCursorHeightOffset_));
    rockThrowStartHeight_ = JsonManager::SafeGet<float>(
        j, "rockThrowStartHeight", rockThrowStartHeight_);
    rockImpactHeight_ = JsonManager::SafeGet<float>(j, "rockImpactHeight", rockImpactHeight_);
    cursorEdgeRadiusRatio_ = std::max(0.0f, JsonManager::SafeGet<float>(
        j, "cursorEdgeRadiusRatio", cursorEdgeRadiusRatio_));
    gridPosX_ = initialGridPosX_;
    gridPosZ_ = initialGridPosZ_;
}

#ifdef USE_IMGUI
bool GameComponents::RailBuilderComponent::DrawInspector() {
    bool changed = false;
    changed |= ImGui::DragFloat("グリッドサイズ", &gridSize_, 0.05f, 0.01f, 20.0f);
    changed |= ImGui::DragInt("初期X", &initialGridPosX_, 1.0f, 0, 500);
    changed |= ImGui::DragInt("初期Z", &initialGridPosZ_, 1.0f, 0, 100);
    changed |= ImGui::Checkbox("水平方向を優先", &HorizontalPrioritize);
    changed |= ImGui::DragFloat("Undo長押し時間", &undoPushMaxTime_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("Undo連続間隔", &undoInterval_, 0.01f, 0.01f, 2.0f);
    changed |= ImGui::DragFloat("設置長押し時間", &buildPushMaxTime_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("設置連続間隔", &buildInterval_, 0.01f, 0.01f, 2.0f);
    changed |= ImGui::DragFloat("表示高さ", &height_, 0.05f, -20.0f, 20.0f);
    changed |= ImGui::DragFloat("脈動基準スケール", &pulseBaseScale_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("脈動振幅", &pulseAmplitude_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("脈動速度", &pulseSpeed_, 0.05f, 0.0f, 30.0f);
    changed |= ImGui::DragFloat("回転速度", &rotationSpeed_, 0.05f, -30.0f, 30.0f);
    changed |= ImGui::DragFloat(
        "岩破壊中カーソル高さ", &rockCursorHeightOffset_, 0.05f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat(
        "投石開始高さ", &rockThrowStartHeight_, 0.05f, -10.0f, 10.0f);
    changed |= ImGui::DragFloat(
        "投石着弾高さ", &rockImpactHeight_, 0.05f, -10.0f, 10.0f);
    changed |= ImGui::DragFloat(
        "画面端のカーソル半径", &cursorEdgeRadiusRatio_, 0.01f, 0.0f, 3.0f);
    UI::Hint("画面端で止める位置。1マスに対する矢印の半径の割合で、"
        "0.5 で矢印が端にちょうど触れます。0 だと半分はみ出します。");
    ImGui::SeparatorText("スタミナ消費量");
    changed |= CVarUI::DrawTree("Game.Stamina.Cost");
    UI::Hint("変更はCVars.jsonへ自動保存され、次の建設から反映されます。");
    return changed;
}
#endif

void GameComponents::RailBuilderComponent::Start() {
    transform_ = Sibling<TransformComponent>();

    if (!transform_ || !railPath_ ||
        !mapGenerator_ || !trainMovement_ || !hunger_ || !rockThrow_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "RailBuilderComponent: 必要なコンポーネントが未設定です");
        SetEnabled(false);
        return;
    }

    if (gridPosX_ < 0 || gridPosZ_ < 0 ||
        gridPosZ_ >= static_cast<int32_t>(railPath_->GetMapSizeZ())) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "RailBuilderComponent: 初期位置が範囲外です ({}, {})",
            gridPosX_, gridPosZ_);
        SetEnabled(false);
        return;
    }

    SyncTransformToGrid();
}

void GameComponents::RailBuilderComponent::Update() {
    // TransformComponent がアタッチされていない場合は処理を中断する
    if (!transform_) {
        return;
    }
    // RailPathComponent がアタッチされていない場合は処理を中断する
    if (!railPath_) {
        return;
    }
    // タイマーを更新する
    timer_ += Time::DeltaTime();

    // TransformComponent のスケールと回転を更新する
    const float modelScale = BlockModelLayout::GetScale(gridSize_);
    transform_->Get().scale = {
        modelScale,
        modelScale * (pulseBaseScale_ + sinf(timer_ * pulseSpeed_) * pulseAmplitude_),
        modelScale };
    transform_->Get().rotate.y = timer_ * rotationSpeed_;

    // ゲームオブジェクトのオーナーからエンジンシステムを取得し、入力マネージャーを取得する
    GameObject* owner = GetOwner();
    EngineSystem* engine =
        owner ? owner->GetEngineSystem() : nullptr;
    // ここで InputManager を取得する
    InputManager* inputManager =
        engine ? engine->GetService<InputManager>() : nullptr;
    // InputManager が存在しない場合は処理を中断する
    if (!inputManager) {
        return;
    }

    // 入力クエリを取得する
    const InputQuery& input = inputManager->GetQuery();

    // 移動入力は押した瞬間に1回処理し、長押し時はUndoと同じように
    // 一定時間経過後、一定間隔で繰り返す。
    bool isContinuousBuild = false;
    const bool isMovePressed =
        input.IsActionPressed(InputAction::MoveRight) ||
        input.IsActionPressed(InputAction::MoveLeft) ||
        input.IsActionPressed(InputAction::MoveForward) ||
        input.IsActionPressed(InputAction::MoveBack);
    if (isMovePressed) {
        buildPushTimer_ += Time::DeltaTime();
        if (buildPushTimer_ >= buildPushMaxTime_) {
            if (buildIntervalTimer_ <= 0.0f) {
                isContinuousBuild = true;
                buildIntervalTimer_ = buildInterval_;
            } else {
                buildIntervalTimer_ -= Time::DeltaTime();
            }
        }
    } else {
        buildPushTimer_ = 0.0f;
        buildIntervalTimer_ = 0.0f;
    }

    const bool isMoveTriggered =
        input.IsActionTriggered(InputAction::MoveRight) ||
        input.IsActionTriggered(InputAction::MoveLeft) ||
        input.IsActionTriggered(InputAction::MoveForward) ||
        input.IsActionTriggered(InputAction::MoveBack);
    if (!isMoveTriggered && !isContinuousBuild) {
        return;
    }

    const bool usePressedDirection = !isMoveTriggered;

    // X方向の移動量を計算する（右キー - 左キー）
    float moveX =
        static_cast<float>(usePressedDirection
            ? input.IsActionPressed(InputAction::MoveRight)
            : input.IsActionTriggered(InputAction::MoveRight)) -
        static_cast<float>(usePressedDirection
            ? input.IsActionPressed(InputAction::MoveLeft)
            : input.IsActionTriggered(InputAction::MoveLeft));

    // Z方向の移動量を計算する（前キー - 後キー）
    float moveZ =
        static_cast<float>(usePressedDirection
            ? input.IsActionPressed(InputAction::MoveForward)
            : input.IsActionTriggered(InputAction::MoveForward)) -
        static_cast<float>(usePressedDirection
            ? input.IsActionPressed(InputAction::MoveBack)
            : input.IsActionTriggered(InputAction::MoveBack));

    // 反対方向の入力が同時に発生した場合は移動しない。
    if (moveX == 0.0f && moveZ == 0.0f) {
        return;
    }

    // 優先方向以外の移動を無効化する
    if (HorizontalPrioritize) {// 水平方向優先
        if(std::abs(moveX)> 0.0f) {
            moveZ = 0.0f;
        }
    } else {// 垂直方向優先
        if (std::abs(moveZ) > 0.0f) {
            moveX = 0.0f;
        }
    }

    // Transform ではなく論理グリッド座標を先に更新する
    const int32_t nextX = gridPosX_ + static_cast<int32_t>(moveX);
    const int32_t nextZ = gridPosZ_ + static_cast<int32_t>(moveZ);

    if (nextX < 0 || nextZ < 0 ||
        nextZ >= static_cast<int32_t>(railPath_->GetMapSizeZ())) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "RailBuilder: 移動先が範囲外です ({}, {})",
            nextX, nextZ);
        return;
    }

    // 最後に設置したレールの一つ前へ戻ろうとした場合は、
    // 既設レールへの移動ではなく最後のレールのUndoとして扱う。
    auto& railMap = railPath_->GetRailMap();
    const auto& railUndoStack = railPath_->GetRailUndoStack();
    if (!railUndoStack.empty()) {
        const std::pair<int32_t, int32_t> previousRail = railUndoStack.size() >= 2
            ? railUndoStack[railUndoStack.size() - 2]
            : railMap.back();
        if (nextX == previousRail.first && nextZ == previousRail.second) {
            TryUndoLastRail();
            return;
        }
    }

    // 既にレールがあるマスへは移動しない
    for (auto& rail : railMap) {
        if (rail.first == nextX && rail.second == nextZ) {
            Logger::GetInstance().Infof(
                LogCategory::Game,
                "RailBuilder: 既設レールのため移動を中止しました ({}, {})",
                nextX, nextZ);
            return;
        }
    }
    for (auto& rail : railUndoStack) {
        if (rail.first == nextX && rail.second == nextZ) {
            Logger::GetInstance().Infof(
                LogCategory::Game,
                "RailBuilder: 既設レールのため移動を中止しました ({}, {})",
                nextX, nextZ);
            return;
        }
    }

    // 画面に映らないマスへは進ませない。生成が先に走ってしまわないよう、
    // マップを延ばす CreateToX より前に断ること。
    if (!IsCellInsideScreen(nextX, nextZ)) {
        if (!isScreenLimited_) {
            isScreenLimited_ = true;
            Logger::GetInstance().Infof(
                LogCategory::Game,
                "RailBuilder: 画面外になるため移動を中止しました ({}, {})",
                nextX, nextZ);
        }
        return;
    }
    isScreenLimited_ = false;

    mapGenerator_->CreateToX(static_cast<std::size_t>(nextX) + 1);
    const MapChipType mapChip = mapGenerator_->GetMapChip(
        static_cast<std::size_t>(nextX), static_cast<std::size_t>(nextZ));

    // 駅本体・空白などの建設不可マスを除き、岩にも即時にレールを敷設する。
    const bool isRock = mapChip == MapChipType::Resource;
    if (!mapGenerator_->CanConnectRail(gridPosX_, gridPosZ_, nextX, nextZ)) {
        Logger::GetInstance().Infof(
            LogCategory::Game,
            "RailBuilder: このマス・方向にはレールを接続できません ({}, {})",
            nextX, nextZ);
        return;
    }

    // 常設レールは走行経路へつなぐだけなので、敷設コスト・Undo時の返却は0。
    const bool isStationRail = mapGenerator_->IsStationRailCell(
        static_cast<std::size_t>(nextX), static_cast<std::size_t>(nextZ));
    const float railCost = isStationRail
        ? 0.0f : std::max(0.0f, GameSettings::RailStaminaCost.Get());
    float baseCost = railCost;
    if (mapChip == MapChipType::Water) {
        baseCost += std::max(0.0f, GameSettings::BridgeStaminaCost.Get());
    } else if (mapChip == MapChipType::Resource) {
        baseCost += std::max(0.0f, GameSettings::RockStaminaCost.Get());
    }
    const float staminaCost = hunger_->CalculateActionCost(baseCost);
    // スタミナの消費をレール登録より先に行う。これにより、通常レールも
    // 岩・橋と同じく、スタミナ不足時は失敗音を鳴らして何も敷設しない。
    if (!hunger_->TryConsumeStamina(staminaCost)) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "RailBuilder: スタミナ不足です (必要={}, 現在={})",
            staminaCost, hunger_->GetCurrentHunger());
        NotifyStaminaInsufficient();
        return;
    }

    const float refundableCost = isRock
        ? hunger_->CalculateActionCost(railCost)
        : staminaCost;
    // 岩を壊し始める時点で、通常のレールと同じく走行経路へ登録する。
    if (!railPath_->PlaceRail(nextX, nextZ, refundableCost)) {
        // PlaceRail が失敗した場合は、先に消費したスタミナを戻す。
        hunger_->AddStamina(staminaCost);
        return;
    }

    if (IsAdjacentToBananaTree(mapGenerator_, nextX, nextZ)) {
        PlayRandomPitchSe(GetOwner(), kBananaBuildSePath);
    } else {
        OnBuildSE_();
    }

    gridPosX_ = nextX;
    gridPosZ_ = nextZ;

    if (isRock) {
        const bool wasBreakingRock = isBreakingRock_;
        isBreakingRock_ = true;
        isCursorAboveRock_ = true;
        rockBreakQueue_.push_back({ gridPosX_, gridPosZ_ });
        if (!wasBreakingRock) {
            trainMovement_->SetRockBreakPaused(true);
        }
        SyncTransformToGrid();

        if (!wasBreakingRock) {
            StartNextRockThrow();
        }

        Logger::GetInstance().Infof(
            LogCategory::Game,
            "RailBuilder: 岩破壊命令を追加しました ({}, {}), スタミナコスト={}, 待機数={}",
            gridPosX_, gridPosZ_, staminaCost, rockBreakQueue_.size());
        return;
    }

    // 通常マスへの敷設ではカーソルを通常高さにする。
    isCursorAboveRock_ = false;
    SyncTransformToGrid();

    Logger::GetInstance().Infof(
        LogCategory::Game,
        "Rail placed at ({}, {})",
        gridPosX_, gridPosZ_);
}

void GameComponents::RailBuilderComponent::LateUpdate() {
    SyncTransformToGrid();
    if (transform_) {
        transform_->Get().TransferMatrix();
    }
}

bool GameComponents::RailBuilderComponent::TryUndoLastRail() {
    if (isBreakingRock_) {
        return false;
    }

    const RailUndoResult undo = railPath_->UndoLastRailPlacement();
    if (!undo.succeeded) {
        return false;
    }

    hunger_->AddStamina(undo.refundAmount);
    gridPosX_ = undo.builderPosition.first;
    gridPosZ_ = undo.builderPosition.second;
    SyncTransformToGrid();

    Logger::GetInstance().Infof(
        LogCategory::Game,
        "Rail removed at ({}, {}); builder returned to ({}, {}), refund={}",
        undo.removedPosition.first, undo.removedPosition.second,
        gridPosX_, gridPosZ_, undo.refundAmount);

    OnUndoSE_();
    return true;
}

void GameComponents::RailBuilderComponent::StartNextRockThrow() {
    if (rockBreakQueue_.empty()) {
        return;
    }

    const RockBreakRequest& request = rockBreakQueue_.front();
    Vector3 throwStart = trainMovement_->GetWorldPosition();
    throwStart.y += rockThrowStartHeight_;
    const Vector3 impactPosition{
        static_cast<float>(request.gridX) * gridSize_,
        rockImpactHeight_,
        static_cast<float>(request.gridZ) * gridSize_
    };

    trainMovement_->PlayRockThrowJump();
    if (!rockThrow_->Play(
            throwStart, impactPosition, [this]() { CompleteRockBreak(); })) {
        CompleteRockBreak();
        return;
    }
    PlayRandomPitchSe(GetOwner(), kRockThrowSePath);
}

void GameComponents::RailBuilderComponent::CompleteRockBreak() {
    if (!isBreakingRock_ || rockBreakQueue_.empty()) {
        return;
    }

    const RockBreakRequest completed = rockBreakQueue_.front();
    rockBreakQueue_.pop_front();
    mapGenerator_->SetMapChip(
        static_cast<std::size_t>(completed.gridX),
        static_cast<std::size_t>(completed.gridZ),
        MapChipType::Ground);
    GameResultData::AddBrokenRockCount();

    PlayRandomPitchSe(GetOwner(), kRockBreakSePath);

    // 岩が砕けた瞬間にカメラを揺らす。強さは Game.CameraShake.RockBreak.* で調整する。
    RockBreakShakeSettingsComponent::PlayRockBreak();

    // 同じ瞬間に、壊したマスの周りへ破片を散らす（投石の着弾位置と同じ場所）。
    PlayRockBreakDebris({
        static_cast<float>(completed.gridX) * gridSize_,
        rockImpactHeight_,
        static_cast<float>(completed.gridZ) * gridSize_ });

    Logger::GetInstance().Infof(
        LogCategory::Game,
        "RailBuilder: 岩を破壊して地面にしました ({}, {}), 残り待機数={}",
        completed.gridX, completed.gridZ, rockBreakQueue_.size());

    isCursorAboveRock_ = false;

    if (!rockBreakQueue_.empty()) {
        StartNextRockThrow();
        return;
    }

    isBreakingRock_ = false;
    trainMovement_->SetRockBreakPaused(false);
    SyncTransformToGrid();
}

void GameComponents::RailBuilderComponent::SyncTransformToGrid() {
    if (!transform_) {
        return;
    }

    transform_->Get().translate.x = static_cast<float>(gridPosX_) * gridSize_;
    transform_->Get().translate.z = static_cast<float>(gridPosZ_) * gridSize_;

    float cursorHeight = height_;
    const MapChipType mapChip = mapGenerator_
        ? mapGenerator_->GetMapChip(
            static_cast<std::size_t>(gridPosX_), static_cast<std::size_t>(gridPosZ_))
        : MapChipType::Ground;
    if (mapChip == MapChipType::Station) {
        // 駅は1ブロックより背が高いため、屋根の上にも通常の浮き幅を確保する。
        const float stationTop = BlockModelLayout::GetSurfaceHeight(gridSize_) +
            BlockModelLayout::kStationModelHeight * BlockModelLayout::GetScale(gridSize_);
        const float clearance = std::max(
            0.0f, height_ - BlockModelLayout::GetSurfaceHeight(gridSize_));
        cursorHeight = std::max(height_ + gridSize_, stationTop + clearance);
    }
    if (mapChip == MapChipType::Resource || isCursorAboveRock_) {
        cursorHeight = std::max(
            cursorHeight, height_ + std::max(gridSize_, rockCursorHeightOffset_));
    }
    if (trainMovement_) {
        // 駅と車両が同じマスにあっても加算せず、必要な高さの最大値を使う。
        cursorHeight = std::max(cursorHeight, height_ +
            trainMovement_->GetCursorHeightOffsetAt(
                transform_->Get().translate.x, transform_->Get().translate.z));
    }
    transform_->Get().translate.y = cursorHeight;
}

// ===== 画面外への移動を止める =====
// ゲーム視点のカメラは列車とカーソルの中点を写す（Presets/CameraRigs/GamePlay.json）。
// カーソルが先へ行くほど中点も先へ動くので、置いていかれた列車は画面の左へ押し出される。
// 止めるのは操作しているカーソルだけで、列車は遅れて写らなくなっても構わない。
//
// 判定にはリグが決めた「今のカメラ」をそのまま使う。構図を変えれば止まる位置も追従する。
// 減衰でカメラが遅れている間は止まる位置も手前になり、カメラが追いつくにつれてまた
// 進めるようになる。長押しでカメラを置き去りにして画面外へ抜けられないのはこのため。
// カメラが追いつく向きはカーソルを画面の内側へ戻す向きなので、いったん通した移動が
// 後から画面外になることはない。
bool GameComponents::RailBuilderComponent::IsInsideScreen(
    const Vector3& worldPosition) const {
    // カメラが分からないときは制限しない。判定できないことを理由に遊べなくしない。
    if (!viewCamera_) {
        return true;
    }

    // MathCore::Coordinate::WorldToNormalizedScreen は画面の実サイズを要求するうえ、
    // カメラの後ろの点を画面内へ折り返してしまう。ここは NDC が要るだけなので、
    // エディタの CameraIconOverlay と同じく w を見ながら自分で割る。
    const Matrix4x4 viewProjection =
        viewCamera_->GetViewMatrix() * viewCamera_->GetProjectionMatrix();
    const auto toClip = [&viewProjection](const Vector3& position) {
        return MathCore::CoordinateTransform::TransformCoord(
            Vector4{ position.x, position.y, position.z, 1.0f }, viewProjection);
    };

    const Vector4 clip = toClip(worldPosition);
    // w <= 0 はカメラの後ろ。割ると符号が反転して、背後の点が画面内に見えてしまう。
    if (clip.w <= 1.0e-5f) {
        return false;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;

    // カーソルの画面上の半径を、その場のカメラで測る。同じ大きさでも遠ければ小さく
    // 写るので、割合を決め打ちにせず、カメラの右・上へ半径ぶん動かした点を射影して
    // 画面上でどれだけ離れるかを見る。カメラが引けば止まる位置も自動で端へ寄る。
    const float radius = gridSize_ * std::max(0.0f, cursorEdgeRadiusRatio_);
    float halfX = 0.0f;
    float halfY = 0.0f;
    if (radius > 0.0f) {
        const Vector4 rightClip = toClip(worldPosition + viewCamera_->GetRight() * radius);
        if (rightClip.w > 1.0e-5f) {
            halfX = std::abs(rightClip.x / rightClip.w - ndcX);
        }
        const Vector4 upClip = toClip(worldPosition + viewCamera_->GetUp() * radius);
        if (upClip.w > 1.0e-5f) {
            halfY = std::abs(upClip.y / upClip.w - ndcY);
        }
    }

    // 射影行列のアスペクト比は基準解像度に固定されている（Camera::ResolveAspectRatio）。
    // NDC の ±1 がそのままレターボックス後の表示領域の端になるので、
    // 端に外形が触れるところがそのまま限界になる。
    return std::abs(ndcX) + halfX <= 1.0f && std::abs(ndcY) + halfY <= 1.0f;
}

bool GameComponents::RailBuilderComponent::IsCellInsideScreen(
    int32_t gridX, int32_t gridZ) const {
    if (!transform_) {
        return true;
    }

    // 高さは今のカーソルと同じとみなす（隣のマスで大きくは変わらない）。
    return IsInsideScreen({
        static_cast<float>(gridX) * gridSize_,
        transform_->Get().translate.y,
        static_cast<float>(gridZ) * gridSize_ });
}

void GameComponents::RailBuilderComponent::SetGridSize(float size) {
    gridSize_ = size;  
}

void GameComponents::RailBuilderComponent::SetHorizontalPrioritize(bool prioritize) {
    HorizontalPrioritize = prioritize;
}

void GameComponents::RailBuilderComponent::SetViewCamera(Camera* camera) {
    viewCamera_ = camera;
}

float GameComponents::RailBuilderComponent::GetNextPlacementCost() const {
    if (!mapGenerator_ || !hunger_) {
        return 0.0f;
    }

    // 入力が来るまで進む向きは決まらないので、優先方向の 1 マス先を見せる。
    // 未生成のマスは GetMapChip が Void を返し、通常の地面と同じ扱いになる。
    const int32_t nextX = gridPosX_ + (HorizontalPrioritize ? 1 : 0);
    const int32_t nextZ = gridPosZ_ + (HorizontalPrioritize ? 0 : 1);
    if (nextX < 0 || nextZ < 0) {
        return 0.0f;
    }

    const auto x = static_cast<std::size_t>(nextX);
    const auto z = static_cast<std::size_t>(nextZ);

    const float railCost = mapGenerator_->IsStationRailCell(x, z)
        ? 0.0f : std::max(0.0f, GameSettings::RailStaminaCost.Get());
    float baseCost = railCost;

    const MapChipType mapChip = mapGenerator_->GetMapChip(x, z);
    if (mapChip == MapChipType::Water) {
        baseCost += std::max(0.0f, GameSettings::BridgeStaminaCost.Get());
    } else if (mapChip == MapChipType::Resource) {
        baseCost += std::max(0.0f, GameSettings::RockStaminaCost.Get());
    }

    return hunger_->CalculateActionCost(baseCost);
}

void GameComponents::RailBuilderComponent::SetInsufficientFeedback(
    std::function<void()> onStaminaInsufficient) {
    OnStaminaInsufficient_ = std::move(onStaminaInsufficient);
}

void GameComponents::RailBuilderComponent::NotifyStaminaInsufficient() {
    if (OnStaminaInsufficient_) {
        OnStaminaInsufficient_();
    }
    if (OnFailureSE_) {
        OnFailureSE_();
    }
}
