#include "pch.h"
#include "RailPathComponent.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Input/InputAction.h"
#include "Input/InputManager.h"
#include "Utility/FrameRate/Time.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

GameComponents::RailPathComponent::RailPathComponent(uint32_t mapSizeZ, uint32_t startX, uint32_t startZ) :
    mapSizeZ_(mapSizeZ), startX_(startX), startZ_(startZ) {
    // 初期位置もビルダーが通ったマスとしてレールへ登録する
    if (startZ < mapSizeZ_) {
        railMap_.emplace_back(startX, startZ);
    } else {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "RailPathComponent: initial position out of bounds ({}, {})",
            startX, startZ);
    }
}

json GameComponents::RailPathComponent::OnSerialize() const {
    return {
        { "mapSizeZ", mapSizeZ_ },
        { "startX", startX_ },
        { "startZ", startZ_ }
    };
}

void GameComponents::RailPathComponent::OnDeserialize(const json& j) {
    mapSizeZ_ = std::max<uint32_t>(1, JsonManager::SafeGet<uint32_t>(j, "mapSizeZ", mapSizeZ_));
    startX_ = JsonManager::SafeGet<uint32_t>(j, "startX", startX_);
    startZ_ = std::min(JsonManager::SafeGet<uint32_t>(j, "startZ", startZ_), mapSizeZ_ - 1);
    railMap_.clear();
    railUndoStack_.clear();
    railUndoCosts_.clear();
    trainRouteQueue_.clear();
    railMap_.emplace_back(startX_, startZ_);
}

#ifdef USE_IMGUI
bool GameComponents::RailPathComponent::DrawInspector() {
    int mapSize = static_cast<int>(mapSizeZ_);
    int startX = static_cast<int>(startX_);
    int startZ = static_cast<int>(startZ_);
    bool changed = false;
    if (ImGui::DragInt("Z方向マップサイズ", &mapSize, 1.0f, 1, 100)) {
        mapSizeZ_ = static_cast<uint32_t>(std::max(mapSize, 1));
        startZ_ = std::min(startZ_, mapSizeZ_ - 1);
        changed = true;
    }
    if (ImGui::DragInt("開始X", &startX, 1.0f, 0, 500)) { startX_ = static_cast<uint32_t>(std::max(startX, 0)); changed = true; }
    if (ImGui::DragInt("開始Z", &startZ, 1.0f, 0, static_cast<int>(mapSizeZ_ - 1))) { startZ_ = static_cast<uint32_t>(std::max(startZ, 0)); changed = true; }
    ImGui::TextDisabled("確定済み: %zu / 未確定: %zu", railMap_.size(), railUndoStack_.size());
    ImGui::TextWrapped("開始位置とマップサイズはシーン再読み込み時に反映されます。");
    return changed;
}
#endif

void GameComponents::RailPathComponent::Start() {
}

void GameComponents::RailPathComponent::Update() {
}

bool GameComponents::RailPathComponent::PlaceRail(
    int32_t x, int32_t z, float refundableStaminaCost) {
    if (x >= 0 && z >= 0 && z < static_cast<int32_t>(mapSizeZ_)) {
        railUndoStack_.emplace_back(x, z);
        railUndoCosts_.push_back(refundableStaminaCost);
        trainRouteQueue_.emplace_back(x, z);
        return true;
    } else {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "PlaceRail: out of bounds");
        return false;
    }
}

GameComponents::RailUndoResult GameComponents::RailPathComponent::UndoLastRailPlacement() {
    if (railUndoStack_.empty() || railUndoCosts_.empty()) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "UndoLastRailPlacement: no rail to undo");
        return {};
    }

    RailUndoResult result;
    result.succeeded = true;
    result.removedPosition = railUndoStack_.back();
    result.refundAmount = railUndoCosts_.back();

    railUndoStack_.pop_back();
    railUndoCosts_.pop_back();

    if (!trainRouteQueue_.empty() &&
        trainRouteQueue_.back() == result.removedPosition) {
        trainRouteQueue_.pop_back();
    }

    result.builderPosition = !railUndoStack_.empty()
        ? railUndoStack_.back()
        : railMap_.back();
    return result;
}

bool GameComponents::RailPathComponent::ConfirmNextRailPlacement() {
    if (trainRouteQueue_.empty()) {
        Logger::GetInstance().Warnf(
            LogCategory::Game,
            "ConfirmNextRailPlacement: no rail to confirm");
        return false;
    }

    const auto position = trainRouteQueue_.front();

    // 駅によって既に確定済みなら、列車用キューだけを進める。
    const bool alreadyConfirmed =
        std::find(railMap_.begin(), railMap_.end(), position) != railMap_.end();
    if (!alreadyConfirmed) {
        if (railUndoStack_.empty() || railUndoStack_.front() != position) {
            Logger::GetInstance().Errorf(
                LogCategory::Game,
                "ConfirmNextRailPlacement: rail queues are inconsistent");
            return false;
        }
        railMap_.push_back(position);
        railUndoStack_.erase(railUndoStack_.begin());
        railUndoCosts_.erase(railUndoCosts_.begin());
    }

    trainRouteQueue_.erase(trainRouteQueue_.begin());
    return true;
}

void GameComponents::RailPathComponent::ConfirmAllPendingRailPlacements() {
    railMap_.insert(
        railMap_.end(), railUndoStack_.begin(), railUndoStack_.end());
    railUndoStack_.clear();
    railUndoCosts_.clear();
}

bool GameComponents::RailPathComponent::TryGetNextUnconfirmedRail(
    std::pair<int32_t, int32_t>& destination) const {
    if (trainRouteQueue_.empty()) {
        return false;
    }

    destination = trainRouteQueue_.front();
    return true;
}

std::size_t GameComponents::RailPathComponent::GetUnconfirmedRailCount() const {
    return trainRouteQueue_.size();
}

std::vector<std::pair<int32_t, int32_t>>& GameComponents::RailPathComponent::GetRailMap() {
    return railMap_;
}

uint32_t GameComponents::RailPathComponent::GetMapSizeZ() const {
    return mapSizeZ_;
}

std::size_t GameComponents::RailPathComponent::GetLaidRailCount() const {
    // railMap_ には列車の初期位置も含まれるため、長さとしては除外する。
    const std::size_t railCount = railMap_.size() + railUndoStack_.size();
    return railCount > 0 ? railCount - 1 : 0;
}

const std::vector<std::pair<int32_t, int32_t>>&
GameComponents::RailPathComponent::GetRailUndoStack() const {
    return railUndoStack_;
}
