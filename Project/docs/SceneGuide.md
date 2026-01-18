# ?? シーンシステム使用ガイド

**CoreEngine シーン作成・管理の完全ガイド**

---

## 目次

1. [概要](#概要)
2. [基本的な使い方](#基本的な使い方)
3. [シーンライフサイクル](#シーンライフサイクル)
4. [GameObjectの作成と管理](#gameobjectの作成と管理)
5. [更新処理のパターン](#更新処理のパターン)
6. [実装例](#実装例)
7. [ベストプラクティス](#ベストプラクティス)
8. [トラブルシューティング](#トラブルシューティング)

---

## 概要

### システムアーキテクチャ

```
Framework (アプリケーション層)
    ↓
MyGame (ゲーム固有層)
    ↓
SceneManager (シーン管理層)
    ↓
BaseScene (シーン基底クラス)
    ↓
派生シーン (TestScene, ParticleTestScene等)
```

### 特徴

? **自動的なライフサイクル管理** - Initialize/Update/Draw/Finalizeが自動実行  
? **GameObjectの一元管理** - CreateObject()で簡単に生成  
? **メモリ安全性** - unique_ptrによる自動メモリ管理  
? **GPU同期** - ダブルバッファリングによる安全な削除  
? **LateUpdateパターン** - 更新順序の明確化  

---

## 基本的な使い方

### 1. シーンクラスの作成

**ヘッダーファイル (YourScene.h)**

```cpp
#pragma once
#include "Scene/BaseScene.h"
#include "EngineSystem/EngineSystem.h"

// 必要なGameObjectをインクルード
#include "ObjectCommon/SpriteObject.h"
#include "Engine/Particle/ParticleSystem.h"

/// @brief カスタムシーンクラス
class YourScene : public BaseScene {
public:
    /// @brief 初期化
    void Initialize(EngineSystem* engine) override;

    /// @brief 描画処理
    void Draw() override;

    /// @brief 解放
    void Finalize() override;

protected:
    /// @brief 更新処理（GameObjectの更新前）
    void OnUpdate() override;

    /// @brief 後処理（GameObjectの更新後、クリーンアップ前）
    void OnLateUpdate() override;

private:
    // シーン固有のメンバー変数
    ParticleSystem* particleSystem_ = nullptr;
    SpriteObject* playerSprite_ = nullptr;
};
```

**実装ファイル (YourScene.cpp)**

```cpp
#include "YourScene.h"

void YourScene::Initialize(EngineSystem* engine)
{
    // 1. 必ず最初にBaseScene::Initialize()を呼ぶ
    BaseScene::Initialize(engine);

    // 2. 必要なコンポーネントを取得
    auto dxCommon = engine_->GetComponent<DirectXCommon>();
    
    // 3. GameObjectの生成
    auto sprite = CreateObject<SpriteObject>();
    sprite->Initialize("Texture/player.png");
    sprite->SetActive(true);
    playerSprite_ = sprite;  // 生ポインタで保持（optional）
}

void YourScene::OnUpdate()
{
    // GameObjectの更新「前」に実行したい処理
    auto keyboard = engine_->GetComponent<KeyboardInput>();
    
    if (keyboard->IsKeyTriggered(DIK_SPACE)) {
        // プレイヤーのジャンプ処理など
    }
}

void YourScene::OnLateUpdate()
{
    // GameObjectの更新「後」に実行したい処理
    // 例: カメラ追従、衝突判定の後処理など
}

void YourScene::Draw()
{
    // 基底クラスの描画を呼ぶだけ（必須）
    BaseScene::Draw();
}

void YourScene::Finalize()
{
    // 特別な終了処理が必要な場合のみ実装
    // GameObjectは自動的にクリアされる
}
```

---

## シーンライフサイクル

### 実行フロー

```
┌─────────────────────────────────────────────────────────┐
│ 1. SceneManager::ChangeScene("YourScene")              │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 2. YourScene::Initialize(engine)                       │
│    ├─ BaseScene::Initialize(engine)                    │
│    │   ├─ カメラセットアップ                            │
│    │   ├─ ライトセットアップ                            │
│    │   └─ グリッドセットアップ (DEBUG)                 │
│    └─ シーン固有の初期化                                │
│        └─ CreateObject<T>() でGameObject生成           │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 3. 毎フレーム: BaseScene::Update() [final]             │
│    ├─ 共通処理（カメラ、ライト、ImGui）                │
│    ├─ OnUpdate()          ← 派生クラス固有の処理       │
│    ├─ UpdateAll()         ← 全GameObjectの更新         │
│    ├─ OnLateUpdate()      ← 派生クラスの後処理         │
│    └─ CleanupDestroyed()  ← 削除マークされた削除       │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 4. 毎フレーム: YourScene::Draw()                       │
│    └─ BaseScene::Draw()                                │
│        ├─ RegisterAllToRender()                        │
│        ├─ RenderManager::DrawAll()                     │
│        └─ ClearQueue()                                 │
└─────────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────────┐
│ 5. シーン切り替え時: YourScene::Finalize()             │
│    └─ BaseScene::Finalize()                            │
│        └─ gameObjectManager_.Clear()                   │
└─────────────────────────────────────────────────────────┘
```

---

## GameObjectの作成と管理

### 基本的な作成方法

```cpp
void YourScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    // ===== パターン1: 生ポインタを保持しない =====
    // シンプルで安全な方法
    auto enemy = CreateObject<EnemyObject>();
    enemy->Initialize();
    enemy->SetActive(true);
    // enemyは自動的に管理される
    
    // ===== パターン2: 生ポインタを保持する =====
    // 後でアクセスする必要がある場合
    auto player = CreateObject<PlayerObject>();
    player->Initialize();
    playerObject_ = player;  // メンバー変数に保存
    
    // ===== パターン3: コンストラクタ引数を渡す =====
    auto sprite = CreateObject<SpriteObject>();
    sprite->Initialize("Texture/bullet.png");
}
```

### GameObjectへのアクセス

```cpp
// ===== メンバー変数で保持する場合 =====
class YourScene : public BaseScene {
private:
    PlayerObject* player_ = nullptr;
};

void YourScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    auto player = CreateObject<PlayerObject>();
    player->Initialize();
    player_ = player;  // 生ポインタを保存
}

void YourScene::OnUpdate()
{
    // 安全にアクセス
    if (player_ && !player_->IsMarkedForDestroy()) {
        player_->SetPosition({0.0f, 1.0f, 0.0f});
    }
}
```

### GameObjectの削除

```cpp
void YourScene::OnUpdate()
{
    auto keyboard = engine_->GetComponent<KeyboardInput>();
    
    if (keyboard->IsKeyTriggered(DIK_D)) {
        // 削除マークを付ける（フレーム終了時に自動削除）
        if (enemy_) {
            enemy_->Destroy();
        }
    }
}

// ?? 注意: 削除後にアクセスしないこと
void YourScene::OnLateUpdate()
{
    // Destroy()後、同フレーム内はまだアクセス可能
    if (enemy_ && !enemy_->IsMarkedForDestroy()) {
        // OK: まだ削除されていない
    }
    
    // 次フレームでは enemy_ は無効なポインタになる
}
```

---

## 更新処理のパターン

### Update()の実行順序

```cpp
class BaseScene : public IScene {
public:
    // ? Update()はfinalなのでオーバーライド不可
    virtual void Update() override final;

protected:
    // ? これらをオーバーライドする
    virtual void OnUpdate() {}
    virtual void OnLateUpdate() {}
};
```

### OnUpdate() vs OnLateUpdate()

| タイミング | 用途 | 例 |
|----------|------|---|
| **OnUpdate()** | GameObjectの更新「前」 | 入力処理、状態遷移 |
| **OnLateUpdate()** | GameObjectの更新「後」 | カメラ追従、衝突後処理 |

### 実装例

```cpp
void YourScene::OnUpdate()
{
    // ===== GameObjectの更新前に実行 =====
    
    auto keyboard = engine_->GetComponent<KeyboardInput>();
    
    // シーン切り替え
    if (keyboard->IsKeyTriggered(DIK_TAB)) {
        if (sceneManager_) {
            sceneManager_->ChangeScene("NextScene");
        }
        return;  // 早期リターンしてもOK
    }
    
    // プレイヤー入力
    if (player_) {
        Vector3 move = {0.0f, 0.0f, 0.0f};
        if (keyboard->IsKeyPressed(DIK_W)) move.z += 1.0f;
        if (keyboard->IsKeyPressed(DIK_S)) move.z -= 1.0f;
        if (keyboard->IsKeyPressed(DIK_A)) move.x -= 1.0f;
        if (keyboard->IsKeyPressed(DIK_D)) move.x += 1.0f;
        
        player_->SetMoveDirection(move);
    }
}

void YourScene::OnLateUpdate()
{
    // ===== GameObjectの更新後に実行 =====
    
    // カメラがプレイヤーを追従
    if (player_ && !player_->IsMarkedForDestroy()) {
        Vector3 playerPos = player_->GetWorldPosition();
        
        auto camera = cameraManager_->GetActiveCamera(CameraType::Camera3D);
        if (camera) {
            camera->SetTarget(playerPos);
        }
    }
}
```

---

## 実装例

### 例1: シンプルなゲームシーン

```cpp
// GameScene.h
#pragma once
#include "Scene/BaseScene.h"

class GameScene : public BaseScene {
public:
    void Initialize(EngineSystem* engine) override;
    void Draw() override;
    void Finalize() override;

protected:
    void OnUpdate() override;

private:
    SpriteObject* background_ = nullptr;
    TextObject* scoreText_ = nullptr;
    int score_ = 0;
};

// GameScene.cpp
#include "GameScene.h"

void GameScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    // 背景スプライト
    auto bg = CreateObject<SpriteObject>();
    bg->Initialize("Texture/background.png");
    bg->GetSpriteTransform().scale = {1.0f, 1.0f, 1.0f};
    background_ = bg;
    
    // スコア表示
    auto text = CreateObject<TextObject>();
    text->Initialize("C:/Windows/Fonts/arial.ttf", 32, "Score");
    text->SetText("Score: 0");
    text->GetTransform().translate = {50.0f, 50.0f, 0.0f};
    text->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    scoreText_ = text;
}

void GameScene::OnUpdate()
{
    auto keyboard = engine_->GetComponent<KeyboardInput>();
    
    // スコア加算（スペースキー）
    if (keyboard->IsKeyTriggered(DIK_SPACE)) {
        score_ += 10;
        if (scoreText_) {
            scoreText_->SetText("Score: " + std::to_string(score_));
        }
    }
}

void GameScene::Draw()
{
    BaseScene::Draw();
}

void GameScene::Finalize()
{
    // 特別な処理がなければ空でOK
}
```

### 例2: パーティクルエフェクトを使用

```cpp
void EffectScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    auto dxCommon = engine_->GetComponent<DirectXCommon>();
    auto resourceFactory = engine_->GetComponent<ResourceFactory>();
    
    // パーティクルシステム作成
    auto particle = CreateObject<ParticleSystem>();
    particle->Initialize(dxCommon, resourceFactory, "Explosion");
    particle->SetEmitterPosition({0.0f, 0.0f, 0.0f});
    particle->SetBlendMode(BlendMode::kBlendModeAdd);
    
    // エミッション設定
    auto& emissionModule = particle->GetEmissionModule();
    auto emissionData = emissionModule.GetEmissionData();
    emissionData.rateOverTime = 50;
    emissionModule.SetEmissionData(emissionData);
    
    // 再生開始
    particle->Play();
    particleSystem_ = particle;
}
```

### 例3: 3Dモデルオブジェクト

```cpp
void ModelScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    // カスタムGameObjectの作成
    auto character = CreateObject<CharacterObject>();
    character->Initialize();
    character->SetPosition({0.0f, 0.0f, 0.0f});
    character->SetActive(true);
    character_ = character;
}

void ModelScene::OnUpdate()
{
    auto keyboard = engine_->GetComponent<KeyboardInput>();
    
    if (character_) {
        // キー入力で移動
        Vector3 velocity = {0.0f, 0.0f, 0.0f};
        if (keyboard->IsKeyPressed(DIK_W)) velocity.z = 0.1f;
        if (keyboard->IsKeyPressed(DIK_S)) velocity.z = -0.1f;
        
        character_->AddVelocity(velocity);
    }
}
```

---

## ベストプラクティス

### ? 推奨される書き方

```cpp
// 1. Initialize()で必ずBaseScene::Initialize()を最初に呼ぶ
void YourScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);  // ← 必須
    
    // シーン固有の初期化...
}

// 2. Draw()では基底クラスを呼ぶだけ
void YourScene::Draw()
{
    BaseScene::Draw();  // ← これだけでOK
}

// 3. GameObjectへのアクセスは安全に
void YourScene::OnUpdate()
{
    if (player_ && !player_->IsMarkedForDestroy()) {
        player_->Update();
    }
}

// 4. CreateObject()の戻り値は必要なときだけ保持
auto enemy = CreateObject<EnemyObject>();
enemy->Initialize();
// enemyは自動的に管理される
```

### ? 避けるべき書き方

```cpp
// 1. BaseScene::Update()を呼んではいけない
void YourScene::OnUpdate()
{
    BaseScene::Update();  // ? 無限再帰！
}

// 2. Update()をオーバーライドしない
void YourScene::Update() override  // ? finalなのでコンパイルエラー
{
    // ...
}

// 3. GameObjectを手動でdeleteしない
void YourScene::OnUpdate()
{
    delete player_;  // ? 二重解放の危険！
}

// 4. 削除されたGameObjectにアクセスしない
void YourScene::OnUpdate()
{
    enemy_->Destroy();
}

void YourScene::OnLateUpdate()
{
    // 同じフレーム内ならOK
    if (enemy_ && !enemy_->IsMarkedForDestroy()) {
        enemy_->DoSomething();  // ? OK
    }
}

// 次フレーム
void YourScene::OnUpdate()
{
    enemy_->DoSomething();  // ? クラッシュ！既に削除済み
}
```

---

## トラブルシューティング

### Q1: スタックオーバーフローが発生する

**原因:** `OnUpdate()`内で`BaseScene::Update()`を呼んでいる

```cpp
// ? 間違い
void YourScene::OnUpdate()
{
    BaseScene::Update();  // 無限再帰！
}

// ? 正解
void YourScene::OnUpdate()
{
    // BaseScene::Update()は呼ばない
    // シーン固有の処理のみ
}
```

### Q2: GameObjectが描画されない

**チェックリスト:**
1. `SetActive(true)`を呼んでいるか？
2. `Draw()`で`BaseScene::Draw()`を呼んでいるか？
3. カメラが正しく設定されているか？

```cpp
void YourScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    auto obj = CreateObject<YourObject>();
    obj->Initialize();
    obj->SetActive(true);  // ← これを忘れずに
}

void YourScene::Draw()
{
    BaseScene::Draw();  // ← これも必須
}
```

### Q3: ポインタがnullになる

**原因:** GameObjectが削除された後にアクセスしている

```cpp
// ? 安全な方法
void YourScene::OnUpdate()
{
    if (enemy_ && !enemy_->IsMarkedForDestroy()) {
        enemy_->Attack();
    }
}
```

### Q4: メモリリークが発生する

**原因:** 手動で`new`したオブジェクトを解放していない

```cpp
// ? 間違い
void YourScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    customData_ = new CustomData();  // メモリリーク！
}

// ? 正解
class YourScene : public BaseScene {
private:
    std::unique_ptr<CustomData> customData_;
};

void YourScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    customData_ = std::make_unique<CustomData>();  // 自動解放
}
```

---

## まとめ

### シーン作成の5ステップ

1. **ヘッダーファイル作成** - `BaseScene`を継承
2. **Initialize()実装** - `BaseScene::Initialize()`を最初に呼ぶ
3. **OnUpdate()実装** - シーン固有の更新処理
4. **Draw()実装** - `BaseScene::Draw()`を呼ぶだけ
5. **SceneManagerに登録** - `RegisterScene<YourScene>("SceneName")`

### 重要なポイント

? `Update()`はfinalなので、`OnUpdate()`/`OnLateUpdate()`を使う  
? `BaseScene::Update()`は**絶対に**呼ばない  
? GameObjectは`CreateObject<T>()`で作成  
? 削除は`Destroy()`を呼ぶだけ  
? 生ポインタの保持は必要最小限に  

---

**Happy Coding! ??**
