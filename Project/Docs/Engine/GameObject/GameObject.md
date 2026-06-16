# GameObject

`GameObject` はゲームワールドに存在するすべてのオブジェクトの共通基底クラスです。

## ヘッダ

```cpp
#include "ObjectCommon/GameObject.h"
```

## 概要

- 更新・描画・アクティブ制御・破棄などの共通機能を提供します
- 3Dモデルを持つオブジェクトは `ModelGameObject` を、スプライトは `SpriteObject` を経由して継承します
- シーン内で `CreateObject<T>()` を使って生成・登録します

## 継承階層

```
GameObject
├── ModelGameObject  ← 3Dモデルオブジェクト用
│   ├── PlayerObject (アプリ層)
│   ├── EnemyObject  (アプリ層)
│   └── ...
├── SpriteObject     ← 2Dスプライト用
└── ParticleSystem   ← パーティクル用
```

---

## ライフサイクル

| メソッド | タイミング | 説明 |
|---------|-----------|------|
| `Initialize()` | `CreateObject` 時に自動呼び出し | 初期化処理 |
| `Update()` | 毎フレーム（アクティブ時） | 更新処理 |
| `Draw(camera)` | 描画時（アクティブ時） | 描画処理 |
| `Destroy()` | 任意のタイミング | 削除マークを付ける（フレーム末に実際に削除） |

## アクティブ制御

```cpp
auto obj = CreateObject<ModelObject>("enemy.gltf");

// アクティブ設定（更新・描画のON/OFF）
obj->SetActive(true);
obj->SetActive(false);

// アクティブ状態を確認
if (obj->IsActive()) {
    // ...
}
```

## 破棄

```cpp
// 削除マークを付ける（Update内から安全に呼べる）
obj->Destroy();

// 削除マークの確認
if (obj->IsMarkedForDestroy()) {
    // フレーム末に削除される
}
```

## 描画制御

```cpp
// 描画順序の設定（小さいほど先に描画）
obj->SetRenderOrder(10);

// 描画順序のリセット（RenderPassTypeの優先度に戻す）
obj->ResetRenderOrder();
```

## コライダー（衝突判定）

```cpp
// 球体コライダーを追加
obj->AddSphereCollider(1.0f, CollisionLayer::Player);

// AABBコライダーを追加
obj->AddAABBCollider(Vector3{2.0f, 2.0f, 2.0f}, CollisionLayer::Enemy);

// コライダーの確認・取得
if (obj->HasCollider()) {
    auto* collider = obj->GetCollider();
}

// コライダーの削除
obj->RemoveCollider();
```

## 衝突イベント

派生クラスでオーバーライドして衝突時の処理を実装します。

```cpp
class PlayerObject : public CoreEngine::ModelGameObject {
public:
    void OnCollisionEnter(GameObject* other) override {
        // 衝突開始時（1回だけ呼ばれる）
    }

    void OnCollisionStay(GameObject* other) override {
        // 衝突中（毎フレーム呼ばれる）
    }

    void OnCollisionExit(GameObject* other) override {
        // 衝突終了時（1回だけ呼ばれる）
    }
};
```

## 名前とシリアライズ

```cpp
// 名前の設定・取得
obj->SetName("Player01");
std::string name = obj->GetName();

// シリアライズの有効/無効
obj->SetSerializeEnabled(true);  // JSONに保存される
obj->SetSerializeEnabled(false); // JSONに保存されない
```

### カスタムシリアライズ

```cpp
class MyObject : public CoreEngine::ModelGameObject {
public:
    json OnSerialize() const override {
        json j = ModelGameObject::OnSerialize();
        j["hp"] = hp_;
        j["speed"] = speed_;
        return j;
    }

    void OnDeserialize(const json& j) override {
        ModelGameObject::OnDeserialize(j);
        if (j.contains("hp")) hp_ = j["hp"];
        if (j.contains("speed")) speed_ = j["speed"];
    }

private:
    int hp_ = 100;
    float speed_ = 5.0f;
};
```

## エンジンシステムへのアクセス

```cpp
// GameObject内からエンジンシステムを取得
auto* engine = GetEngineSystem();
auto* keyboard = engine->GetComponent<KeyboardInput>();
```
