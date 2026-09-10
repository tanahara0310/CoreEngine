#include "pch.h"
#include "ResultScene.h"

#include "Audio/AudioSystem.h"
#include "Camera/Camera.h"
#include "Camera/CameraManager.h"
#include "Components/GameCore/GameResultData.h"
#include "Components/Result/ResultTipsComponent.h"
#include "Components/Result/ResultMonkeyShakeComponent.h"
#include "Components/Result/ResultButtonAnimationComponent.h"
#include "Components/Utility/BlockModelLayout.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Scenes/GameScene/SkyFogFeature.h"
#include "Scenes/ResultScene/ResultSceneUi.h"
#include "GameObjects/GameSceneObject.h"
#include "EngineSystem/EngineSystem.h"
#include "Input/InputManager.h"
#include "Scene/SceneManager.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"
#include "Utility/FrameRate/Time.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

using namespace CoreEngine;

namespace
{
constexpr const char* kResultBgmPath = "Sounds/BGM/Result_bgm.mp3";
constexpr const char* kRailBuildSePath = "Application/Assets/Sounds/SE/rail_build.mp3";
constexpr const char* kDecisionSePath = "Sounds/SE/decision.mp3";

CVar<float> ResultMonkeyRingRadius{
    "Result.Monkey.RingRadius",
    4.5f,
    "リザルト画面でサルを配置する円形エリアの半径（サル数が多い場合は自動拡張）",
    CVarRange{ 0.0f, 30.0f } };

CVar<float> ResultCameraOrbitSpeed{
    "Result.Camera.Orbit.RotationSpeed",
    0.32f,
    "リザルトカメラの周回速度（ラジアン/秒）",
    CVarRange{ -2.0f, 2.0f } };

constexpr float kResultMonkeyY = 7.8f;
constexpr float kResultDecorationY = kResultMonkeyY;
constexpr float kResultGroundGridSize = 1.0f;
constexpr float kResultGroundTileScale =
    GameComponents::BlockModelLayout::GetScale(kResultGroundGridSize);
constexpr float kResultGroundTileY =
    kResultMonkeyY
    - GameComponents::BlockModelLayout::kModelBlockSize * kResultGroundTileScale;
constexpr float kResultMonkeyMinCenterDistance = 1.9f;
constexpr float kResultMonkeyLayoutEdgePadding = 0.9f;
constexpr float kResultDecorationMinCenterDistance = 2.1f;
constexpr float kResultDecorationLayoutMargin = 3.0f;
constexpr int kResultMonkeyRandomPlacementAttempts = 512;
constexpr int kResultDecorationRandomPlacementAttempts = 512;

float GetResultMonkeyLayoutRadius(std::size_t monkeyCount)
{
    const float configuredRadius = std::max(0.0f, ResultMonkeyRingRadius.Get());
    const float count = static_cast<float>(std::max<std::size_t>(1, monkeyCount));
    const float autoExpandedRadius = kResultMonkeyLayoutEdgePadding
        + kResultMonkeyMinCenterDistance * 0.75f * std::sqrt(count);
    return std::max(configuredRadius, autoExpandedRadius);
}

float GetResultSceneLayoutRadius(std::size_t monkeyCount)
{
    return GetResultMonkeyLayoutRadius(monkeyCount) + kResultDecorationLayoutMargin;
}
}

ResultScene::ResultScene::~ResultScene() = default;

void ResultScene::ResultScene::OnInitialize() {
    // ========== シーンの設定 ==========
    SetSceneName("ResultScene");
    // 結果画面専用の地形を使うため、エンジン標準の床は生成しない。
    SetDefaultGroundEnabled(false);

    // 結果画面の設定はシーンJSONへ保存できる空オブジェクトに集約する。
    // OnInitialize 後にシーン復元が走るため、Tipsの表示は最初の更新で確定する。
    auto* resultTipsObject = CreateObject<GameScene::GameSceneObject>("ResultTipsSettings");
    if (resultTipsObject) {
        resultTips_ = resultTipsObject->AddComponent<GameComponents::ResultTipsComponent>();
    }

    const std::size_t monkeyCount = std::max<std::size_t>(
        1,
        GameComponents::GameResultData::GetMonkeyCount());
    const std::size_t resultRockCount =
        GameComponents::GameResultData::GetBrokenRockCount() / 4;
    const std::size_t resultBananaTreeCount =
        GameComponents::GameResultData::GetBananaHarvestCount() / 4;
    const std::uint32_t seed =
        0x9E3779B9u
        ^ static_cast<std::uint32_t>(monkeyCount) * 0x85EBCA6Bu
        ^ static_cast<std::uint32_t>(resultRockCount) * 0xC2B2AE35u
        ^ static_cast<std::uint32_t>(resultBananaTreeCount) * 0x27D4EB2Fu
        ^ GameComponents::GameResultData::GetHorizontalProgressBlocks();

    // ゲームシーンと同じ ground.obj を1マスずつ敷き、タイルごとに
    // マテリアル色を少し変えて、マップチップのような床にする。
    const auto createResultGroundTile = [this](
        int gridX,
        int gridZ,
        const Vector4& tint) {
            auto* tile = CreateObject(
                "Result_ground_tile_" + std::to_string(gridX)
                + "_" + std::to_string(gridZ));
            if (!tile) {
                return;
            }

            tile->SetSerializeEnabled(true);
            auto* transform = tile->AddComponent<TransformComponent>();
            if (transform) {
                transform->Translate() = {
                    static_cast<float>(gridX) * kResultGroundGridSize,
                    kResultGroundTileY,
                    static_cast<float>(gridZ) * kResultGroundGridSize };
                transform->Scale() = {
                    kResultGroundTileScale,
                    kResultGroundTileScale,
                    kResultGroundTileScale };
            }
            tile->AddComponent<MeshRendererComponent>("ground.obj");
            if (auto* material = tile->AddComponent<MaterialComponent>()) {
                material->SetColor(tint);
            }
            tile->SetActive(true);
        };

    const float sceneRadius = GetResultSceneLayoutRadius(monkeyCount);
    const int groundTileRadius = std::max(
        1,
        static_cast<int>(std::ceil(sceneRadius)) + 1);
    std::mt19937 groundRandom(seed ^ 0xD1B54A32u);
    std::uniform_real_distribution<float> groundVariation(-1.0f, 1.0f);
    for (int gridX = -groundTileRadius; gridX <= groundTileRadius; ++gridX) {
        for (int gridZ = -groundTileRadius; gridZ <= groundTileRadius; ++gridZ) {
            const float amount = groundVariation(groundRandom);
            const float luminance = 1.0f + 0.20f * amount;
            const float blue = luminance * (1.0f + 0.12f * amount);
            createResultGroundTile(
                gridX,
                gridZ,
                { luminance, luminance, blue, 1.0f });
        }
    }

    std::mt19937 placementRandom(seed);
    std::uniform_real_distribution<float> yRotationDistribution(
        0.0f,
        2.0f * std::numbers::pi_v<float>);

    const auto createResultMonkey = [this](
        const std::string& name,
        const Vector3& position,
        std::size_t monkeyIndex,
        float yRotation) {
            auto* monkey = CreateObject(name);
            if (!monkey) {
                return;
            }

            monkey->SetSerializeEnabled(true);
            auto* transform = monkey->AddComponent<TransformComponent>();
            if (transform) {
                transform->Translate() = position;
                transform->Rotate() = { 0.0f, yRotation, 0.0f };
            }
            monkey->AddComponent<MeshRendererComponent>("result_monkey.obj");
            monkey->AddComponent<GameComponents::ResultMonkeyShakeComponent>(monkeyIndex);
            monkey->SetActive(true);
        };

    const auto createResultDecoration = [this](
        const std::string& name,
        const char* modelPath,
        const Vector3& position,
        float yRotation) {
            auto* decoration = CreateObject(name);
            if (!decoration) {
                return;
            }

            decoration->SetSerializeEnabled(true);
            auto* transform = decoration->AddComponent<TransformComponent>();
            if (transform) {
                transform->Translate() = position;
                transform->Rotate() = { 0.0f, yRotation, 0.0f };
            }
            decoration->AddComponent<MeshRendererComponent>(modelPath);
            decoration->SetActive(true);
        };

    std::vector<Vector3> occupiedPositions;
    const std::size_t decorationCount = resultRockCount + resultBananaTreeCount;
    occupiedPositions.reserve(monkeyCount + decorationCount);
    // 中央のサルも配置判定に含める（Yは使わず、XZの距離だけを判定する）。
    occupiedPositions.push_back({ 0.0f, 0.0f, 0.0f });

    const auto isPositionAvailable = [
        &occupiedPositions](
        const Vector3& candidate,
        float minCenterDistanceSquared,
        float placementRadiusSquared) {
            const float distanceFromCenterSquared =
                candidate.x * candidate.x + candidate.z * candidate.z;
            if (distanceFromCenterSquared > placementRadiusSquared) {
                return false;
            }

            for (const Vector3& placed : occupiedPositions) {
                const float deltaX = candidate.x - placed.x;
                const float deltaZ = candidate.z - placed.z;
                const float distanceSquared = deltaX * deltaX + deltaZ * deltaZ;
                if (distanceSquared < minCenterDistanceSquared) {
                    return false;
                }
            }
            return true;
        };

    createResultMonkey(
        "Result_monkey",
        { 0.0f, kResultMonkeyY, 0.0f },
        0,
        yRotationDistribution(placementRandom));

    if (monkeyCount > 1) {
        // サルの足元の大きさを基準に、円形エリア内へ候補位置をランダムに生成する。
        // 中央のサルと、すでに配置したサルとの距離を判定するため、同じ円周に並ばず重ならない。
        const float layoutRadius = GetResultMonkeyLayoutRadius(monkeyCount);
        const float placementRadius = std::max(
            0.0f,
            layoutRadius - kResultMonkeyLayoutEdgePadding);
        const float placementRadiusSquared = placementRadius * placementRadius;
        const float minCenterDistanceSquared =
            kResultMonkeyMinCenterDistance * kResultMonkeyMinCenterDistance;
        std::uniform_real_distribution<float> positionDistribution(
            -placementRadius,
            placementRadius);
        std::uniform_real_distribution<float> angleDistribution(
            0.0f,
            2.0f * std::numbers::pi_v<float>);

        for (std::size_t index = 1; index < monkeyCount; ++index) {
            Vector3 selectedPosition{};
            bool positionFound = false;

            for (int attempt = 0;
                attempt < kResultMonkeyRandomPlacementAttempts && !positionFound;
                ++attempt) {
                const Vector3 candidate{
                    positionDistribution(placementRandom),
                    0.0f,
                    positionDistribution(placementRandom) };
                if (isPositionAvailable(
                    candidate,
                    minCenterDistanceSquared,
                    placementRadiusSquared)) {
                    selectedPosition = candidate;
                    positionFound = true;
                }
            }

            // 高密度になった場合は、黄金角の候補も試して配置数を確保する。
            if (!positionFound) {
                const float goldenAngle =
                    std::numbers::pi_v<float> * (3.0f - std::sqrt(5.0f));
                const std::size_t fallbackAttempts = std::max<std::size_t>(
                    512,
                    monkeyCount * 256);
                const float radiusSpan = std::max(
                    0.0f,
                    placementRadius - kResultMonkeyMinCenterDistance);
                const float randomPhase = angleDistribution(placementRandom);

                for (std::size_t attempt = 0;
                    attempt < fallbackAttempts && !positionFound;
                    ++attempt) {
                    const float normalizedAttempt = static_cast<float>(attempt + 1)
                        / static_cast<float>(fallbackAttempts);
                    const float candidateRadius =
                        kResultMonkeyMinCenterDistance
                        + radiusSpan * std::sqrt(normalizedAttempt);
                    const float angle = randomPhase
                        + static_cast<float>(attempt) * goldenAngle;
                    const Vector3 candidate{
                        std::cos(angle) * candidateRadius,
                        0.0f,
                        std::sin(angle) * candidateRadius };
                    if (isPositionAvailable(
                        candidate,
                        minCenterDistanceSquared,
                        placementRadiusSquared)) {
                        selectedPosition = candidate;
                        positionFound = true;
                    }
                }
            }

            if (!positionFound) {
                break;
            }

            occupiedPositions.push_back(selectedPosition);
            createResultMonkey(
                "Result_monkey_" + std::to_string(index + 1),
                {
                    selectedPosition.x,
                    kResultMonkeyY,
                    selectedPosition.z
                },
                index,
                yRotationDistribution(placementRandom));
        }
    }

    const auto placeDecorations = [
        &createResultDecoration,
        &isPositionAvailable,
        &occupiedPositions,
        &placementRandom,
        &yRotationDistribution,
        monkeyCount](
        const char* modelPath,
        const char* namePrefix,
        std::size_t count) {
            if (count == 0) {
                return;
            }

            const float layoutRadius = GetResultSceneLayoutRadius(monkeyCount);
            const float placementRadius = std::max(
                0.0f,
                layoutRadius - kResultMonkeyLayoutEdgePadding);
            const float placementRadiusSquared = placementRadius * placementRadius;
            const float minCenterDistanceSquared =
                kResultDecorationMinCenterDistance * kResultDecorationMinCenterDistance;
            std::uniform_real_distribution<float> positionDistribution(
                -placementRadius,
                placementRadius);
            std::uniform_real_distribution<float> angleDistribution(
                0.0f,
                2.0f * std::numbers::pi_v<float>);

            for (std::size_t index = 0; index < count; ++index) {
                Vector3 selectedPosition{};
                bool positionFound = false;

                for (int attempt = 0;
                    attempt < kResultDecorationRandomPlacementAttempts && !positionFound;
                    ++attempt) {
                    const Vector3 candidate{
                        positionDistribution(placementRandom),
                        0.0f,
                        positionDistribution(placementRandom) };
                    if (isPositionAvailable(
                        candidate,
                        minCenterDistanceSquared,
                        placementRadiusSquared)) {
                        selectedPosition = candidate;
                        positionFound = true;
                    }
                }

                if (!positionFound) {
                    const float goldenAngle =
                        std::numbers::pi_v<float> * (3.0f - std::sqrt(5.0f));
                    const std::size_t fallbackAttempts = std::max<std::size_t>(
                        512,
                        count * 256);
                    const float radiusSpan = std::max(
                        0.0f,
                        placementRadius - kResultDecorationMinCenterDistance);
                    const float randomPhase = angleDistribution(placementRandom);

                    for (std::size_t attempt = 0;
                        attempt < fallbackAttempts && !positionFound;
                        ++attempt) {
                        const float normalizedAttempt = static_cast<float>(attempt + 1)
                            / static_cast<float>(fallbackAttempts);
                        const float candidateRadius =
                            kResultDecorationMinCenterDistance
                            + radiusSpan * std::sqrt(normalizedAttempt);
                        const float angle = randomPhase
                            + static_cast<float>(attempt) * goldenAngle;
                        const Vector3 candidate{
                            std::cos(angle) * candidateRadius,
                            0.0f,
                            std::sin(angle) * candidateRadius };
                        if (isPositionAvailable(
                            candidate,
                            minCenterDistanceSquared,
                            placementRadiusSquared)) {
                            selectedPosition = candidate;
                            positionFound = true;
                        }
                    }
                }

                if (!positionFound) {
                    break;
                }

                occupiedPositions.push_back(selectedPosition);
                createResultDecoration(
                    std::string(namePrefix) + std::to_string(index + 1),
                    modelPath,
                    {
                        selectedPosition.x,
                        kResultDecorationY,
                        selectedPosition.z
                    },
                    yRotationDistribution(placementRandom));
            }
        };

    placeDecorations(
        "rock.obj",
        "Result_rock_",
        resultRockCount);
    placeDecorations(
        "banana_tree.obj",
        "Result_banana_tree_",
        resultBananaTreeCount);

    // ゲームシーンと同じ雲（高さフォグ）。設定は「ゲーム設定」の Game.Fog.* を共有する。
    AddFeature(GameComponents::CreateSkyFogFeature());

    if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
        resultBgm_ = audioSystem->PlayScoped(
            kResultBgmPath,
            { .bus = AudioBus::BGM, .loop = true, .volume = 1.0f / 3.0f });
    }

    ui_ = ResultSceneUi::Build(
        [this](const std::string& text,
            float fontSize,
            UIAnchor anchor,
            const Vector2& position,
            const Vector4& color,
            const std::string& name) -> UIText* {
                return CreateText(text, fontSize, anchor, position, color, name);
        },
        [this](const std::string& texturePath,
            const std::string& name) -> UIImage* {
                auto* image = CreateObject<UIImage>();
                if (!image) {
                    return nullptr;
                }
                image->Initialize(texturePath, name);
                return image;
        });

    SetSelection(selection_, false);
}

void ResultScene::ResultScene::OnUpdate() {
    InitializeTipText();

    auto* inputManager = engine_ ? engine_->GetService<InputManager>() : nullptr;
    if (returnRequested_ || !sceneManager_ || !inputManager) {
        return;
    }
    const auto& input = inputManager->GetQuery();
    if (input.IsActionTriggered(InputAction::UICancel)) {
        returnRequested_ = true;
        sceneManager_->ChangeScene("TitleScene");
        return;
    }

    const bool left = input.IsActionTriggered(InputAction::MoveLeft);
    const bool right = input.IsActionTriggered(InputAction::MoveRight);
    if (left != right) {
        SetSelection(
            selection_ == Selection::Retry ? Selection::Title : Selection::Retry,
            true);

        if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
            audioSystem->PlayOneShot(
                kRailBuildSePath,
                { .bus = AudioBus::SE });
        }
    }
    if (input.IsActionTriggered(InputAction::UIConfirm)) {
        ConfirmSelection();
    }
}

void ResultScene::ResultScene::InitializeTipText()
{
    if (tipInitialized_) {
        return;
    }
    tipInitialized_ = true;

    if (!resultTips_) {
        return;
    }

    const auto& tips = resultTips_->GetTips();
    if (tips.empty()) {
        return;
    }

    // 空欄を飛ばしつつ、設定された配列順で次のTipを選ぶ。
    for (std::size_t offset = 0; offset < tips.size(); ++offset) {
        const std::size_t index = (nextTipIndex_ + offset) % tips.size();
        if (tips[index].empty()) {
            continue;
        }

        ResultSceneUi::SetTipText(ui_, tips[index]);
        nextTipIndex_ = (index + 1) % tips.size();
        return;
    }
}

void ResultScene::ResultScene::OnLateUpdate()
{
    UpdateResultCamera();
}

void ResultScene::ResultScene::UpdateResultCamera()
{
    if (!cameraManager_) {
        return;
    }

    auto* camera = cameraManager_->GetCamera(CameraNames::Game);
    if (!camera) {
        return;
    }

    // 配置エリアの端まで収まるよう、サル数に応じてカメラを後退させる。
    const std::size_t monkeyCount = std::max<std::size_t>(
        1,
        GameComponents::GameResultData::GetMonkeyCount());
    const float radius = GetResultSceneLayoutRadius(monkeyCount);
    const float cameraDistance = std::max(18.0f, radius + 14.0f);
    resultCameraOrbitAngle_ += ResultCameraOrbitSpeed.Get()
        * std::max(0.0f, Time::UnscaledDeltaTime());
    resultCameraOrbitAngle_ = std::fmod(
        resultCameraOrbitAngle_,
        2.0f * std::numbers::pi_v<float>);
    const Vector3 focus = { 0.0f, kResultMonkeyY, 0.0f };
    camera->SetTranslate({
        std::sin(resultCameraOrbitAngle_) * cameraDistance,
        kResultMonkeyY + 5.5f,
        -std::cos(resultCameraOrbitAngle_) * cameraDistance });
    camera->LookAt(focus);
    camera->UpdateMatrix();
}

void ResultScene::ResultScene::SetSelection(Selection selection, bool playReaction)
{
    selection_ = selection;

    auto* retryAnimation = ui_.retryButton
        ? ui_.retryButton->GetComponent<GameComponents::ResultButtonAnimationComponent>()
        : nullptr;
    auto* titleAnimation = ui_.titleButton
        ? ui_.titleButton->GetComponent<GameComponents::ResultButtonAnimationComponent>()
        : nullptr;

    if (retryAnimation) {
        retryAnimation->SetSelected(selection_ == Selection::Retry);
    }
    if (titleAnimation) {
        titleAnimation->SetSelected(selection_ == Selection::Title);
    }

    if (!playReaction) {
        return;
    }

    auto* selectedAnimation = selection_ == Selection::Retry
        ? retryAnimation
        : titleAnimation;
    if (selectedAnimation) {
        selectedAnimation->PlaySelectionReaction();
    }
}

void ResultScene::ResultScene::ConfirmSelection()
{
    if (returnRequested_ || !sceneManager_) {
        return;
    }

    returnRequested_ = true;

    if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
        audioSystem->PlayOneShot(
            kDecisionSePath,
            { .bus = AudioBus::SE });
    }

    const char* nextScene = selection_ == Selection::Retry ? "GameScene" : "TitleScene";
    auto* selectedAnimation = selection_ == Selection::Retry
        ? (ui_.retryButton
            ? ui_.retryButton->GetComponent<GameComponents::ResultButtonAnimationComponent>()
            : nullptr)
        : (ui_.titleButton
            ? ui_.titleButton->GetComponent<GameComponents::ResultButtonAnimationComponent>()
            : nullptr);

    auto* unselectedAnimation = selection_ == Selection::Retry
        ? (ui_.titleButton
            ? ui_.titleButton->GetComponent<GameComponents::ResultButtonAnimationComponent>()
            : nullptr)
        : (ui_.retryButton
            ? ui_.retryButton->GetComponent<GameComponents::ResultButtonAnimationComponent>()
            : nullptr);

    const auto changeScene = [this, nextScene] {
        if (sceneManager_) {
            sceneManager_->ChangeScene(nextScene);
        }
        };

    if (selectedAnimation) {
        if (unselectedAnimation) {
            unselectedAnimation->PlayUnselectedFade();
        }
        selectedAnimation->PlayConfirmReaction(changeScene);
        return;
    }

    changeScene();
}
