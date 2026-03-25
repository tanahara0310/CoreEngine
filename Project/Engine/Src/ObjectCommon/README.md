# GameObject システム - 使用方法ガイド

## 概要

`GameObject` はゲームワールドに存在するすべてのオブジェクトの共通基底クラスです。  
`GameObjectManager` に登録することでライフサイクル（初期化・更新・描画・破棄）が自動管理されます。

---

## クラス継承ツリー

```
GameObject                          ← 共通基底（直接継承は特殊用途のみ）
├── ModelGameObject                 ← 3Dモデルを持つオブジェクトの中間基底
│   ├── AnimatedModelObject         ← スケルタルアニメーション付きモデル
│   │   ├── WalkModelObject
│   │   ├── SkeletonModelObject
│   │   └── SneakWalkModelObject
│   ├── FenceObject
│   ├── SphereObject
│   ├── TerrainObject
│   ├── ModelObject                 ← 動的にモデルパスを指定できる汎用型
│   └── AnimatedCubeObject
├── SpriteObject                    ← 2Dスプライト
├── SkyBoxObject                    ← スカイボックス（独自レンダリング）
└── ParticleSystem                  ← パーティクルシステム
```

---

## オブジェクトの生成方法

すべてのオブジェクトは `BaseScene` の `CreateObject<T>()` を通じて生成します。  
**`new` による直接生成は禁止です**（後述の注意事項を参照）。

### パターン A: モデルパスが固定のオブジェクト（最も一般的）

```cpp
// シーンの Initialize() 内で
auto fence = CreateObject<FenceObject>();
fence->GetTransform().translate = { 0.0f, 0.0f, 5.0f };
fence->SetActive(true);
```

`CreateObject<T>()` の内部で `Initialize()` が自動的に呼ばれます。  
モデルロード・テクスチャロード・トランスフォーム初期化はすべて自動実行されます。

---

### パターン B: 動的にモデルパスを指定するオブジェクト

`ModelObject` はコンストラクタでモデルパスを受け取ります。

```cpp
auto sphere = CreateObject<ModelObject>("sphere.obj");
sphere->GetTransform().translate = { 0.0f, 1.0f, 0.0f };
```

---

### パターン C: 後から設定が必要なオブジェクト

初期化後にセッターで追加設定します。

```cpp
// テクスチャは IBL 設定後に確定するため、セッターで渡す
auto skyBox = CreateObject<SkyBoxObject>();
skyBox->SetTexture(environmentMapTexture);
skyBox->SetActive(true);
```

---

### パターン D: 衝突判定が必要なオブジェクト

`CreateObject()` の後にコライダーを追加します。

```cpp
auto player = CreateObject<SphereObject>();
player->GetTransform().translate = { 0.0f, 1.0f, 0.0f };
player->AddSphereCollider(1.0f, CollisionLayer::Player);
```

---

## 新しいオブジェクトクラスの作り方

### ケース 1: モデルを持つ固定オブジェクト（推奨）

`ModelGameObject` を継承し、フックメソッドを override するだけです。

```cpp
// MyObject.h
#pragma once
#include "ObjectCommon/Model/ModelGameObject.h"

class MyObject : public CoreEngine::ModelGameObject {
public:
    const char* GetObjectName() const override { return "MyObject"; }

protected:
    std::string GetModelPath()   const override { return "mymodel.obj"; }
    std::string GetTexturePath() const override { return "mytexture.png"; }

    // 追加の初期化が必要なら OnInitialize() を override
    void OnInitialize() override {
        GetTransform().scale = { 2.0f, 2.0f, 2.0f };
    }

    // 毎フレームの処理が必要なら OnUpdate() を override
    void OnUpdate() override {
        // ゲームロジック
    }
};
```

使用例：
```cpp
auto obj = CreateObject<MyObject>();
```

#### 利用可能なフックメソッド一覧

| メソッド | 呼ばれるタイミング | 用途 |
|---------|-----------------|------|
| `GetModelPath()` | Initialize() 内 | ロードするモデルファイル名を返す |
| `GetTexturePath()` | Initialize() 内 | テクスチャファイル名を返す（空文字列でモデル組み込みテクスチャを使用） |
| `OnInitialize()` | Initialize() の末尾 | モデルロード後の追加初期化 |
| `OnUpdate()` | Update() の末尾 | 毎フレームのゲームロジック |
| `OnDraw()` | Draw() の末尾 | 描画後の追加処理 |

---

### ケース 2: 衝突イベントが必要なオブジェクト

```cpp
class EnemyObject : public CoreEngine::ModelGameObject {
public:
    void OnCollisionEnter(CoreEngine::GameObject* other) override {
        // 衝突開始
    }
    void OnCollisionStay(CoreEngine::GameObject* other) override {
        // 衝突継続中（毎フレーム）
    }
    void OnCollisionExit(CoreEngine::GameObject* other) override {
        // 衝突終了
    }

protected:
    std::string GetModelPath() const override { return "enemy.obj"; }
};
```

衝突判定を有効にするには、レイヤー設定とコライダー追加が必要です：

```cpp
// シーンの Initialize() 内
SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy);

auto enemy = CreateObject<EnemyObject>();
enemy->AddSphereCollider(1.0f, CollisionLayer::Enemy);
```

---

### ケース 3: モデルを持たない特殊オブジェクト

`SkyBoxObject` のように独自レンダリングが必要な場合は `GameObject` を直接継承します。

```cpp
class MySpecialObject : public CoreEngine::GameObject {
public:
    void Initialize() override {
        // GPU リソースの確保など
        // GetEngineSystem() で EngineSystem にアクセス可能
        auto* dxCommon = GetEngineSystem()->GetComponent<DirectXCommon>();
    }

    void Update() override {
        if (!IsActive()) return;
        // 毎フレームの処理
    }

    void Draw(const CoreEngine::ICamera* camera) override {
        if (!IsVisible() || !camera) return;
        // 独自描画処理
    }

    const char* GetObjectName() const override { return "MySpecialObject"; }
    CoreEngine::RenderPassType GetRenderPassType() const override {
        return CoreEngine::RenderPassType::Model; // 適切なパスを選択
    }
};
```

---

### ケース 4: 動的にモデルパスを変えたいオブジェクト

コンストラクタでパスを受け取る `ModelObject` のパターンを踏襲します。

```cpp
class ConfigurableObject : public CoreEngine::ModelGameObject {
public:
    explicit ConfigurableObject(const std::string& modelPath)
        : modelPath_(modelPath) {}

protected:
    std::string GetModelPath() const override { return modelPath_; }

private:
    std::string modelPath_;
};

// 使用例
auto obj = CreateObject<ConfigurableObject>("custom_model.obj");
```

---

## シリアライズ（JSON 保存・復元）

シーンの保存・復元に対応させるには `OnSerialize()` と `OnDeserialize()` を override します。

```cpp
class MyObject : public CoreEngine::ModelGameObject {
public:
    // transform + active は ModelGameObject が自動で保存するため、
    // 追加パラメータがなければ override 不要

    // 追加パラメータを保存したい場合:
    json OnSerialize() const override {
        json j = ModelGameObject::OnSerialize(); // 親の保存データを引き継ぐ
        j["myParam"] = myParam_;
        return j;
    }

    void OnDeserialize(const json& j) override {
        ModelGameObject::OnDeserialize(j);       // 親の復元処理を先に実行
        if (j.contains("myParam")) {
            myParam_ = j["myParam"].get<float>();
        }
    }

private:
    float myParam_ = 1.0f;
};
```

シリアライズを無効にしたいオブジェクトは `Initialize()` 内で設定します：

```cpp
void MyObject::Initialize() {
    // 通常の初期化処理...
    SetSerializeEnabled(false); // JSON 保存対象から除外
}
```

---

## ImGui デバッグ UI の拡張

デバッグビルドでオブジェクト固有のパラメータを ImGui に表示するには `DrawImGuiExtended()` を override します。

```cpp
#ifdef _DEBUG
bool MyObject::DrawImGuiExtended() {
    // ModelGameObject の場合は必ず先に親を呼ぶ（Transform + Material が表示される）
    bool changed = ModelGameObject::DrawImGuiExtended();

    // 独自パラメータを追加
    if (ImGui::DragFloat("My Param", &myParam_, 0.1f)) {
        changed = true;
    }

    return changed;
}
#endif
```

> **注意:** `ModelGameObject` を継承している場合、`ModelGameObject::DrawImGuiExtended()` を呼ばないと Transform と Material の表示が消えます。

---

## ライフサイクルの流れ

```
CreateObject<T>(コンストラクタ引数)
  └─ GameObjectManager::SpawnRaw()
       ├─ spawner_ の注入（Spawn<T>() が使えるようになる）
       ├─ 名前の自動付与（未設定の場合: "TypeName_0", "TypeName_1" ...）
       └─ Initialize() の自動呼出し
            └─ ModelGameObject::Initialize() の場合:
                 ├─ transform_.Initialize()    GPU バッファ確保
                 ├─ モデルロード（GetModelPath()）
                 ├─ テクスチャロード（GetTexturePath()）
                 └─ OnInitialize()             派生クラスの追加処理

毎フレーム（GameObjectManager::UpdateAll() から）
  └─ IsActive() && !IsMarkedForDestroy() && IsAutoUpdate() のとき Update() 呼出し

毎フレーム（RenderManager から）
  └─ IsActive() && IsVisible() のとき Draw() 呼出し

フレーム末（GameObjectManager::CleanupDestroyed() から）
  └─ IsMarkedForDestroy() のオブジェクトをメモリ解放
```

---

## Update() 中の安全なスポーン

`Update()` の中から `Spawn<T>()` を呼ぶと、pending キューに積まれて次フレームから有効になります。

```cpp
void MyObject::OnUpdate() {
    if (shouldSpawnBullet_) {
        // Update() 中でも安全に呼べる
        auto* bullet = Spawn<BulletObject>();
        bullet->SetDirection(GetWorldPosition());
    }
}
```

---

## オブジェクトの破棄

```cpp
// フレーム末に安全に破棄（Update() 内からも呼べる）
obj->Destroy();

// シーン全体をクリア（シーン遷移時に BaseScene が自動実行）
gameObjectManager_.Clear();
```

---

## ⚠️ 注意事項

### `new` による直接生成は禁止

```cpp
// ❌ NG: Initialize() が呼ばれず、GameObjectManager にも登録されない
auto* obj = new FenceObject();

// ✅ OK: 必ず CreateObject<T>() を使う
auto* obj = CreateObject<FenceObject>();
```

### `Initialize()` の手動呼出しは禁止（二重初期化になる）

```cpp
// ❌ NG: SpawnRaw が自動で呼ぶため、二重初期化になる
auto* obj = CreateObject<FenceObject>();
obj->Initialize(); // ← 呼んではいけない

// ✅ OK: CreateObject だけでよい
auto* obj = CreateObject<FenceObject>();
```

### 他の GameObject への参照はコンストラクタではなくセッターで渡す

```cpp
// ❌ NG: 相手オブジェクトがまだ生成されていない可能性がある
auto* cam = CreateObject<FollowCamera>(playerObj); // コンストラクタで渡す

// ✅ OK: 生成後にセッターで渡す
auto* cam = CreateObject<FollowCamera>();
cam->SetFollowTarget(playerObj);
```

### エンジン依存（DirectXCommon 等）は `Initialize()` 内で取得する

```cpp
// ❌ NG: コンストラクタで GetEngineSystem() は呼べない（まだ登録前）
MyObject() {
    auto* dx = GetEngineSystem()->GetComponent<DirectXCommon>(); // クラッシュ
}

// ✅ OK: Initialize() 内で取得する
void Initialize() override {
    auto* dx = GetEngineSystem()->GetComponent<DirectXCommon>(); // 安全
}
```

### `ModelGameObject` を継承するが `model_` を使わない設計は避ける

独自の頂点バッファや描画処理が必要な場合は `GameObject` を直接継承してください（`SkyBoxObject` がこのパターンの参考実装です）。

---

## ファイル構成

```
Engine/Src/ObjectCommon/
├── README.md                     # このファイル
├── GameObject.h / .cpp           # 共通基底クラス
├── GameObjectManager.h / .cpp    # オブジェクトの一元管理
├── IObjectSpawner.h              # Spawn<T>() のインターフェース
├── Model/
│   ├── ModelGameObject.h / .cpp  # 3Dモデル中間基底
│   └── AnimatedModelObject.h / .cpp # アニメーション付き中間基底
└── Sprite/
    └── SpriteObject.h / .cpp     # 2Dスプライト
```
