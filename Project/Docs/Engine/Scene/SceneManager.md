# シーン管理

CoreEngine のシーンシステムは `BaseScene`（シーン基底クラス）と `SceneManager`（シーン管理）で構成されます。

## ヘッダ

```cpp
#include "Scene/BaseScene.h"
#include "Scene/SceneManager.h"
```

---

## BaseScene

シーンの共通処理を提供する基底クラスです。  
派生クラスは `OnInitialize()` / `OnUpdate()` をオーバーライドしてシーン固有のロジックを実装します。

### シーンの作成

```cpp
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

namespace CoreEngine {

class GameScene : public BaseScene {
public:
    void OnInitialize() override {
        // シーン名を設定（JSON保存に使用）
        SetSceneName("GameScene");

        // ゲームオブジェクトの生成
        auto player = CreateObject<PlayerObject>();
        player->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        player->SetActive(true);

        auto enemy = CreateObject<EnemyObject>("enemy.gltf");
        enemy->GetTransform().translate = { 10.0f, 0.0f, 5.0f };
        enemy->SetActive(true);

        // テクスチャの読み込み
        auto& texMgr = TextureManager::GetInstance();
        auto envMap = texMgr.Load("sky.hdr");

        // IBL（環境マップ）の設定
        auto ibl = engine_->GetComponent<IBLSystem>();
        IBLSystem::SetupParams iblParams;
        iblParams.environmentMap = envMap.texture.Get();
        iblParams.environmentMapSRV = envMap.gpuHandle;
        iblParams.environmentKey = "sky.hdr";
        ibl->Setup(iblParams);

        // SkyBox の作成
        auto skyBox = CreateObject<SkyBoxObject>();
        skyBox->SetTexture(envMap);
        skyBox->SetActive(true);

        // 衝突判定レイヤーの設定
        SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy);
    }

    void Draw() override {
        BaseScene::Draw();  // 必ず基底クラスの Draw を呼ぶ
    }

protected:
    void OnFinalize() override {
        // シーン固有の解放処理（Finalize() は final のためオーバーライド不可。
        // 基底クラスの呼び出しは不要）
    }

    void OnUpdate() override {
        // キーボード入力の取得
        auto keyboard = engine_->GetComponent<KeyboardInput>();
        if (!keyboard) return;

        if (keyboard->IsKeyTriggered(DIK_ESCAPE)) {
            sceneManager_->ChangeScene("TitleScene");
        }
    }
};

}
```

### BaseScene のオーバーライド可能メソッド

| メソッド | タイミング | 説明 |
|---------|-----------|------|
| `OnInitialize()` | 初期化時 | シーン固有の初期化。`CreateObject` やリソース読み込みをここで行う |
| `OnUpdate()` | 毎フレーム（GameObject更新前） | ゲームロジックの更新 |
| `OnLateUpdate()` | 毎フレーム（GameObject更新後） | 後処理（カメラ追従など） |
| `Draw()` | 描画時 | 描画処理（`BaseScene::Draw()` を呼ぶこと） |
| `OnFinalize()` | 終了時 | シーン固有のリソース解放（Feature 解放・GameObject クリアの前に呼ばれる） |

※ `Initialize()` / `Update()` / `Finalize()` は final のためオーバーライドできません。対応する `OnXxx()` フックを使用してください。

### BaseScene のヘルパーメソッド

| メソッド | 説明 |
|---------|------|
| `CreateObject<T>(args...)` | GameObjectを生成して自動登録。ポインタを返す |
| `SetSceneName(name)` | シーン名を設定（JSONファイルパスに使用） |
| `SetCollisionEnabled(layerA, layerB)` | レイヤー間の衝突判定を有効化 |
| `LoadObjectsFromJson()` | JSONからオブジェクトデータを復元（通常は自動呼び出し） |

### BaseScene の保護メンバー

| メンバー | 型 | 説明 |
|---------|-----|------|
| `engine_` | `EngineSystem*` | エンジンシステムへのポインタ |
| `cameraManager_` | `unique_ptr<CameraManager>` | カメラ管理 |
| `directionalLight_` | `DirectionalLightData*` | ディレクショナルライト（LightingFeature が生成） |
| `gameObjectManager_` | `GameObjectManager` | ゲームオブジェクト管理 |

### シーン横断機能（SceneFeature）

ライト・影・コリジョン・環境（SkyBox / 無限床 / 大気 / 雲）・グリッド・デバッグエディタ・BGM といった
共通処理は `ISceneFeature`（`Engine/Src/Scene/Feature/`）としてモジュール化されており、
BaseScene は登録された Feature を決まったフェーズでディスパッチするだけの薄いコンポーザーです。
エンジン機能をシーンへ組み込む場合は **BaseScene を編集せず Feature を追加**します。

```cpp
class MyFeature : public CoreEngine::ISceneFeature {
public:
    const char* GetName() const override { return "My"; }

    void Update(SceneContext& ctx, SceneUpdatePhase phase) override {
        if (phase != SceneUpdatePhase::PostLogic) {
            return;
        }
        // 毎フレーム処理（ctx.engine / ctx.gameObjectManager / ctx.gameViewCamera3D 等が使える）
    }
};

// 既定 Feature に追加する場合は BaseScene::RegisterDefaultFeatures() に登録、
// 特定シーンだけの場合は OnInitialize() 内で:
AddFeature(std::make_unique<MyFeature>());
```

| `SceneUpdatePhase` | タイミング | 既定 Feature の例 |
|---|---|---|
| `FrameStart` | カメラ更新後・`OnUpdate()` 前 | ライト更新＋影LVP、グリッド、デバッグエディタ |
| `PreObjectUpdate` | `OnUpdate()` 後・GameObject 更新前 | 無限床のカメラ追従 |
| `PostObjectUpdate` | GameObject 更新後・`OnLateUpdate()` 前 | コリジョン収集→判定 |
| `PostLogic` | `OnLateUpdate()` 後 | 大気散乱→雲の更新 |

同一フェーズ内の実行順は `AddFeature(feature, priority)` の priority 昇順（同値は登録順）で、
`RenderPipeline::AddPass` と同じ規約です。

---

## SceneManager

シーンの登録・切り替え・トランジションを管理するクラスです。

### シーンの登録と初期化

```cpp
// Framework::Initialize() 内で
sceneManager_ = std::make_unique<CoreEngine::SceneManager>();
sceneManager_->Initialize(GetEngineSystem());
GetEngineSystem()->SetSceneManager(sceneManager_.get());

// シーンの登録
sceneManager_->RegisterScene<TitleScene>("TitleScene");
sceneManager_->RegisterScene<GameScene>("GameScene");
sceneManager_->RegisterScene<ResultScene>("ResultScene");

// 初期シーンを設定（トランジションなし）
sceneManager_->SetInitialScene("TitleScene");
```

### シーン遷移

```cpp
// デフォルトトランジション（フェード）
sceneManager_->ChangeScene("GameScene");

// トランジションタイプと時間を指定
sceneManager_->ChangeScene("ResultScene",
    SceneTransition::TransitionType::Fade, 1.5f);

// トランジションなし（即時切り替え）
sceneManager_->ChangeScene("TitleScene",
    SceneTransition::TransitionType::None, 0.0f);
```

### SceneManager の主要メソッド

| メソッド | 説明 |
|---------|------|
| `RegisterScene<T>(name)` | シーンをファクトリ登録 |
| `SetInitialScene(name)` | 初期シーンを設定 |
| `ChangeScene(name)` | シーン遷移（デフォルトフェード） |
| `ChangeScene(name, type, duration)` | トランジション指定でシーン遷移 |
| `HasScene(name)` | シーンが登録されているか確認 |
| `GetCurrentSceneName()` | 現在のシーン名を取得 |
| `GetAllSceneNames()` | 全登録シーン名を取得 |
| `IsTransitioning()` | トランジション中か確認 |

### トランジションタイプ

| タイプ | 説明 |
|-------|------|
| `TransitionType::None` | トランジションなし（即座に切り替え） |
| `TransitionType::Fade` | フェードイン・フェードアウト（デフォルト） |
