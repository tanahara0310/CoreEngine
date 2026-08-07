# 衝突判定

CoreEngine の衝突判定システムは `Collider` / `CollisionWorld` / `CollisionLayer` で構成されます。

> **リファクタリング完了（Phase 0〜6）。** 経緯と設計判断は
> [Collision_Refactoring_Plan.md](Collision_Refactoring_Plan.md) にまとめてあります。
> 挙動の実測は `CollisionTestScene`（Scene Manager タブから切り替え）で確認できます。
> 未実装の機能は [現在の制限](#現在の制限) を参照してください。

## ヘッダ

```cpp
#include "Collision/Collider.h"
#include "Collision/CollisionWorld.h"
#include "Collision/CollisionConfig.h"
#include "Collision/CollisionLayer.h"
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

// コライダーの確認（先頭のコライダーが返る）
if (player->HasCollider()) {
    auto* collider = player->GetCollider();
}

// コライダーの削除（すべて外す）
player->RemoveCollider();
```

**`AddXxxCollider` は呼ぶたびに本数が増えます。** 置き換えたい場合は
`RemoveCollider()` してから呼んでください。

### 複数のコライダーを持たせる

本体判定と攻撃判定を別レイヤーで持つ、部位ごとに判定を置く、といった場合は
`GetColliders()`（`ColliderComponent`）を使います。**ローカルオフセット**を指定できます。

```cpp
// 本体（中心）
boss->AddSphereCollider(1.0f, CollisionLayer::Boss);

// 攻撃判定（本体から +X に 2.5 ずらした位置）
boss->GetColliders().AddSphere(1.0f, CollisionLayer::BossAttack, { 2.5f, 0.0f, 0.0f });

// ボックスも同様
boss->GetColliders().AddBox({ 2.0f, 1.0f, 1.0f }, CollisionLayer::BossAttack, { 0.0f, 2.0f, 0.0f });

// 本数と個別アクセス
size_t count = boss->GetColliders().Count();
Collider* body = boss->GetColliders().Get(0);
```

`Add` 系が返す参照は、その後コライダーを追加しても無効化されません。
**同じオブジェクトが持つコライダー同士は判定されません**（自己衝突しません）。

オフセットにもオーナーのスケールが乗ります。

## レイヤー設定

`BaseScene` 内でレイヤー間の衝突判定を有効にします。

初期状態では **`Default` 行だけが他のレイヤーと衝突する設定**（`Default` × `Default` を含む）で、
それ以外の組み合わせはすべて無効です。使うペアは明示的に有効化してください。

```cpp
void GameScene::OnInitialize() {
    // Player と Enemy の衝突を有効化（第 3 引数を省略すると true）
    SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Enemy);

    // 明示的に無効化することもできる
    SetCollisionEnabled(CollisionLayer::Player, CollisionLayer::Item, false);

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

### 接触情報を受け取る

法線・貫通深度・接触点や「どのコライダーに当たったか」が要る場合は
`CollisionInfo` を受け取るオーバーロードを使います。

```cpp
void OnCollisionEnter(const CoreEngine::CollisionInfo& info) override {
    // info.normal … 自分から相手へ向かう向き（正規化済み）
    // info.depth  … 貫通深度
    // info.point  … 代表接触点
    // info.selfCollider / info.otherCollider … どのコライダー同士か

    Knockback(info.normal * -1.0f);   // 相手と逆方向へ弾かれる
}
```

判定システムは常にこちらを呼びます。**既定実装が `OnCollisionEnter(GameObject*)` へ
転送する**ので、相手だけ分かればよい場合は従来どおりで構いません。
両方 override する場合は、`CollisionInfo` 版から
`GameObject::OnCollisionEnter(info)` を明示的に呼べば旧版へも流れます。

`Exit` は接触が切れた後に呼ばれるため、`normal` はゼロ・`depth` は 0 です。

---

## 押し出し（めり込み解消）

既定ではコライダーは**トリガー（通知専用）**で、重なっても位置は動きません。
押し戻したい場合は両方のコライダーで `SetTrigger(false)` にします。

```cpp
// 壁: 押し返されない側
wall->AddAABBCollider({ 2.0f, 4.0f, 2.0f }, CollisionLayer::Environment);
wall->GetCollider()->SetTrigger(false);
wall->GetCollider()->SetStatic(true);    // これが無いと壁ごと動く

// プレイヤー: 押し戻される側
player->AddSphereCollider(1.0f, CollisionLayer::Player);
player->GetCollider()->SetTrigger(false);
```

| 条件 | 挙動 |
|---|---|
| どちらかがトリガー | 押し出さない（通知のみ） |
| 両方が動ける | 半分ずつ分担して離れる |
| 片方が `SetStatic(true)` | 動ける方だけを全量押し出す |
| 両方とも動かせない | 何もしない |

押し出しは `GameObject::TryApplyCollisionPush()` を通ります。既定は「動かせない」
（`false` を返す）で、`ModelGameObject` が `translate` を更新します。独自の移動方法を
持つオブジェクトはこれをオーバーライドしてください。

**押し出しはコールバックより先に実行されます。** コールバックに渡る `CollisionInfo` は
解決前の値（どれだけめり込んでいたか）です。

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

`SetRadius` は球コライダー、`SetSize` は Box コライダーにしか効きません。
形状が違うときは**何も起きません**（エラーにもなりません）。
形状ごと差し替えたい場合は `SetShape(CollisionShape::MakeBox(...))` を使います。

`SetRadius` / `SetSize` に渡すのは**ローカル値**です。判定にはオブジェクトの
スケールが自動で掛かります（後述）。

---

## スケールの扱い

コライダーのサイズにはオーナーの**ワールドスケールが自動で乗ります**。

- 球: 最大軸のスケールを採用（非等倍では球でいられないため、安全側に大きい方を取る）
- AABB: 各軸のスケールを成分ごとに乗算

スケールは `GameObject::GetWorldScale()` から取ります。既定は等倍で、
`ModelGameObject` はワールド行列の基底ベクトル長から求める（＝親階層のスケールも含む）
実装を持っています。独自の `GameObject` 派生でスケールを反映したい場合は
`GetWorldScale()` をオーバーライドしてください。

なお**回転は反映されません**（OBB 未対応。AABB は常に軸平行）。

---

## 独自の GameObject 派生にコライダーを付ける

`GameObject::GetWorldPosition()` は**純粋仮想**です。当たり判定はこの値だけを
位置ソースにするため、実装を忘れるとコンパイルエラーになります
（以前は基底が原点を返していたため、実装忘れが「全員が原点で衝突する」という
無音のバグになっていました）。

```cpp
class MyObject : public CoreEngine::GameObject {
public:
    CoreEngine::Vector3 GetWorldPosition() const override { return position_; }
    // スケールも判定に効かせたい場合のみ
    CoreEngine::Vector3 GetWorldScale() const override { return scale_; }
};
```

位置という概念を持たないオブジェクト（`LineDrawable` / `GridRenderer` など）は
`{}` を返すよう明示してあります。これらにコライダーを付けないでください。

---

## 破棄・着脱まわりの保証

- 接触中の相手が `Destroy()` / `SetActive(false)` / `SetEnabled(false)` /
  `RemoveCollider()` のいずれで判定対象から外れても、**生き残った側に
  `OnCollisionExit` が飛びます**。
- **衝突コールバックの中から `RemoveCollider()` / `AddSphereCollider()` を呼んでも安全です。**
  コライダーの実体はフレーム末（`GameObjectManager::CleanupDestroyed`）まで保持され、
  判定ループが持っている生ポインタが宙に浮きません。

---

## 現在の制限

実バグは Phase 1 で修正済みですが、以下は**機能として未実装**です。
実装計画は [Collision_Refactoring_Plan.md](Collision_Refactoring_Plan.md) を参照してください。

- **コライダーの形状は球と Box のみ**です。`Math/Geometry` にはカプセルの交差関数が
  ありますが、`AABB × Capsule` が未実装なのでコライダーの形状としては選べません。
  OBB（回転するボックス）もありません。
- **押し出しは単発解決**です（反復ソルバではありません）。3 つ以上のオブジェクトが
  同時に押し合う状況では 1 フレームで収束しません。跳ね返り・摩擦・速度の扱いもありません。
- **押し出しは親を持つオブジェクトを想定していません**。ワールドの移動量をローカル
  `translate` にそのまま足すため、親の回転・スケールは考慮されません。
- **押し出しは単発解決**で反復ソルバではありません（再掲）。
- **クエリはコライダーの線形走査**です。ブロードフェーズの空間構造は判定にしか使っておらず、
  レイキャスト・オーバーラップの高速化には使っていません。
- **静的コライダーも毎フレーム空間構造へ入れ直しています**（ダーティ時のみ再挿入する
  最適化は未実装）。

---

## シーンへの問い合わせ（レイキャスト / オーバーラップ）

`BaseScene::GetCollisionWorld()` から `CollisionWorld` を取得して問い合わせます。

```cpp
#include "Collision/CollisionWorld.h"

CoreEngine::CollisionWorld* world = GetCollisionWorld();

// 最も近いヒットを取る
CoreEngine::RaycastHit hit;
if (world->Raycast(CoreEngine::Geometry::Ray{ origin, direction }, 100.0f,
                   CoreEngine::CollisionWorld::kAllLayers, &hit)) {
    // hit.collider / hit.object / hit.distance / hit.point / hit.normal
}

// 範囲内のコライダーを集める
std::vector<CoreEngine::Collider*> found;
world->OverlapSphere(CoreEngine::Geometry::Sphere{ center, 5.0f },
                     CoreEngine::CollisionWorld::kAllLayers, found);
```

`layerMask` は「bit n が立っていれば `CollisionLayer(n)` を対象にする」ビット集合です。
`CollisionConfig::GetLayerMask(layer)` で「そのレイヤーが衝突しうる相手」のマスクを取れます。

> **注意**: 問い合わせは**直近の判定フェーズ（`PostObjectUpdate`）時点の登録内容**を見ます。
> `OnUpdate`（判定より前）から呼ぶと 1 フレーム前の状態になります。

---

## ブロードフェーズ

判定は「ブロードフェーズ（AABB とレイヤーで候補を絞る）→ ナローフェーズ（実形状で判定）」
の 2 段です。ブロードフェーズは切り替えられます。

```cpp
world->SetBroadPhase(CollisionWorld::BroadPhaseType::UniformGrid);
world->SetGridCellSize(8.0f);   // よくある物体サイズの 1〜2 倍が目安
```

| 種類 | 向き |
|---|---|
| `BruteForce`（既定） | 件数が少ないうち。空間分割の構築コストが無いぶん速い |
| `UniformGrid` | 件数が増えたとき。セルサイズの調整が要る |

実装は `Collider` を知らない純データ設計（`BroadPhase::Proxy`）なので、
`GameObject` を作らずに単体テストできます。両実装が同じ候補ペアを返すことは
回帰テスト U16 が見張っています。

---

## 幾何演算を直接使う

形状同士の交差やレイ判定は、コライダーを通さずに `Math/Geometry` を直接呼べます。
純粋幾何レイヤなので `GameObject` にもレンダラにも依存しません。

```cpp
#include "Math/Geometry/Intersect.h"
#include "Math/Geometry/RayCast.h"

using namespace CoreEngine;

// 交差判定（Contact を渡すと法線・貫通深度・接触点が取れる）
Geometry::Contact contact;
if (Geometry::Intersect(Geometry::Sphere{ pos, 1.0f }, aabb, &contact)) {
    // contact.normal は A から B へ押し出す向き
    // B を normal 方向へ contact.depth だけ動かすと分離する
}

// Contact が不要なら省略できる（接触情報を計算しない高速パスになる）
if (Geometry::Intersect(sphereA, sphereB)) { /* ... */ }

// レイキャスト
Geometry::RayHit hit;
if (Geometry::Raycast(Geometry::Ray{ origin, dir }, aabb, &hit)) {
    // hit.point / hit.normal / hit.distance
}
```

| ヘッダ | 内容 |
|---|---|
| `Math/Geometry/Shapes.h` | `AABB` / `Sphere` / `Capsule` / `LineSegment` / `Ray` / `Plane` |
| `Math/Geometry/Intersect.h` | `Intersect()` / `Contains()` / `Contact` |
| `Math/Geometry/RayCast.h` | `Raycast()` / `RaycastTriangle()` / `RayHit` |
| `Math/Geometry/Distance.h` | 距離・最近接点 |

**`Plane` の符号規約**: `dot(normal, p) + d = 0` が平面上。`DistanceTo()` が正なら法線側です。
以前は `Frustum.h` と `CollisionUtils` に符号が逆の `Plane` が 2 つありました。

**1 つの形状ペアの交差判定は 1 箇所にしかありません。** 対称なペア（`AABB`×`Sphere` など）は
引数を入れ替えて転送し、`Contact::normal` だけを反転しています。新しい形状を足すときも
この方針を守ってください。

---

## デバッグ表示とエディタ

### コライダーのワイヤ表示

Engine Settings の CVar から切り替えます。

| CVar | 既定 | 内容 |
|---|---|---|
| `r.Collision.DebugDraw` | false | コライダーのワイヤ表示 |
| `r.Collision.DebugDrawOnlyColliding` | false | 接触中のものだけ描く |
| `r.Collision.DebugColorIdle` / `ColorHit` / `ColorTrigger` | 緑 / 赤 / 青 | 線色 |
| `r.Collision.DebugLineAlpha` | 0.85 | 線の不透明度 |
| `r.Collision.DebugSphereSegments` | 16 | 球の分割数 |
| `r.Collision.BroadPhase` | 0 | 0=総当たり / 1=一様グリッド |
| `r.Collision.GridCellSize` | 8.0 | グリッドのセルサイズ |

色は状態で変わります（**接触中=赤 / トリガー=青 / 通常=緑**）。

描画は `ColliderDebugRenderer` が担当し、**`Collider` 側はレンダラを知りません**。
描く側がコライダーを見に行く向きです。

### Inspector

オブジェクトを選択すると、インスペクタの **コライダータブ**（5 番目）で
形状・サイズ・オフセット・レイヤー・トリガー/静止フラグを編集できます。
球や箱の追加、全削除もここから行えます。スケール適用後の実効 AABB も表示されます。

### コリジョンマトリクス

Engine Settings の **Collision Matrix** でレイヤー間の衝突可否を格子状に編集できます。

> **保存されません。** マトリクスはシーンごとに `OnInitialize()` の `SetCollisionEnabled()` で
> 組み立てるものです。保存した設定で上書きすると「コードに書いてあるのに違う挙動になる」
> 状態を作ってしまうため、このパネルは実行中の調整用と位置づけています。
> 確定した内容はシーンのコードへ書き戻してください。

---

## 回帰テストシーン

`CollisionTestScene`（`Application/Src/Scenes/CollisionTestScene/`）が
Enter/Stay/Exit・レイヤーフィルタ・破棄時の挙動・スケール反映を毎回同じ
フレームスケジュールで自動実行します。

- 切り替え: 実行中に **Scene Manager タブ**から `CollisionTestScene` を選択
- 結果表示: メニューの **Application → Collision Test** パネル（PASS / FAIL の一覧）
- ログ: `Cache/logs/Game/Physics/Physics_*.log` に判定結果が残る
- `Tab` キーまたはパネルのボタンでテストをリスタート

コライダー周りに手を入れたら、このシーンで PASS / FAIL の内訳が変わっていないことを
確認してください。
