#include "pch.h"
#include "WaterWaveViewComponent.h"

#include "Camera/Camera.h"
#include "GameObject/GameObject.h"
#include "GameObject/Component/Render/TileWaterComponent.h"
#include "MapGeneratorComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "Components/Utility/GameCamera.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

using namespace CoreEngine;

GameComponents::WaterWaveViewComponent::WaterWaveViewComponent(
    MapGeneratorComponent* mapGenerator,
    float gridSize,
    uint32_t viewDistanceX,
    std::size_t initialCapacity)
    : gridSize_(gridSize),
      viewDistanceX_(viewDistanceX),
      initialCapacity_(initialCapacity),
      mapGenerator_(mapGenerator) {
}

REFLECT_DEFINE_BEGIN(GameComponents::WaterWaveViewComponent, "水面の波")
    REFLECT_PARTIAL()
    REFLECT_OBJECT_REF(mapGenerator_, "マップ生成")
REFLECT_DEFINE_END()
REFLECT_REGISTER(GameComponents::WaterWaveViewComponent)

json GameComponents::WaterWaveViewComponent::OnSerialize() const {
    return {
        { "gridSize", gridSize_ },
        { "viewDistanceX", viewDistanceX_ },
        { "waveHeightScale", waveHeightScale_ },
        { "waveSpeedScale", waveSpeedScale_ },
        { "waterLevelRatio", waterLevelRatio_ },
        { "waterRoughness", waterRoughness_ },
        { "waterColor", JsonManager::Vector4ToJson(waterColor_) },
        { "fallEnabled", fallEnabled_ },
        { "fallLengthRatio", fallLengthRatio_ },
        { "fallFlowSpeed", fallFlowSpeed_ },
        { "fallFoamStrength", fallFoamStrength_ }
    };
}

void GameComponents::WaterWaveViewComponent::OnDeserialize(const json& j) {
    gridSize_ = std::max(0.01f, JsonManager::SafeGet<float>(j, "gridSize", gridSize_));
    viewDistanceX_ = std::max<uint32_t>(1,
        JsonManager::SafeGet<uint32_t>(j, "viewDistanceX", viewDistanceX_));
    waveHeightScale_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "waveHeightScale", waveHeightScale_));
    waveSpeedScale_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "waveSpeedScale", waveSpeedScale_));
    waterLevelRatio_ = std::clamp(
        JsonManager::SafeGet<float>(j, "waterLevelRatio", waterLevelRatio_), 0.0f, 1.0f);
    waterColor_ = JsonManager::SafeGetVector4(j, "waterColor", waterColor_);
    waterRoughness_ = std::clamp(
        JsonManager::SafeGet<float>(j, "waterRoughness", waterRoughness_), 0.0f, 1.0f);
    fallEnabled_ = JsonManager::SafeGet<bool>(j, "fallEnabled", fallEnabled_);
    fallLengthRatio_ = std::max(0.01f,
        JsonManager::SafeGet<float>(j, "fallLengthRatio", fallLengthRatio_));
    fallFlowSpeed_ = std::max(0.0f,
        JsonManager::SafeGet<float>(j, "fallFlowSpeed", fallFlowSpeed_));
    fallFoamStrength_ = std::clamp(
        JsonManager::SafeGet<float>(j, "fallFoamStrength", fallFoamStrength_), 0.0f, 4.0f);
    ApplySettingsToTiles();
}

#ifdef USE_IMGUI
bool GameComponents::WaterWaveViewComponent::DrawInspector() {
    bool changed = false;
    changed |= ImGui::DragFloat("波の高さ", &waveHeightScale_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("波の速さ", &waveSpeedScale_, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat("水面の高さ", &waterLevelRatio_, 0.005f, 0.0f, 1.0f);

    // || で繋ぐと短絡して後ろのウィジェットが描かれなくなるので、必ず別々に呼ぶ
    const bool colorEdited = ImGui::ColorEdit4("色", &waterColor_.x);
    const bool roughnessEdited =
        ImGui::DragFloat("粗さ", &waterRoughness_, 0.005f, 0.0f, 1.0f);
    changed |= colorEdited || roughnessEdited;

    ImGui::SeparatorText("マップ端の滝");
    changed |= ImGui::Checkbox("滝を出す", &fallEnabled_);
    changed |= ImGui::DragFloat("落差（マス）", &fallLengthRatio_, 0.05f, 0.5f, 40.0f);
    changed |= ImGui::DragFloat("流れの速さ", &fallFlowSpeed_, 0.02f, 0.0f, 20.0f);

    if (ImGui::DragFloat("泡の強さ", &fallFoamStrength_, 0.01f, 0.0f, 4.0f)) {
        changed = true;
    }

    if (changed) {
        ApplySettingsToTiles();
    }
    if (tiles_) {
        ImGui::TextDisabled("板: 水面 %zu 枚 / 滝 %zu 枚",
            tiles_->GetSurfaceTileCount(), tiles_->GetFallTileCount());
    }
    return changed;
}
#endif

void GameComponents::WaterWaveViewComponent::Awake() {
    GameObject* owner = GetOwner();
    tiles_ = owner ? owner->AddComponent<TileWaterComponent>(initialCapacity_) : nullptr;
    if (!tiles_) {
        Logger::GetInstance().Errorf(
            LogCategory::Game,
            "WaterWaveViewComponent: 水面の板を描くコンポーネントを付けられませんでした");
        SetEnabled(false);
        return;
    }
    ApplySettingsToTiles();
}

void GameComponents::WaterWaveViewComponent::Update() {
    // 描画範囲はゲーム視点カメラの位置から決める（MapViewComponent と同じ基準）
    CoreEngine::Camera* viewCamera = FindGameCamera(GetOwner());
    if (!mapGenerator_ || viewCamera == nullptr || !tiles_) {
        return;
    }

    // 描画範囲の決め方は MapViewComponent と揃える（地面と水がずれた範囲で出ないように）
    const auto cameraFocusPosition = viewCamera->GetTranslate();
    const float cameraFocusGridX = std::round(cameraFocusPosition.x / gridSize_);
    const uint32_t viewCenterX = cameraFocusGridX > 0.0f
        ? static_cast<uint32_t>(cameraFocusGridX)
        : 0;

    const std::size_t startX = (viewCenterX > viewDistanceX_)
        ? (viewCenterX - viewDistanceX_)
        : 0;
    const std::size_t endX = viewCenterX + viewDistanceX_;

    // カメラの先に必要な分だけ、X正方向へマップを延長する
    mapGenerator_->CreateToX(endX);

    const auto& mapChips = mapGenerator_->GetMapChips();
    for (std::size_t x = startX; x < endX && x < mapChips.size(); ++x) {
        const std::size_t zCount = mapChips[x].size();
        for (std::size_t z = 0; z < zCount; ++z) {
            if (mapGenerator_->GetMapChip(x, z) != MapChipType::Water) {
                continue;
            }
            const float worldX = x * gridSize_;
            tiles_->DrawSurface(worldX, z * gridSize_);

            if (!fallEnabled_) {
                continue;
            }
            // マップは Z 方向へ広がらない帯で、両端の外は地面すら無い空間。
            // そこに面した水マスからカーテンを垂らす。X 方向はカメラの先へ
            // 延び続けるので端が存在せず、ここでは見ない。
            // 幅 1 マスのマップでは両方に該当するため、else で繋がないこと。
            if (z == 0) {
                tiles_->DrawFall(worldX, -0.5f * gridSize_, true);
            }
            if (z + 1 == zCount) {
                tiles_->DrawFall(worldX, (z + 0.5f) * gridSize_, false);
            }
        }
    }
}

void GameComponents::WaterWaveViewComponent::ApplySettingsToTiles() {
    if (!tiles_) {
        return;
    }
    tiles_->SetTileSize(gridSize_);
    tiles_->SetSurfaceHeight(GetWaterSurfaceHeight());
    tiles_->SetWaveHeightScale(waveHeightScale_);
    tiles_->SetWaveSpeedScale(waveSpeedScale_);
    tiles_->SetWaterMaterial(waterColor_, waterRoughness_);
    tiles_->SetFallLengthRatio(fallLengthRatio_);
    tiles_->SetFallFlowSpeed(fallFlowSpeed_);
    tiles_->SetFallFoamStrength(fallFoamStrength_);
}

float GameComponents::WaterWaveViewComponent::GetWaterSurfaceHeight() const {
    // 地面ブロックは [底面, 底面+1マス] を占める。その途中に静止水面を置く。
    return BlockModelLayout::GetGroundHeight(gridSize_) + waterLevelRatio_ * gridSize_;
}
