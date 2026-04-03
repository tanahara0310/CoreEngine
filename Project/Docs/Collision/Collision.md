# 衝突判定

CoreEngine の衝突判定システムは `Collider` / `CollisionManager` / `CollisionLayer` で構成されます。

## ヘッダ

```cpp
#include "Collider/Collider.h"
#include "Collider/CollisionManager.h"
#include "Collider/CollisionConfig.h"
#include "Collider/CollisionLayer.h"
```

## 概要

- 球体（Sphere）と AABB の 2 種類のコライダーに対応しています
- レイヤーベースの衝突フィルタリングが可能です
- `OnCollisionEnter` / `OnCollisionStay` / `OnCollisionExit` のイベントコールバックをサポートします
- `GameObject` の簡易 API から手軽にコライダーを追加できます

## コライダーの追加

### GameObject の簡易 API

```cpp
// 球体コライダーの追加
auto player = CreateObject<PlayerObject>();
player->AddSphereCollider(1.0f, CoreEngine::CollisionLayer::Player);

// AABB コライダーの追加
auto wall = CreateObject<WallObject>();
wall->AddAABBCollider(
    CoreEngine::Vector3{ 2.0f, 3.0f, 0.5f },  // サイズ
    CoreEngine::CollisionLayer::Environment
);

// コライダーの確認
if (player->HasCollider()) {
    auto* collider = player->GetCollider();
}

// コライダーの削除
player->RemoveCollider();
```

## レイヤー設定

`BaseScene` 内でレイヤー間の衝突判定を有効にします:

```cpp
void GameScene::OnInitialize() {
    // Player と Enemy の衝突を有効化
    SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy);

    // Player と Item の衝突を有効化
    SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Item);

    // PlayerBullet と Enemy の衝突を有効化
    SetCollisionEnabled(CollisionLayer::PlayerBullet, CollisionLayer::Enemy);
}
```

### CollisionLayer 一覧

| レイヤー | 説明 |
|---------|------|
| `Default` | デフォルト（汎用） |
| `Player` | プレイヤー |
| `Enemy` | 敵 |
| `PlayerBullet` | プレイヤーの弾 |
| `EnemyBullet` | 敵の弾 |
| `Boss` | ボス |
| `BossBullet` | ボスの弾 |
| `BossAttack` | ボスの攻撃判定 |
| `Item` | アイテム |
| `Environment` | 環境オブジェクト（壁など） |

## 衝突イベント

`GameObject` の派生クラスで衝突イベントをオーバーライドします:

```cpp
class PlayerObject : public CoreEngine::ModelGameObject {
public:
    void OnCollisionEnter(GameObject* other) override {
        // 衝突開始（最初の1フレームのみ）
        if (auto* enemy = dynamic_cast<EnemyObject*>(other)) {
            TakeDamage(enemy->GetAttackPower());
        }
    }

    void OnCollisionStay(GameObject* other) override {
        // 衝突継続中（毎フレーム）
    }

    void OnCollisionExit(GameObject* other) override {
        // 衝突終了（離れた1フレームのみ）
    }
};
```

## 使用例：完全な衝突判定セットアップ

```cpp
class CollisionTestScene : public CoreEngine::BaseScene {
    void OnInitialize() override {
        SetSceneName("CollisionTest");

        // レイヤー間の衝突を有効化
        SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy);

        // プレイヤーの作成
        auto player = CreateObject<SphereObject>();
        player->GetTransform().translate = { 0.0f, 0.0f, 0.0f };
        player->AddSphereCollider(1.0f, CollisionLayer::Player);
        player->SetActive(true);

        // 敵の作成
        auto enemy = CreateObject<SphereObject>();
        enemy->GetTransform().translate = { 5.0f, 0.0f, 0.0f };
        enemy->AddSphereCollider(1.0f, CollisionLayer::Enemy);
        enemy->SetActive(true);
    }
};
```

## コライダーの動的制御

```cpp
// コライダーの有効/無効
auto* collider = obj->GetCollider();
collider->SetEnabled(false);  // 一時的に無効化
collider->SetEnabled(true);   // 再度有効化

// サイズの変更
collider->SetRadius(2.0f);    // 球体コライダーの半径変更
collider->SetSize({3.0f, 3.0f, 3.0f}); // AABB のサイズ変更
```
