#include "pch.h"
#include "RailViewComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Components/Rail/RailPathComponent.h"
#include "Components/Building/MapGeneratorComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "Components/Utility/ModelRenderPoolComponent.h"
#include "Camera/Camera.h"
#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include "Graphics/Line/LineManager.h"


using namespace CoreEngine;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

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

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

json GameComponents::RailViewComponent::OnSerialize() const {
    return {
        { "gridSize", gridSize_ },
        { "viewDistanceX", viewDistanceX_ },
        { "jumpHeight", railJumpHeight_ },
        { "jumpDuration", railJumpDuration_ },
        { "bananaBuildRotationTurns", bananaBuildRotationTurns_ },
        { "staggerInterval", confirmationStaggerInterval_ },
        { "seVolume", confirmationSeVolume_ },
        { "seBasePitch", confirmationSeBasePitch_ },
        { "sePitchStep", confirmationSePitchStep_ },
        { "seMaxPitch", confirmationSeMaxPitch_ }
    };
}

void GameComponents::RailViewComponent::OnDeserialize(const json& j) {
    gridSize_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "gridSize", gridSize_));
    viewDistanceX_ = std::max<uint32_t>(1, JsonManager::SafeGet<uint32_t>(j, "viewDistanceX", viewDistanceX_));
    railJumpHeight_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "jumpHeight", railJumpHeight_));
    railJumpDuration_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "jumpDuration", railJumpDuration_));
    bananaBuildRotationTurns_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "bananaBuildRotationTurns", bananaBuildRotationTurns_));
    confirmationStaggerInterval_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "staggerInterval", confirmationStaggerInterval_));
    confirmationSeVolume_ = std::clamp(JsonManager::SafeGet<float>(j, "seVolume", confirmationSeVolume_), 0.0f, 1.0f);
    confirmationSeBasePitch_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "seBasePitch", confirmationSeBasePitch_));
    confirmationSePitchStep_ = std::max(0.0f, JsonManager::SafeGet<float>(j, "sePitchStep", confirmationSePitchStep_));
    confirmationSeMaxPitch_ = std::max(confirmationSeBasePitch_, JsonManager::SafeGet<float>(j, "seMaxPitch", confirmationSeMaxPitch_));
}

#ifdef USE_IMGUI
bool GameComponents::RailViewComponent::DrawInspector() {
    bool changed = false;
    changed |= ImGui::DragFloat("グリッドサイズ", &gridSize_, 0.05f, 0.01f, 20.0f);
    int distance = static_cast<int>(viewDistanceX_);
    if (ImGui::DragInt("描画距離X", &distance, 1.0f, 1, 500)) { viewDistanceX_ = static_cast<uint32_t>(std::max(distance, 1)); changed = true; }
    ImGui::TextDisabled("共通モデルスケール: %.3f", BlockModelLayout::GetScale(gridSize_));
    ImGui::TextDisabled("レール底面の高さ: %.3f", BlockModelLayout::GetSurfaceHeight(gridSize_));
    changed |= ImGui::DragFloat("設置ジャンプ高さ", &railJumpHeight_, 0.01f, 0.0f, 10.0f);
    changed |= ImGui::DragFloat("設置ジャンプ時間", &railJumpDuration_, 0.01f, 0.01f, 10.0f);
    changed |= ImGui::DragFloat("バナナ隣接時の設置回転（周）",
        &bananaBuildRotationTurns_, 0.1f, 0.0f, 4.0f);
    changed |= ImGui::DragFloat("確定SEの時間差", &confirmationStaggerInterval_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::SliderFloat("確定SE音量", &confirmationSeVolume_, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("確定SE基準ピッチ", &confirmationSeBasePitch_, 0.01f, 0.01f, 4.0f);
    changed |= ImGui::DragFloat("確定SEピッチ増分", &confirmationSePitchStep_, 0.01f, 0.0f, 4.0f);
    changed |= ImGui::DragFloat("確定SE最大ピッチ", &confirmationSeMaxPitch_, 0.01f, confirmationSeBasePitch_, 4.0f);
    return changed;
}
#endif

void GameComponents::RailViewComponent::Start() {
    transform_ = Sibling<TransformComponent>();

    // ゲーム開始時から存在する始点レールは演出の対象にしない。
    if (railPath_) {
        railJumpTimes_.assign(
            railPath_->GetRailMap().size() + railPath_->GetRailUndoStack().size(),
            railJumpDuration_);
        confirmationSoundTimes_.assign(
            railPath_->GetRailMap().size(),
            railJumpDuration_);
        confirmationSoundPitches_.assign(
            railPath_->GetRailMap().size(),
            confirmationSeBasePitch_);
    }
}

void GameComponents::RailViewComponent::Update() {
    // TransformComponent がアタッチされていない場合は処理を中断する
    if (!transform_) {
        return;
    }
    // RailPathComponent がアタッチされていない場合は処理を中断する
    if (!railPath_) {
        return;
    }

    UpdateRailJumpAnimations(Time::DeltaTime());
    UpdateConfirmationSounds(Time::DeltaTime());
    DrawRailModels();
}

void GameComponents::RailViewComponent::UpdateRailJumpAnimations(float deltaTime) {
    // 確定済みと未確定を連結した経路。レールは置かれた順に並び、列車が通って
    // 確定しても添字は変わらないため、設置した瞬間の演出をそのまま持ち越せる。
    // 増えた分は 0 から動き出し、Undo で減った分は末尾ごと捨てる。
    railJumpTimes_.resize(
        railPath_->GetRailMap().size() + railPath_->GetRailUndoStack().size(),
        0.0f);

    const float safeDeltaTime = std::max(deltaTime, 0.0f);
    for (float& animationTime : railJumpTimes_) {
        animationTime = std::min(animationTime + safeDeltaTime, railJumpDuration_);
    }
}

float GameComponents::RailViewComponent::GetRailJumpOffset(
    std::size_t pathIndex) const {
    if (pathIndex >= railJumpTimes_.size()) {
        return 0.0f;
    }

    const float animationTime = railJumpTimes_[pathIndex];
    if (animationTime >= railJumpDuration_) {
        return 0.0f;
    }

    const float progress = std::clamp(
        animationTime / railJumpDuration_,
        0.0f,
        1.0f);
    return std::sin(progress * kPi) * railJumpHeight_;
}

float GameComponents::RailViewComponent::GetBananaBuildRotation(
    std::size_t pathIndex, int32_t gridX, int32_t gridZ) const {
    if (pathIndex >= railJumpTimes_.size() || bananaBuildRotationTurns_ <= 0.0f ||
        !IsAdjacentToBananaTree(mapGenerator_, gridX, gridZ)) {
        return 0.0f;
    }

    const float animationTime = railJumpTimes_[pathIndex];
    if (animationTime >= railJumpDuration_) {
        return 0.0f;
    }

    const float progress = std::clamp(
        animationTime / railJumpDuration_,
        0.0f,
        1.0f);
    // ジャンプと一緒に回り始め、最後は元のレール向きへ滑らかに戻す。
    const float easedProgress = progress * progress * (3.0f - 2.0f * progress);
    return easedProgress * bananaBuildRotationTurns_ * 2.0f * kPi;
}

void GameComponents::RailViewComponent::UpdateConfirmationSounds(float deltaTime) {
    const std::size_t confirmedCount = railPath_->GetRailMap().size();

    // 将来確定済みレールを巻き戻す処理が追加されても、添字を範囲内に保つ。
    if (confirmationSoundTimes_.size() > confirmedCount) {
        confirmationSoundTimes_.resize(confirmedCount);
        confirmationSoundPitches_.resize(confirmedCount);
    }

    // 同じフレームに複数本確定した場合（駅到達時）は、順番に再生する。
    const std::size_t firstNewIndex = confirmationSoundTimes_.size();
    for (std::size_t i = firstNewIndex; i < confirmedCount; ++i) {
        const float delay = confirmationStaggerInterval_ *
            static_cast<float>(i - firstNewIndex);
        confirmationSoundTimes_.push_back(-delay);
        confirmationSoundPitches_.push_back(std::min(
            confirmationSeBasePitch_ +
                confirmationSePitchStep_ * static_cast<float>(i - firstNewIndex),
            confirmationSeMaxPitch_));
    }

    const float safeDeltaTime = std::max(deltaTime, 0.0f);
    for (std::size_t i = 0; i < confirmationSoundTimes_.size(); ++i) {
        float& animationTime = confirmationSoundTimes_[i];
        if (animationTime < railJumpDuration_) {
            const float previousTime = animationTime;
            animationTime = std::min(
                animationTime + safeDeltaTime,
                railJumpDuration_);

            // 待ち時間を越えてレールが確定した瞬間に、一度だけSEを鳴らす。
            const auto& rail = railPath_->GetRailMap()[i];
            const bool isStationRail = mapGenerator_ && mapGenerator_->IsStationRailCell(
                static_cast<std::size_t>(rail.first), static_cast<std::size_t>(rail.second));
            if (previousTime <= 0.0f && animationTime > 0.0f && onRailBuildSE_) {
                onRailBuildSE_(
                    confirmationSeVolume_, confirmationSoundPitches_[i], isStationRail);
            }
        }
    }
}

void GameComponents::RailViewComponent::DrawRailModels() {
    if (!railPool_ || !railLeftPool_ || !railRightPool_ ||
        !bridgePool_ || !mapGenerator_) {
        return;
    }

    using GridPosition = std::pair<int32_t, int32_t>;

    // 確定・未確定を連結し、1本の経路としてモデルの形状を判定する。
    const auto& confirmedRails = railPath_->GetRailMap();
    const auto& pendingRails = railPath_->GetRailUndoStack();
    std::vector<GridPosition> railPath;
    railPath.reserve(confirmedRails.size() + pendingRails.size());
    railPath.insert(railPath.end(), confirmedRails.begin(), confirmedRails.end());
    railPath.insert(railPath.end(), pendingRails.begin(), pendingRails.end());

    int32_t minVisibleX = 0;
    int32_t maxVisibleX = (std::numeric_limits<int32_t>::max)();
    if (viewCamera_ && gridSize_ > 0.0f) {
        const float centerGridX =
            viewCamera_->GetTranslate().x / gridSize_;
        minVisibleX = std::max(
            0,
            static_cast<int32_t>(std::floor(centerGridX)) -
                static_cast<int32_t>(viewDistanceX_));
        maxVisibleX =
            static_cast<int32_t>(std::ceil(centerGridX)) +
            static_cast<int32_t>(viewDistanceX_);
    }

    const auto directionBetween = [](const GridPosition& from, const GridPosition& to) {
        return GridPosition{ to.first - from.first, to.second - from.second };
    };
    const auto yawFromDirection = [](const GridPosition& direction) {
        // レールモデルの基準方向が+Zなので、+Zを0ラジアンとしてY軸回転を求める。
        return std::atan2(
            static_cast<float>(direction.first),
            static_cast<float>(direction.second));
    };

    const float modelScale = BlockModelLayout::GetScale(gridSize_);
    const Vector3 scale{ modelScale, modelScale, modelScale };
    const float railHeight = BlockModelLayout::GetSurfaceHeight(gridSize_);
    const float bridgeHeight = BlockModelLayout::GetBridgeHeight(gridSize_);

    // 未接続の駅前レールは横向きに表示する。接続後の形状は通常経路から決める。
    const auto& mapChips = mapGenerator_->GetMapChips();
    const std::size_t stationRailEndX = std::min(
        mapChips.size(), static_cast<std::size_t>(maxVisibleX) + 1);
    for (std::size_t x = static_cast<std::size_t>(minVisibleX); x < stationRailEndX; ++x) {
        for (std::size_t z = 0; z < mapChips[x].size(); ++z) {
            const GridPosition stationRail{ static_cast<int32_t>(x), static_cast<int32_t>(z) };
            if (mapGenerator_->IsStationRailCell(x, z) &&
                std::find(railPath.begin(), railPath.end(), stationRail) == railPath.end()) {
                railPool_->Draw(
                    { static_cast<float>(x) * gridSize_, railHeight, static_cast<float>(z) * gridSize_ },
                    { 0.0f, kPi * 0.5f, 0.0f }, scale);
            }
        }
    }

    for (std::size_t i = 0; i < railPath.size(); ++i) {
        const GridPosition current = railPath[i];
        if (current.first < minVisibleX || current.first > maxVisibleX) {
            continue;
        }
        const bool hasPrevious = i > 0;
        const bool hasNext = i + 1 < railPath.size();
        // 接続先がまだないゲーム開始時の始点レールは右方向（+X）を向ける。
        GridPosition incoming = { 1, 0 };
        GridPosition outgoing = { 1, 0 };

        if (hasPrevious) {
            incoming = directionBetween(railPath[i - 1], current);
        }
        if (hasNext) {
            outgoing = directionBetween(current, railPath[i + 1]);
        }
        if (!hasPrevious && hasNext) {
            incoming = outgoing;
        } else if (hasPrevious && !hasNext) {
            outgoing = incoming;
        }

        const float jumpOffset = GetRailJumpOffset(i);
        const float bananaBuildRotation =
            GetBananaBuildRotation(i, current.first, current.second);
        const Vector3 position = {
            static_cast<float>(current.first) * gridSize_,
            railHeight + jumpOffset,
            static_cast<float>(current.second) * gridSize_
        };

        // 水チップはそのまま描画し、地面と同じ高さへ橋モデルを追加する。
        if (current.first >= 0 && current.second >= 0 &&
            mapGenerator_->GetMapChip(
                static_cast<std::size_t>(current.first),
                static_cast<std::size_t>(current.second)) == MapChipType::Water) {
            bridgePool_->Draw(
                {
                    static_cast<float>(current.first) * gridSize_,
                    bridgeHeight,
                    static_cast<float>(current.second) * gridSize_
                },
                { 0.0f, yawFromDirection(outgoing), 0.0f },
                scale);
        }

        // XZ平面の外積。正なら進行方向に対して左折、負なら右折。
        const int32_t turn =
            incoming.first * outgoing.second -
            incoming.second * outgoing.first;
        if (hasPrevious && hasNext && turn > 0) {
            railLeftPool_->Draw(
                position, { 0.0f, yawFromDirection(incoming) + bananaBuildRotation, 0.0f }, scale);
        } else if (hasPrevious && hasNext && turn < 0) {
            railRightPool_->Draw(
                position, { 0.0f, yawFromDirection(incoming) + bananaBuildRotation, 0.0f }, scale);
        } else {
            railPool_->Draw(
                position, { 0.0f, yawFromDirection(outgoing) + bananaBuildRotation, 0.0f }, scale);
        }
    }
}

void GameComponents::RailViewComponent::SetGridSize(float size) {
    gridSize_ = size;
}

void GameComponents::RailViewComponent::SetCenterPosition(const CoreEngine::Vector3& position) {
    transform_->Get().translate = position;
}
