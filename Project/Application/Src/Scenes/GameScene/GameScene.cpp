#include "pch.h"
#include "GameScene.h"

#include "EngineSystem/EngineSystem.h"
#include "Audio/AudioSystem.h"
#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Render/MaterialComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "GameObject/GameObjectManager.h"
#include "Graphics/Model/ModelManager.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Particle/ParticleSystem.h"
#include "Scene/Feature/TimeOfDayFeature.h"
#include "OffscreenTrainIndicatorFeature.h"
#include "BananaTreeAuraFeature.h"
#include "GameEntranceFeature.h"
#include "PauseMenuFeature.h"
#include "RailDirectionGuideFeature.h"
#include "SkyFogFeature.h"
#include "SpeedBlurFeature.h"
#include "SpeedGaugeFeature.h"
#include "StageLightsFeature.h"
#include "StaminaGaugeFeature.h"
#include "Utility/Logger/Logger.h"

#include "Components/Utility/ModelRenderPoolComponent.h"

#include "Components/Building/MapGeneratorComponent.h"
#include "Components/Building/MapViewComponent.h"
#include "Components/Building/RockThrowComponent.h"
#include "Components/Building/WaterWaveViewComponent.h"
#include "Components/Camera/RockBreakShakeSettingsComponent.h"
#include "Components/Rail/RailBuilderComponent.h"
#include "Components/Rail/RailPathComponent.h"
#include "Components/Rail/RailViewComponent.h"
#include "Components/Train/SpawnPopComponent.h"
#include "Components/Train/TrainMovementComponent.h"

#include "Components/GameCore/GameManagerComponent.h"
#include "Components/GameCore/GameResultData.h"
#include "Components/GameCore/GameSettingsComponent.h"
#include "Components/GameCore/HungerComponent.h"
#include "GameObjects/Effect/BananaHarvestEffect.h"
#include "GameObjects/Effect/RockBreakDebris.h"
#include "GameObjects/Effect/StationSlowdownEffect.h"
#include "GameObjects/GameSceneObject.h"
#include "UI/UIText.h"
#include "Utility/JsonManager/JsonManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

using namespace CoreEngine;

namespace {
    constexpr const char* kGameBgmPath = "Application/Assets/Sounds/BGM/Game_bgm.mp3";
    constexpr const char* kMonkeyLaunchTrailPresetPath =
        "Application/Assets/Presets/Particle/MonkeyLaunchTrail.json";
    constexpr const char* kMonkeyLaunchParticleModel = "particle.obj";
    constexpr const char* kMonkeyLaunchParticleTexture = "particle.png";

    CoreEngine::ParticleSystem* CreateMonkeyLaunchTrail(
        CoreEngine::GameObjectManager* objectManager,
        CoreEngine::EngineSystem* engine,
        const std::string& name) {
        if (!engine || !objectManager) {
            return nullptr;
        }

        auto* dxCommon = engine->GetService<CoreEngine::GraphicsCore>();
        auto* resourceFactory = engine->GetService<CoreEngine::ResourceFactory>();
        auto* modelManager = engine->GetService<CoreEngine::ModelManager>();
        if (!dxCommon || !resourceFactory || !modelManager) {
            CoreEngine::Logger::GetInstance().Errorf(
                CoreEngine::LogCategory::Game,
                "MonkeyLaunchTrail: 必要なサービスが揃っていないのでパーティクルを作れません");
            return nullptr;
        }

        modelManager->PreloadModels({ kMonkeyLaunchParticleModel });
        auto* modelResource = modelManager->GetModelResource(kMonkeyLaunchParticleModel);
        if (!modelResource) {
            CoreEngine::Logger::GetInstance().Errorf(
                CoreEngine::LogCategory::Game,
                "MonkeyLaunchTrail: モデルを読めませんでした: {}",
                kMonkeyLaunchParticleModel);
            return nullptr;
        }

        auto* particleSystem =
            objectManager->AddObject(std::make_unique<CoreEngine::ParticleSystem>());
        particleSystem->Initialize(dxCommon, resourceFactory, name);
        particleSystem->SetTexture(kMonkeyLaunchParticleTexture);
        particleSystem->SetModelResource(modelResource);
        if (!particleSystem->LoadPreset(kMonkeyLaunchTrailPresetPath)) {
            CoreEngine::Logger::GetInstance().Errorf(
                CoreEngine::LogCategory::Game,
                "MonkeyLaunchTrail: プリセットを読めませんでした: {}",
                kMonkeyLaunchTrailPresetPath);
            return nullptr;
        }

        return particleSystem;
    }
    constexpr const char* kStageProjectPath = "Application/Assets/Maps/stage_project.json";

    uint32_t ToUInt(int value, int minimum = 0) {
        return static_cast<uint32_t>(std::max(value, minimum));
    }

    std::string ToPortablePath(const std::filesystem::path& path) {
        const std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }

    std::vector<std::string> ScanAreaCsvFiles(const std::string& areaName) {
        const std::filesystem::path areaDirectory =
            std::filesystem::path("Application") / "Assets" / "Maps" / "Areas" / areaName;
        std::error_code ec;
        if (!std::filesystem::is_directory(areaDirectory, ec)) {
            return {};
        }

        std::vector<std::filesystem::path> csvFiles;
        for (const auto& entry : std::filesystem::directory_iterator(areaDirectory, ec)) {
            std::error_code fileError;
            if (!entry.is_regular_file(fileError) || fileError) {
                continue;
            }
            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (extension == ".csv") {
                csvFiles.push_back(entry.path());
            }
        }
        std::sort(csvFiles.begin(), csvFiles.end(),
            [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
                return lhs.filename().generic_u8string() < rhs.filename().generic_u8string();
            });

        std::vector<std::string> paths;
        paths.reserve(csvFiles.size());
        for (const auto& csvFile : csvFiles) {
            paths.push_back(ToPortablePath(csvFile));
        }
        return paths;
    }

    void RefreshAreaCsvFiles(GameComponents::MapGenerationSettings& settings) {
        bool hasArea1 = false;
        for (auto& pool : settings.csvPools) {
            const std::vector<std::string> scanned = ScanAreaCsvFiles(pool.name);
            if (!scanned.empty()) {
                pool.paths = scanned;
            }
            hasArea1 = hasArea1 || pool.name == "Area1";
        }

        // 構成JSONにArea1が無くても、フォルダーに置かれたチャンクは使えるようにする。
        if (!hasArea1) {
            const std::vector<std::string> area1Paths = ScanAreaCsvFiles("Area1");
            if (!area1Paths.empty()) {
                settings.csvPools.push_back({ "Area1", area1Paths });
            }
        }
    }

    GameComponents::MapGenerationSettings MakeDefaultMapSettings(std::size_t chunkSizeX) {
        GameComponents::MapGenerationSettings settings;
        settings.mode = GameComponents::MapGenerationMode::FixedThenRandomCsvPool;
        settings.csvChunkSizeX = chunkSizeX;
        settings.csvPools = {
            { "Area1", {} },
            { "Area2", {} },
        };
        settings.initialCsvPoolName = "Area1";
        settings.fixedCsvPath = "Application/Assets/Maps/fixed.csv";
        RefreshAreaCsvFiles(settings);
        return settings;
    }

    void LoadStageProjectSettings(GameComponents::MapGenerationSettings& settings) {
        const json root = JsonManager::GetInstance().LoadJson(kStageProjectPath);
        if (root.empty() || !root.is_object()) {
            return;
        }

        settings.csvChunkSizeX = std::max<std::size_t>(1,
            JsonManager::SafeGet<std::size_t>(root, "chunkSizeX", settings.csvChunkSizeX));
        settings.fixedCsvPath = JsonManager::SafeGet<std::string>(
            root, "fixedCsvPath", settings.fixedCsvPath);
        settings.initialCsvPoolName = JsonManager::SafeGet<std::string>(
            root, "initialArea", settings.initialCsvPoolName);

        if (root.contains("areas") && root["areas"].is_array()) {
            std::vector<GameComponents::CsvMapPoolSettings> pools;
            for (const auto& element : root["areas"]) {
                if (!element.is_object()) {
                    continue;
                }
                const std::string name = JsonManager::SafeGet<std::string>(
                    element, "name", std::string{});
                if (name.empty() || !element.contains("paths") || !element["paths"].is_array()) {
                    continue;
                }

                GameComponents::CsvMapPoolSettings pool;
                pool.name = name;
                for (const auto& path : element["paths"]) {
                    if (path.is_string()) {
                        pool.paths.push_back(path.get<std::string>());
                    }
                }
                pools.push_back(std::move(pool));
            }
            if (!pools.empty()) {
                settings.csvPools = std::move(pools);
            }
        }
        // Area1フォルダーを正として再走査するため、後から追加したCSVも自動で入る。
        RefreshAreaCsvFiles(settings);
    }
}

GameScene::GameScene::~GameScene() = default;

void GameScene::GameScene::OnInitialize() {
    GameComponents::GameResultData::Reset();

    // ========== シーンの設定 ==========
    SetSceneName("GameScene");
    // 既定床（y = 0 の無限板）は生成しない。ステージのブロックより下は
    // SkyFogFeature の雲で埋めるので、板を出すと雲も水場の滝も板に隠れてしまう。
    SetDefaultGroundEnabled(false);

    // ========== 昼夜サイクル ==========
    // 時刻を進めて空と太陽・月を昼→夕→夜と変えるだけの Feature。
    // 進み方（1 周の秒数・開始時刻）は Engine Settings の "Time of Day" から調整する。
    AddFeature(std::make_unique<CoreEngine::TimeOfDayFeature>());
    // 夕方から夜にかけて灯る、ビルダーとトロッコの灯り（ポイントライト）
    AddFeature(std::make_unique<StageLightsFeature>());
    // 突入演出（雲海ブレイク → もくひょう看板 → つなげ！！）と、200m 刻みの目標提示。
    // 開幕の雲は Game.Fog.* を借りて書き換えるので、SkyFogFeature より先に登録すること
    // （同じ FrameStart では登録順に回る。後にすると雲の反映が 1 フレーム遅れる）。
    AddFeature(GameComponents::CreateGameEntranceFeature());
    // ステージのブロックより下を埋める雲（高さフォグ）。
    // 濃さ・色・高さは「ゲーム設定」の Game.Fog.* から調整する。
    AddFeature(GameComponents::CreateSkyFogFeature());
    // スタミナをバナナの粒で見せる HUD ゲージ。
    // 位置・粒あたりのスタミナ量は「ゲーム設定」の Game.StaminaGauge.* から調整する。
    AddFeature(GameComponents::CreateStaminaGaugeFeature());
    // ツタで吊るした木の看板のポーズメニュー。ESC ／ パッドの START で開く。
    // 見た目は「ゲーム設定」の Game.PauseMenu.* から調整する。
    AddFeature(GameComponents::CreatePauseMenuFeature());
    // トロッコの速さを km/h のオドメーターで見せる HUD。
    // 位置・1 マスの実距離は「ゲーム設定」の Game.SpeedGauge.* から調整する。
    AddFeature(GameComponents::CreateSpeedGaugeFeature());
    // 速さに合わせて画面へモーションブラーを掛ける。速度計を見ていなくても
    // 加速と、駅で速さを失う瞬間が画面全体で分かるようにするためのもの。
    // 濃さは「ゲーム設定」の Game.SpeedBlur.* から調整する。
    AddFeature(GameComponents::CreateSpeedBlurFeature());
    // レール先頭の上下左右へ、伸ばせる向きだけ床に矢印を出すガイド。
    // 戻る（Undo）向きだけは別の記号にしてある。
    // 見た目は「ゲーム設定」の Game.RailGuide.* から調整する。
    AddFeature(GameComponents::CreateRailDirectionGuideFeature());
    // カーソルを伸ばしすぎてトロッコが画面外へ押し出されている間、画面の端へ
    // トロッコのアイコンと「あと○m」を出す案内。
    // 見た目は「ゲーム設定」の Game.TrainOffscreen.* から調整する。
    AddFeature(GameComponents::CreateOffscreenTrainIndicatorFeature());
    // バナナの木の上下左右へ、レールが無い間だけ四角い波動を出して収穫範囲を示す。
    // 見た目は「ゲーム設定」の Game.BananaTreeAura.* から調整する。
    AddFeature(GameComponents::CreateBananaTreeAuraFeature());

    // ========== BGMの再生 ==========
    auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr;
    if (!audioSystem) {
        return;
    }
    gameBgm_ = audioSystem->PlayScoped(
        kGameBgmPath,
        { .bus = AudioBus::BGM, .loop = true,
          .volume = GameComponents::GameSettings::BgmVolume.Get() });

    // ========== SEの登録 ==========
    std::function<void()> playDecisionSe = [this] {
        if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
            audioSystem->PlayOneShot(
                "Application/Assets/Sounds/SE/decision.mp3",
                { .bus = AudioBus::SE });
        }
        };
    std::function<void()> playBuildSe = [this] {
        if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
            int randomIndex = rand() % 50;
            float pitch = 0.5f + (static_cast<float>(randomIndex) / 50.0f);

            audioSystem->PlayOneShot(
                "Application/Assets/Sounds/SE/build.mp3",
                { .bus = AudioBus::SE,.volume = 0.5f, .pitch = pitch });
        }
        };
    std::function<void()> playUndoSe = [this] {
        if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
            audioSystem->PlayOneShot(
                "Application/Assets/Sounds/SE/build_return.mp3",
                { .bus = AudioBus::SE });
        }
        };
    std::function<void()> playFailureSe = [this] {
        if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
            audioSystem->PlayOneShot(
                "Application/Assets/Sounds/SE/beep.mp3",
                { .bus = AudioBus::SE });
        }
        };
    std::function<void(float, float, bool)> playRailBuildSe =
        [this](float volume, float pitch, bool isStationRail) {
        if (auto* audioSystem = engine_ ? engine_->GetService<AudioSystem>() : nullptr) {
            CoreEngine::PlayParams params;
            params.bus = AudioBus::SE;
            params.volume = volume;
            params.pitch = pitch;
            audioSystem->PlayOneShot(
                isStationRail
                    ? "Application/Assets/Sounds/SE/build_station.mp3"
                    : "Application/Assets/Sounds/SE/rail_build.mp3",
                params);
        }
        };

    // ========== ゲームルールの設定 ==========
    const float gridSize = GameComponents::GameSettings::GridSize.Get();
    const uint32_t mapSizeZ = ToUInt(
        GameComponents::GameSettings::MapSizeZ.Get(), 1);

    const uint32_t initialBuilderPosX = ToUInt(
        GameComponents::GameSettings::BuilderStartX.Get());
    const uint32_t initialBuilderPosZ = std::min(
        ToUInt(GameComponents::GameSettings::BuilderStartZ.Get()),
        mapSizeZ - 1);

    const uint32_t initialGenerateMapSizeX = ToUInt(
        GameComponents::GameSettings::InitialMapSizeX.Get(), 1);
    const uint32_t renderWorldDistance = ToUInt(
        GameComponents::GameSettings::RenderDistance.Get(), 1);

    // 先頭は fixed.csv の実幅ぶんをそのまま使い、その終端からArea1の
    // チャンクCSVを開始する。構成表を読めない場合はサンプルの既定値を使う。
    GameComponents::MapGenerationSettings mapSettings = MakeDefaultMapSettings(
        ToUInt(GameComponents::GameSettings::CsvChunkSizeX.Get(), 1));
    LoadStageProjectSettings(mapSettings);

    // ========== オブジェクトの生成 ==========
    //　ゲームマスターの追加
    auto* gameManager = CreateObject<GameSceneObject>("GameManager");
    auto* gameManagerComponent =
        gameManager->AddComponent<GameComponents::GameManagerComponent>(sceneManager_);
    gameManager->AddComponent<GameComponents::GameSettingsComponent>();

    // 床のオブジェクトプールを生成
    auto* groundPoolManager = CreateObject<GameSceneObject>("GroundPoolManager");
    groundPoolManager->AddComponent<CoreEngine::TransformComponent>();
    groundPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "ground.obj",
        ToUInt(GameComponents::GameSettings::GroundPoolCapacity.Get(), 1), false);
    // 床の下へ吊るす柱（スカート）のオブジェクトプールを生成。
    // 同じ ground.obj を使うが、1マスにつき床とスカートの2つを同時に出すので
    // プールは分ける。必要数は床と同じなので容量も同じ CVar から取る。
    auto* groundSkirtPoolManager = CreateObject<GameSceneObject>("GroundSkirtPoolManager");
    groundSkirtPoolManager->AddComponent<CoreEngine::TransformComponent>();
    groundSkirtPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "ground.obj",
        ToUInt(GameComponents::GameSettings::GroundPoolCapacity.Get(), 1), false);
    // 水場は WaterWaveViewComponent が板を並べて波打たせる（この下の方で生成する）。
    // 駅のオブジェクトプールを生成
    auto* stationPoolManager = CreateObject<GameSceneObject>("StationPoolManager");
    stationPoolManager->AddComponent<CoreEngine::TransformComponent>();
    stationPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "station.obj",
        ToUInt(GameComponents::GameSettings::StationPoolCapacity.Get(), 1), true);
    // 岩のオブジェクトプールを生成
    auto* rockPoolManager = CreateObject<GameSceneObject>("RockPoolManager");
    rockPoolManager->AddComponent<CoreEngine::TransformComponent>();
    rockPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "rock.obj",
        ToUInt(GameComponents::GameSettings::RockPoolCapacity.Get(), 1), true);
    // レールを敷けない空白マスへ立てる硬い岩のオブジェクトプールを生成
    auto* hardRockPoolManager = CreateObject<GameSceneObject>("HardRockPoolManager");
    hardRockPoolManager->AddComponent<CoreEngine::TransformComponent>();
    hardRockPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "hard_rock.obj",
        ToUInt(GameComponents::GameSettings::HardRockPoolCapacity.Get(), 1), true);
    // バナナの木のオブジェクトプールを生成（仮モデルとしてbox.objを使用）
    auto* bananaTreePoolManager = CreateObject<GameSceneObject>("BananaTreePoolManager");
    bananaTreePoolManager->AddComponent<CoreEngine::TransformComponent>();
    bananaTreePoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "banana_tree.obj",
        ToUInt(GameComponents::GameSettings::BananaTreePoolCapacity.Get(), 1), true);
    // 地面の上に表示する装飾用の草のオブジェクトプールを生成
    auto* grassPoolManager = CreateObject<GameSceneObject>("GrassPoolManager");
    grassPoolManager->AddComponent<CoreEngine::TransformComponent>();
    grassPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "grass.obj",
        ToUInt(GameComponents::GameSettings::GrassPoolCapacity.Get(), 1), true);
    // 水上レールの下へ表示する橋のオブジェクトプールを生成
    auto* bridgePoolManager = CreateObject<GameSceneObject>("BridgePoolManager");
    bridgePoolManager->AddComponent<CoreEngine::TransformComponent>();
    bridgePoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "bridge.obj",
        ToUInt(GameComponents::GameSettings::BridgePoolCapacity.Get(), 1), true);
    // レールのオブジェクトプールを生成
    auto* railPoolManager = CreateObject<GameSceneObject>("RailPoolManager");
    railPoolManager->AddComponent<CoreEngine::TransformComponent>();
    railPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "rail.obj",
        ToUInt(GameComponents::GameSettings::RailPoolCapacity.Get(), 1), false);
    // レール左のオブジェクトプールを生成
    auto* railLeftPoolManager = CreateObject<GameSceneObject>("RailLeftPoolManager");
    railLeftPoolManager->AddComponent<CoreEngine::TransformComponent>();
    railLeftPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "rail_l.obj",
        ToUInt(GameComponents::GameSettings::RailLeftPoolCapacity.Get(), 1), false);
    // レール右のオブジェクトプールを生成
    auto* railRightPoolManager = CreateObject<GameSceneObject>("RailRightPoolManager");
    railRightPoolManager->AddComponent<CoreEngine::TransformComponent>();
    railRightPoolManager->AddComponent<GameComponents::ModelRenderPoolComponent>(
        "rail_r.obj",
        ToUInt(GameComponents::GameSettings::RailRightPoolCapacity.Get(), 1), false);

    // マップを生成するコンポーネントを追加
    auto* mapGenerator = CreateObject<GameSceneObject>("MapGenerator");
    mapGenerator->AddComponent<CoreEngine::TransformComponent>();
    mapGenerator->AddComponent<GameComponents::MapGeneratorComponent>(
        mapSizeZ, initialGenerateMapSizeX, mapSettings);

    // 建設行動で消費し、バナナの木で回復するスタミナを管理する。
    auto* hungerComponent = gameManager->AddComponent<GameComponents::HungerComponent>(
        mapGenerator->GetComponent<GameComponents::MapGeneratorComponent>(),
        gameManagerComponent);
    
    // レールの配置を管理するコンポーネントを追加
    auto* railPath = CreateObject<GameSceneObject>("RailPath");
    railPath->AddComponent<CoreEngine::TransformComponent>();
    railPath->AddComponent<GameComponents::RailPathComponent>(
        mapSizeZ, initialBuilderPosX, initialBuilderPosZ);

    // レールを表示するコンポーネントを追加
    auto* railView = CreateObject<GameSceneObject>("RailView");
    railView->AddComponent<CoreEngine::TransformComponent>();

    // レールを配置するオブジェクトを生成
    auto* railBuilder = CreateObject<GameSceneObject>("RailBuilder");
    railBuilder->AddComponent<CoreEngine::TransformComponent>();

    // 列車の移動ロジックを持つオブジェクト。描画とアニメーションは別コンポーネントで追加する。
    auto* train = CreateObject<GameSceneObject>("Train");
    auto* trainTransform = train->AddComponent<CoreEngine::TransformComponent>();
    auto* trainMovement = train->AddComponent<GameComponents::TrainMovementComponent>(
        gridSize, GameComponents::GameSettings::TrainMoveSpeed.Get(),
        initialBuilderPosX, initialBuilderPosZ,
        railPath->GetComponent<GameComponents::RailPathComponent>(),
        gameManagerComponent,
        hungerComponent);
    // モデルの正面（-Z）をマップ右方向（+X）へ向ける。
    trainTransform->Get().rotate.y = -1.57079632679f;

    train->AddComponent< CoreEngine::MeshRendererComponent>("trolley.obj");

    // 岩破壊時に列車から投げる石。アニメーションはゲーム終了演出中も完了させる。
    auto* rockProjectile = CreateObject<GameSceneObject>("RockProjectile");
    rockProjectile->AddComponent<CoreEngine::TransformComponent>();
    rockProjectile->AddComponent<CoreEngine::MeshRendererComponent>("rock.obj");
    auto* rockThrow = rockProjectile->AddComponent<GameComponents::RockThrowComponent>();

    // 列車の描画は、列車の移動ロジックを持つコンポーネントとは別のコンポーネントで行う。
    railBuilder->AddComponent<GameComponents::RailBuilderComponent>(
        gridSize, initialBuilderPosX, initialBuilderPosZ,
        railPath->GetComponent<GameComponents::RailPathComponent>(),
        mapGenerator->GetComponent<GameComponents::MapGeneratorComponent>(),
        train->GetComponent<GameComponents::TrainMovementComponent>(),
        hungerComponent,
        rockThrow,
        playBuildSe, playUndoSe, playFailureSe);

    railBuilder->AddComponent<CoreEngine::MeshRendererComponent>("arrow.obj");

    gameManagerComponent->SetGameplayComponents(
        train->GetComponent<GameComponents::TrainMovementComponent>(),
        railBuilder->GetComponent<GameComponents::RailBuilderComponent>(),
        hungerComponent);

    // 列車に乗るサル
    auto* monkey = CreateObject<GameSceneObject>("Monkey");
    auto* monkeyTransform = monkey->AddComponent<CoreEngine::TransformComponent>();
    monkey->AddComponent<CoreEngine::MeshRendererComponent>("monkey.obj");
    monkeyTransform->Get().SetParent(&trainTransform->Get());
    // 親のトロッコが右を向くため、サルは正面向きの相対回転にする。
    monkeyTransform->Get().rotate.y = 0.0f;
    // 先頭のサルもゲームオーバー時に飛ぶため、追加サルと同じく
    // 飛行中の軌跡用パーティクルをあらかじめ用意しておく。
    auto* firstMonkeyLaunchTrail = CreateMonkeyLaunchTrail(
        &gameObjectManager_, engine_, "MonkeyLaunchTrail_0");
    trainMovement->AddMonkey(monkeyTransform, firstMonkeyLaunchTrail);
    hungerComponent->SetMonkeyAddedCallback(
        [this, trainMovement, monkeyTransform](std::size_t monkeyCount) {
            auto* carriage = CreateObject<GameSceneObject>(
                "TrainCarriage_" + std::to_string(monkeyCount));
            if (!carriage) {
                return;
            }
            auto* carriageTransform = carriage->AddComponent<CoreEngine::TransformComponent>();
            carriage->AddComponent<CoreEngine::MeshRendererComponent>("trolley.obj");
            // 連結より前に付けて、最初のスケール計算から出現演出を効かせる。
            // 子のサルは親のスケールを継ぐので、まとめて潰れる。
            auto* carriagePop =
                carriage->AddComponent<GameComponents::SpawnPopComponent>();
            trainMovement->AddCarriage(
                carriageTransform, carriagePop->GetScaleMultiplier());

            auto* addedMonkey = CreateObject<GameSceneObject>(
                "Monkey_" + std::to_string(monkeyCount));
            if (!addedMonkey) {
                return;
            }
            auto* addedTransform = addedMonkey->AddComponent<CoreEngine::TransformComponent>();
            addedMonkey->AddComponent<CoreEngine::MeshRendererComponent>("monkey.obj");
            if (addedTransform) {
                addedTransform->Get().SetParent(&carriageTransform->Get());
                addedTransform->Get().translate = monkeyTransform->Get().translate;
                addedTransform->Get().rotate = monkeyTransform->Get().rotate;
                addedTransform->Get().scale = monkeyTransform->Get().scale;
                auto* monkeyLaunchTrail = CreateMonkeyLaunchTrail(
                    &gameObjectManager_, engine_,
                    "MonkeyLaunchTrail_" + std::to_string(monkeyCount));
                trainMovement->AddMonkey(addedTransform, monkeyLaunchTrail);
            }
        });

    // カメラの構図は Presets/CameraRigs/GamePlay.json が持つ。
    // 起動は _camera.json の startupRigName 任せで、ここでは何も駆動しない。
    auto* gameCamera = cameraManager_->GetCamera(CoreEngine::CameraNames::Game);

    // カーソルが画面外へ出ないよう、映っている範囲をカメラから直接見て止める。
    // 渡さなければ制限は掛からないので、リグを止めても建設は従来どおり動く。
    if (auto* railBuilderComponent =
        railBuilder->GetComponent<GameComponents::RailBuilderComponent>()) {
        railBuilderComponent->SetViewCamera(gameCamera);
    }

    // 岩破壊の揺れは静的に鳴らす。ここでは調整用CVarをインスペクタへ出すために付ける。
    auto* cameraSettings = CreateObject<GameSceneObject>("CameraSettings");
    cameraSettings->AddComponent<GameComponents::RockBreakShakeSettingsComponent>();

    // 岩が砕けた瞬間に散る破片。揺れと同じく RailBuilder から静的に鳴らす。
    AddFeature(GameComponents::CreateRockBreakDebrisFeature());

    // サルがバナナの木を通るたびに、バナナを 1 本もぎ取って頭上へ掲げさせる演出。
    // スタミナ・列車・マップは Feature が自分で探して繋ぐので、ここでは登録だけでよい。
    // 見た目と時間は「ゲーム設定」の Game.BananaHarvest.* から調整する。
    AddFeature(GameComponents::CreateBananaHarvestEffectFeature());

    // 駅で速度が落ちる瞬間に、駅・カメラ・速度計・音を同時に鳴らして理由を見せる演出。
    // 速度計を掴むので CreateSpeedGaugeFeature() より後に登録すること。
    // 強さは「ゲーム設定」の Game.StationSlowdown.* から調整する。
    AddFeature(GameComponents::CreateStationSlowdownEffectFeature());

    railView->AddComponent<GameComponents::RailViewComponent>(
        gridSize,
        railPath->GetComponent<GameComponents::RailPathComponent>(),
        railPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        railLeftPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        railRightPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        bridgePoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        mapGenerator->GetComponent<GameComponents::MapGeneratorComponent>(),
        gameCamera,
        playRailBuildSe,
        renderWorldDistance);

    // マップを描画するオブジェクトを追加
    auto* mapRenderer = CreateObject<GameSceneObject>("MapRenderer");
    mapRenderer->AddComponent<CoreEngine::TransformComponent>();
    auto* mapView = mapRenderer->AddComponent<GameComponents::MapViewComponent>(
        mapGenerator->GetComponent<GameComponents::MapGeneratorComponent>(),
        groundPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        groundSkirtPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        // 水は WaterWaveViewComponent が描くので、ここでは渡さない（二重描画になる）
        nullptr,
        stationPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        rockPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        hardRockPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        bananaTreePoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        grassPoolManager->GetComponent<GameComponents::ModelRenderPoolComponent>(),
        gameCamera,
        gridSize, renderWorldDistance);

    // 水マスを描画するオブジェクトを追加。
    // 地面と同じ「マスごとに1枚」だが、頂点シェーダーで波打たせるためプールではなく専用。
    auto* waterRenderer = CreateObject<GameSceneObject>("WaterWaveRenderer");
    waterRenderer->AddComponent<CoreEngine::TransformComponent>();
    waterRenderer->AddComponent<GameComponents::WaterWaveViewComponent>(
        mapGenerator->GetComponent<GameComponents::MapGeneratorComponent>(),
        gameCamera,
        gridSize, renderWorldDistance,
        ToUInt(GameComponents::GameSettings::WaterPoolCapacity.Get(), 1));

    // サルが増えた駅を弾ませる。描画は MapView が持つのでここで繋ぐ。
    hungerComponent->SetStationPopCallback(
        [mapView](int32_t stationX, int32_t stationZ) {
            mapView->PlayStationPop(stationX, stationZ);
        });
}

void GameScene::GameScene::OnUpdate() {
}
