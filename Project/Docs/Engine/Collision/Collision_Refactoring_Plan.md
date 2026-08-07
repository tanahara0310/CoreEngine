# 当たり判定システム リファクタリング計画

作成日: 2026-08-06
対象ブランチ: `future/water-refactoring`
レビュー対象: `Engine/Src/Collider/*`, `Engine/Src/Utility/Collision/*`, `Engine/Src/Scene/Feature/CollisionFeature.*`, `Engine/Src/TileMap/TileCollider.*`, `Engine/Src/GameObject/*`（コライダー統合部）, `Engine/Src/Editor/ImGui/ObjectSelector.cpp`（独自レイ判定）

---

## 1. 現状の全体マップ

| 層 | ファイル | 役割 |
|---|---|---|
| 形状コライダー | `Engine/Src/Collider/Collider.h/.cpp` | 抽象基底。`CheckCollision(Collider*)` 純粋仮想、Enter/Stay/Exit の owner 転送、レイヤー・有効フラグ |
| | `Engine/Src/Collider/SphereCollider.*` | 半径のみ。相手型を `if/else` で分岐して `CollisionUtils` を呼ぶ |
| | `Engine/Src/Collider/AABBCollider.*` | サイズのみ。`GetMin/GetMax = pos ± size*0.5`。`_DEBUG` 限定の `DrawDebug` |
| フィルタ | `Engine/Src/Collider/CollisionLayer.h` | 10 レイヤーの enum（`Count` 終端） |
| | `Engine/Src/Collider/CollisionConfig.*` | `bool matrix_[Count][Count]`、対称書き込み |
| 判定ループ | `Engine/Src/Collider/CollisionManager.*` | 全ペア総当たり、`previousCollisions_`（`unordered_set<pair<Collider*,Collider*>>`）差分で Enter/Stay/Exit |
| シーン統合 | `Engine/Src/Scene/Feature/CollisionFeature.*` | `PostObjectUpdate` で 収集 → 判定。`CollisionConfig` と `CollisionManager` を値で所有 |
| GameObject 統合 | `GameObject.h/.cpp` | `unique_ptr<Collider> collider_` を **1 本**、`AddSphereCollider` / `AddAABBCollider` / `RemoveCollider`、`OnCollisionEnter/Stay/Exit` 仮想関数、`GetWorldPosition()` |
| | `GameObjectManager::RegisterAllColliders` | active かつ未破棄かつ enabled のコライダーを登録 |
| 形状数学 | `Engine/Src/Utility/Collision/CollisionUtils.*` | `Sphere/Capsule/Ray/Plane/LineSegment` 構造体＋距離・最近接点・交差・レイ交差＋**汎用数学（Clamp/Lerp/Slerp/ExpandAABB…）** |
| 2D タイル | `Engine/Src/TileMap/TileCollider.*` | AABB×タイルマップの**押し出し解決**。軸独立、接地/天井/左右壁フラグを返す。**上記系統と完全に無関係** |
| 別実装 | `Editor/ImGui/ObjectSelector.cpp` | `RayIntersectsSphere/AABB/Triangle` を**独自に再実装**（ピッキング用） |
| 幾何 | `Math/BoundingBox.h`, `Math/Frustum.h` | `BoundingBox::TransformBy`（正しい OBB→AABB 変換）、`CoreEngine::Plane`（`CollisionUtils::Plane` と**二重定義**） |

---

## 2. 良い点（維持すべき設計）

1. **レイヤーマトリクスによるフィルタリング** — `SetCollisionEnabled` が両方向へ書き込むため非対称設定バグが構造的に起きない。判定コストの削減とゲームロジックの分離という 2 目的を 1 機構で満たしており、方向性は正しい。
2. **`ISceneFeature` 化されている** — `SceneFeature` コンポジションの流れ（`scene-feature-composition`）に乗っており、`BaseScene` 本体にコリジョンのコードが埋まっていない。判定タイミングが `PostObjectUpdate` として**列挙値で明示**されているのも良い（暗黙の更新順序に依存していない）。
3. **Enter/Stay/Exit の状態機械が 1 箇所に集中している** — 前フレーム集合との差分という素直な実装で、各コライダーが自前で状態を持っていない。拡張時もここだけ見ればよい。
4. **`MakePair` によるペア正規化** — ポインタ順で正規化してから set に入れるため、(a,b)/(b,a) の重複エントリが原理的に生じない。
5. **形状数学が純関数として独立している** — `CollisionUtils` はグローバル状態を持たず、単体テストを書ける素地がある（実際のテストはまだ無い）。カプセル×カプセルの線分間最短距離も縮退ケース分岐込みで正しく書かれている。
6. **所有権が明快** — `GameObject` が `unique_ptr<Collider>` で所有し、`CollisionManager` は生ポインタで借用するだけ。二重解放の余地がない。
7. **`GameObject` の簡易 API** — `AddSphereCollider(1.0f, Layer::Player)` の 1 行で使える手軽さは学習用エンジンとして正しい設計判断。
8. **`TileCollider` の軸独立解決** — 「X 移動→X 解決→Y 移動→Y 解決」でコーナー誤判定を避け、`isOnGround/hitCeiling/hitWallLeft/hitWallRight` まで返す。2D プラットフォーマ用の解決器としては完成度が高く、意図がコメントで明示されている。
9. **ドキュメントが `Docs/Engine/Collision/Collision.md` に存在する** — 使い方が追える（ただし後述の乖離あり）。

---

## 3. 問題点

### A. 実バグ・不正動作（優先度：最高）

#### A-1. `previousCollisions_` が生ポインタを跨フレーム保持 → Exit 欠落 + ABA 問題
`CollisionManager.h:45`。毎フレーム `ClearColliders()` でリストのみ消し、履歴は保持する設計（`CollisionFeature.cpp:15`）。ここに 2 つの不具合がある。

- **Exit が飛ばない**: 重なった状態で敵が `Destroy()` されると、次フレームその敵はリストに載らない → ペアが `currentCollisions` に入らない → しかし `previousCollisions_` にはキーが残るだけで、`OnCollisionExit` を呼ぶ経路が存在しない（Exit は「両方が生きていて離れた」場合のみ発火する）。**生き残った側のプレイヤーが Exit を受け取れない**。「接触中フラグ」を Enter/Exit で管理しているゲームロジックは壊れる。
- **アドレス再利用（ABA）**: `GameObject` の破棄は `CleanupDestroyed()` で 1 フレーム遅延して行われる（`GameObjectManager.cpp:78`）。弾のようにプールで生成/破棄を繰り返すと、`make_unique<SphereCollider>` が**同じアドレスを再取得**する。すると古いペアキーと一致し、新しい弾の初回接触が **Enter ではなく Stay になる**。ダメージ判定を Enter で書いていれば無音で抜ける。

#### A-2. コールバック中のコライダー着脱が即時 delete → use-after-free
`GameObject::RemoveCollider()` / `AddSphereCollider()` は `unique_ptr` を即時 reset/代入する（`GameObject.cpp:90,93`）。`CheckAllCollisions()` のループ内で `a->OnCollisionEnter(b)` → ユーザーコードが `RemoveCollider()` を呼ぶと、`colliders_` に残った生ポインタと直後の `b->OnCollisionEnter(a)` が解放済みメモリを触る。`GameObject::Destroy()` は遅延なのに**コライダー着脱だけ即時**という非対称が原因。アイテム取得時に `RemoveCollider()` を呼ぶのは自然な書き方なので踏みやすい。

#### A-3. `GameObject::GetWorldPosition()` の既定実装が原点を返す
`GameObject.cpp:83` が `return {}`。`Collider::GetPosition()` はこれを唯一の位置ソースにしている（`Collider.cpp:11`）。オーバーライドしているのは `ModelGameObject` と `SkyBoxObject` だけなので、**それ以外の派生（2D/スプライト/独自オブジェクト）にコライダーを付けると全員が原点で重なる**。コンパイルも通り、警告も出ず、常時衝突という形で現れる最悪の失敗モード。

#### A-4. `Default` × `Default` が無効になっている
`CollisionConfig.cpp:16-20` のループが `i != Default` をスキップするため、`matrix_[Default][Default]` だけ `false` のまま。レイヤー未指定（既定引数 `CollisionLayer::Default`）で 2 つのオブジェクトにコライダーを付けると**何も起きない**。ドキュメントの「Default 層のみ全てのレイヤーと衝突」という記述とも実挙動が食い違う。初見で必ず踏む罠。

#### A-5. スケール・回転をまったく反映しない
`AABBCollider::GetMin/GetMax` は `size_` を素で使い、`SphereCollider` も `radius_` を素で使う。`WorldTransform` の scale を掛けていないので、モデルを 2 倍に拡大しても判定は元サイズ。回転すれば AABB は実形状からずれる（OBB が無い）。**`BoundingBox::TransformBy`（`Math/BoundingBox.h:55`）と `ModelGameObject::GetWorldBoundingBox()` という正しい実装が既にあるのに使っていない**のが特に問題。

#### A-6. `RayIntersectAABB` のゼロ除算（潜在的な脆さ。現ビルドでは誤答は未再現）
`CollisionUtils.cpp:271` で `1.0f / ray.direction.x` を無条件に計算している。軸平行なレイ（成分 0）で `inf` になり、原点がちょうどスラブ境界上だと `0 * inf` が NaN になり得るため、`tmin/tmax` の比較（`tmin > tmax`）がすり抜けて誤った交点を返す構造になっている。

**ただし Phase 0 の実測（U12）では、Development ビルドで正しい入口点が返った**（`0 * inf` が NaN にならない浮動小数点設定と思われる）。したがって現時点では「誤答が出るバグ」ではなく **ビルド設定に依存する脆さ**として扱う。修正はゼロ成分の明示分岐で行い、U12 は「今の正しい答え」を固定する回帰テストとして残す。

#### A-7. `Slerp` の縮退で単位長が壊れる
`CollisionUtils.cpp:336`。反平行（`dot == -1`）のとき `Normalize(end - dot*start)` がゼロ長ベクトルの正規化になる。`Vector3::Normalize` がゼロ長をガードして `{0,0,0}` を返すため NaN にはならないが、**結果が長さ 0 のベクトルになる**（Phase 0 の U13 で実測: 長さ 0.000）。回転面が決まらないケースを別途扱う必要がある。

#### A-8. 既定値の規約が不統一
`CollisionUtils::Sphere()` は `radius = 1.0f`、`Capsule()` は長さ 1・半径 0.5 という「それっぽい値」を入れるのに対し、`BoundingBox()` は `FLT_MAX/-FLT_MAX` の**無効値**から始まる。前者は初期化忘れが見た目上動いてしまい、後者は `IsValid()` で検出できる。混在は事故の温床。

### B. 設計上の問題（優先度：高〜中）

#### B-1. 二重ディスパッチが各派生クラスの `if/else` に散在
`SphereCollider::CheckCollision` と `AABBCollider::CheckCollision` の両方に **Sphere×AABB のロジックが重複**している（`SphereCollider.cpp:23`, `AABBCollider.cpp:24`）。形状 N 個で N² 分岐、しかも「片方だけ直す」事故が起きる構造。さらに `static_cast<const SphereCollider&>` は `type_` の手動代入（各コンストラクタ）に依存しており、書き忘れれば静かに UB。カプセルや OBB を足すと分岐が爆発する。

#### B-2. 基底 `Collider` にダミー仮想関数がある
`virtual void SetSize(const Vector3&) { (void)size; }` / `virtual void SetRadius(float)` （`Collider.h:38,40`）。球に `SetSize` が、AABB に `SetRadius` が生えている。呼んでも何も起きない・エラーも出ないインタフェースで、形状を追加するたびに基底が太る（LSP 違反）。

#### B-3. 1 GameObject = 1 コライダー固定
`std::unique_ptr<Collider> collider_` 単体（`GameObject.h:291`）。複合形状、キャラの部位別判定、`CollisionLayer::BossAttack` のような**本体とは別レイヤーの攻撃判定**が原理的に持てない。レイヤー enum に `Boss` と `BossAttack` が両方あるのに、ボスは片方しか持てないという矛盾。

#### B-4. 判定結果が `bool` のみ — 接触情報がない
法線・貫通深度・接触点を返さないため、**押し出しや物理応答が原理的に書けない**。この制約の帰結として 2D タイルの押し出しだけが `TileCollider` という完全に別系統で実装されている。系統分断は設計の欠落が表に出たもの。

#### B-5. ブロードフェーズが存在しない（完全 N²）＋毎フレームのアロケーション
`CheckAllCollisions()` は素の二重ループ。100 体で 4,950 ペア、1,000 体で約 50 万ペア。加えて毎フレーム `unordered_set currentCollisions` を新規確保し、`colliders_` も clear→再構築（reserve なし）。`Hi-Z オクルージョンカリング` や `ModelVisibility` 側に空間分割の資産があるのに共有されていない。現状はコライダー未使用なので顕在化していないが、`object-picking-performance` で全三角形総当たりがフリーズを起こした前例と同じ構図。

#### B-6. ハッシュ関数が弱い
`reinterpret_cast<size_t>(a) ^ reinterpret_cast<size_t>(b)`（`CollisionManager.h:36`）。近接アドレス同士の XOR は上位ビットが消えて下位に集中し、`unordered_set` のバケットが偏る。対称性が必要なのは正しいが、`hash_combine` の対称版（例: 両者を mix してから加算）にすべき。

#### B-7. クエリ API が存在しない
`CollisionManager` は「登録された全ペアを総当たり」以外の窓口を持たない。レイキャスト・オーバーラップ問い合わせ・最近接ヒットが無いため、**`ObjectSelector` が `RayIntersectsSphere/AABB/Triangle` を独自に再実装**している（`ObjectSelector.cpp:289,334,392`）。`CollisionUtils` に Ray×Sphere / Ray×AABB は既にあるのに使われていない完全な重複。Ray×Triangle は逆に `CollisionUtils` 側に無い。

#### B-8. 責務の混線とディレクトリの分散
- `CollisionUtils` が `Clamp/Lerp/Slerp/ExpandAABB/CreateAABBFromPoints` という**汎用数学**まで抱えている（`Math` 配下にあるべき）。`Lerp` は `Math/Easing/EasingUtil` とも重複。
- `Plane` が `Math/Frustum.h`（`normal, d`、`Dot+d`）と `CollisionUtils`（`normal, distance`、`Dot-distance`）で**符号規約まで違う二重定義**。混同すれば符号反転バグになる。
- `Capsule` 構造体と交差関数はあるが `CapsuleCollider` が無い（片肺）。
- 「当たり判定」が `Engine/Src/Collider/`・`Engine/Src/Utility/Collision/`・`Engine/Src/TileMap/` の 3 箇所に散在。

#### B-9. デバッグ可視化がほぼ無い
`AABBCollider::DrawDebug` のみで、①`_DEBUG` 限定（Development ビルドで使えない）、②**呼び出し元がゼロの死コード**、③Sphere 版が無い、④`LineRendererPipeline`/`LineManager`/`Camera` を直接 include してレンダラ依存がコライダー側に漏れている。`window-resize-handling` や `editor-settings-autosave` で確立した「エンジン常駐 UI」の方針にも乗っていない。

#### B-10. CVar / エディタ統合が未対応
プロジェクトには CVar 基盤（`cvar-system`）があり、水面は 35 パラメータを移行済み。当たり判定には `r.Collision.*` が 1 つも無く、可視化トグルもコリジョンマトリクス編集 UI も無い。Inspector にコライダーのセクションが無いので、シーン上でサイズ調整もできない。

#### B-11. 静的/動的の区別が無い
`Environment`（壁）同士のような**永久に動かないペア**も毎フレーム判定している。静的コライダーの再挿入省略という最も効くコスト削減が構造上できない。

#### B-12. トリガーとソリッドの区別が無い
「通知だけ」と「押し戻す」の区別が型にもフラグにも無い。B-4 と合わせて、3D 側は通知専用システムに固定されている。

#### B-13. 実質的に未使用でリグレッションを検知できない
`Application` 側で `AddSphereCollider`/`AddAABBCollider` を呼んでいる箇所は**ゼロ**だった。`SphereObject` は接触数に応じた色替えを実装しているが（`SphereObject.cpp:15-30`）、そこにコライダーを付けるシーンが存在しないため一度も動いていない。テストも無い。つまり A-1〜A-5 のバグはすべて「誰も使っていないから発覚していない」状態で、仕様はドキュメントだけが担保していた。**リファクタリング前に踏み台となる動作シーンを作らないと、直したかどうかを確認できない。**

→ Phase 0 で `CollisionTestScene` を追加して解消済み（§5 参照）。

#### B-14. コードスタイルの不統一
`Collider/*` は 3 スペースインデント・`namespace` 内非インデント、`CollisionManager.cpp` は 4 スペース・`namespace` 内インデント、`TileCollider.cpp` は罫線コメント。同一ディレクトリ内で 2 系統。

### C. 軽微

- `ClearColliders()` と `Clear()` の 2 API は「履歴を残すかどうか」を呼び出し側が意識しなければならず、A-1 の温床になっている。
- `GetAllColliders()` が生ポインタ列を外部へ公開。
- `CollisionFeature` に `Finalize` が無い（`Clear()` を呼ばない）。現状はスコープ破棄で足りるが、Feature 契約としての対称性が無い。
- `colliders_` に `reserve` が無い。
- ドキュメント `Docs/Engine/Collision/Collision.md` の記述と実装の乖離（Default 層の挙動、`SetCollisionEnabled` の第 3 引数、存在しないデバッグ描画の扱い）。

---

## 4. リファクタリング設計案

### 4.1 方針（7 本柱）

| # | 方針 | 解決する問題 |
|---|---|---|
| 1 | 形状を**値型**に切り出し、`Math/Geometry` へ集約 | B-1, B-8 |
| 2 | 交差判定を**フリー関数のディスパッチテーブル**に一元化（重複ゼロ） | B-1, B-2 |
| 3 | 判定結果に**接触情報（法線・深度・点）**を載せる | B-4, B-12 |
| 4 | コライダーは**複数所有 + Transform 由来のワールド形状** | A-3, A-5, B-3 |
| 5 | **安定ハンドル**でライフタイム問題を根絶 | A-1, A-2 |
| 6 | **ブロードフェーズを差し替え可能に**し、クエリ API を公開 | B-5, B-6, B-7, B-11 |
| 7 | **デバッグ描画・エディタ UI をエンジン常駐 + CVar** | B-9, B-10 |

**互換性方針**: `AddSphereCollider` / `AddAABBCollider` / `OnCollisionEnter/Stay/Exit` / `SetCollisionEnabled` は**シグネチャを維持**し、内部を新実装へ委譲する薄いシムとして残す。既存ドキュメントのサンプルコードが動き続けることを不変条件とする。

### 4.2 目標ディレクトリ構成

```
Engine/Src/Math/Geometry/          （新規・純粋幾何。レンダラ・GameObject 非依存）
    Shapes.h                       Sphere / AABB / OBB / Capsule / Ray / LineSegment（Plane は Frustum.h に一本化）
    Intersect.h/.cpp               Intersect(shape, shape, Contact*) の全実装 + ディスパッチ
    Distance.h/.cpp                距離・最近接点
    RayCast.h/.cpp                 Ray×Sphere/AABB/OBB/Capsule/Triangle（tMin/tMax 対応）

Engine/Src/Collision/              （旧 Collider/ を改名・統合）
    CollisionShape.h               形状 variant + ローカルオフセット
    Collider.h/.cpp                データ主体の薄いクラス（仮想関数なし）
    ColliderComponent.h/.cpp       GameObject が持つ複数コライダーの容器
    CollisionLayer.h               （現状維持 + LayerMask 64bit）
    CollisionConfig.h/.cpp         マトリクス + JSON 永続化
    CollisionWorld.h/.cpp          旧 CollisionManager。判定ループ + クエリ API
    BroadPhase/IBroadPhase.h
    BroadPhase/BruteForceBroadPhase.h/.cpp
    BroadPhase/UniformGridBroadPhase.h/.cpp
    Debug/ColliderDebugDraw.h/.cpp （描画は Feature 側から呼ぶ）

Engine/Src/Scene/Feature/
    CollisionFeature.h/.cpp        CollisionWorld の駆動（現状維持 + Finalize 追加）

Engine/Src/TileMap/TileCollider.*  2D 解決器として残す（内部の重なり計算のみ Geometry へ委譲）

Engine/Src/Utility/Collision/      → 削除（中身を Math/Geometry と Math へ分割移動）
```

### 4.3 コア API のかたち

```cpp
// Math/Geometry/Shapes.h ------------------------------------------------
namespace CoreEngine::Geometry {
    struct Sphere  { Vector3 center; float radius; };
    struct AABB    { Vector3 min, max; };            // Math/BoundingBox.h を昇格
    struct OBB     { Vector3 center; Vector3 axis[3]; Vector3 halfExtent; };
    struct Capsule { Vector3 start, end; float radius; };

    using Shape = std::variant<Sphere, AABB, OBB, Capsule>;
}

// Math/Geometry/Intersect.h ---------------------------------------------
struct Contact {
    Vector3 normal{};   ///< A から B へ押し出す方向（正規化）
    float   depth = 0;  ///< 貫通深度（0 以下なら非接触）
    Vector3 point{};    ///< 代表接触点
};

/// 形状ペアの交差判定（実装は 1 ペア 1 箇所のみ。対称ペアは引数を入れ替えて再利用）
bool Intersect(const Shape& a, const Shape& b, Contact* outContact = nullptr);
```

- `Sphere×AABB` の実装は **1 箇所だけ** 存在し、`AABB×Sphere` は法線を反転して転送する。B-1 の重複が構造的に消える。
- `outContact == nullptr` なら早期 return できる高速パスを残す（通知専用ケースのコスト維持）。

```cpp
// Collision/Collider.h --------------------------------------------------
class Collider {
public:
    Geometry::Shape  localShape;              ///< ローカル空間の形状
    Vector3          localOffset{};           ///< 中心オフセット
    CollisionLayer   layer = CollisionLayer::Default;
    bool             isTrigger = true;        ///< false = 押し出し対象
    bool             isStatic  = false;       ///< true = 動かない（BroadPhase が再挿入を省略）
    bool             enabled   = true;

    /// Transform を適用したワールド形状（scale/rotation 反映）
    Geometry::Shape GetWorldShape(const Matrix4x4& world) const;
    GameObject* GetOwner() const;
};
```

- 仮想関数を持たない（`type_` 手動代入と `static_cast` ダウンキャストが消滅 → B-1 後半、B-2 解決）。
- `GetWorldShape` で **scale/rotation を反映**（AABB は `BoundingBox::TransformBy` を流用、Sphere は最大軸スケール、Box は OBB へ昇格）→ A-5 解決。
- 位置は `owner->GetWorldPosition()` ではなく **ワールド行列**から取る → A-3 の無音バグを構造的に排除。

```cpp
// Collision/CollisionWorld.h -------------------------------------------
using ColliderHandle = uint64_t;   ///< index(32bit) | generation(32bit)

class CollisionWorld {
public:
    ColliderHandle Add(Collider* collider);
    void Remove(ColliderHandle handle);       ///< 判定中は遅延削除キューへ
    void Step();                              ///< BroadPhase → NarrowPhase → イベント発火

    // クエリ API（B-7）
    struct RayHit { ColliderHandle handle; GameObject* owner; float distance; Vector3 point, normal; };
    bool Raycast(const Geometry::Ray& ray, float maxDistance, LayerMask mask, RayHit* out) const;
    void RaycastAll(..., std::vector<RayHit>& out) const;
    void OverlapSphere(const Geometry::Sphere&, LayerMask, std::vector<ColliderHandle>& out) const;
    void OverlapBox(const Geometry::OBB&, LayerMask, std::vector<ColliderHandle>& out) const;
};
```

- ペア履歴のキーを `ColliderHandle` にすることで **ABA 問題が消える**（generation が違えば別物）→ A-1 解決。
- `Remove` が判定中なら遅延キューに積み、`Step()` の末尾で処理 → A-2 解決。同時に「破棄されたコライダーと接触中だったペアには `OnCollisionExit` を発火してから消す」規則を入れる（A-1 前半）。

### 4.4 フェーズ計画

> 各フェーズは独立にビルド・動作確認可能な単位とする。フェーズ跨ぎの大改造を 1 コミットにまとめない。

#### Phase 0: 安全網の構築（最優先・これ無しに先へ進まない）
1. **動作確認シーンを作る**: `CollisionTestScene`（Sphere×Sphere / Sphere×AABB / レイヤーフィルタ / Enter-Stay-Exit のログ出力 / オブジェクト破棄中の接触）。B-13 のとおり現状は誰も使っていないため、これが唯一の回帰検出手段になる。
2. **純関数のアサートテスト**: `Geometry` 移行前に `CollisionUtils` の交差関数へ既知入出力のアサートを追加（軸平行レイ、縮退カプセル、境界接触）。
3. **不変条件リストの確定**（下記 4.5）をこの計画書に固定。
4. ドキュメント `Collision.md` を**現状の実挙動**に合わせて修正（Default×Default が無効である旨、`DrawDebug` が未使用である旨を明記）。「直す前に、今どうなっているかを正しく書く」。

**完了条件**: `CollisionTestScene` が Enter/Stay/Exit のログを出し、A-1/A-4 のバグが**ログで再現する**こと。

#### Phase 1: 実バグ修正（設計は変えない）
| 項目 | 修正内容 |
|---|---|
| A-4 | `CollisionConfig` の初期化ループを修正し `Default×Default` を有効化。ドキュメント同期 |
| A-3 | `GameObject::GetWorldPosition()` を純粋仮想化（または `WorldTransform` を `GameObject` へ持ち上げ）。既定で原点を返す実装を消す |
| A-1 | 破棄検出パスを追加: `CollisionWorld` へ「このコライダーは消えた」を通知し、接触中ペアへ `OnCollisionExit` を発火してから履歴を削除。Phase 5 でハンドル化するまでの暫定策 |
| A-2 | `AddXxxCollider`/`RemoveCollider` を判定ループ中は遅延化。加えて判定中の着脱を検出する `assert` |
| A-6 | `RayIntersectAABB` を成分ゼロ対応の安全版に（`inv = 1/d` の代わりに符号付き無限大 + NaN 排除、または成分ごとの分岐） |
| A-7 | `Slerp` の縮退時は線形補間へフォールバック |
| A-8 | `Sphere`/`Capsule` の既定値方針を `BoundingBox` に合わせるか、逆にコンストラクタ必須化して統一 |
| C | `CollisionFeature::Finalize` を追加（`Clear()` 呼び出し） |

**完了条件**: Phase 0 のシーンで再現していたバグが消え、他は挙動不変。

#### Phase 2: 幾何の一元化（`Math/Geometry` の新設）
1. `Math/Geometry/Shapes.h` に形状値型を新設。`Math/BoundingBox.h` の `BoundingBox` を `Geometry::AABB` として位置づけ（**別名 typedef で既存コードを壊さない**）。
2. `Intersect.h/.cpp` に交差実装を集約。**Sphere×AABB を 1 実装に統合**し、対称ペアは転送。`Contact` 出力を実装。
3. `RayCast.h/.cpp` に Ray 系を集約。`ObjectSelector` の `RayIntersectsTriangle` を**ここへ移設**し、`tMin/tMax` 付き API を用意。
4. `Distance.h/.cpp` に距離・最近接点を移設。
5. `Clamp/Lerp/Slerp/ExpandAABB/CreateAABBFromPoints` を `Math` 側へ移動（`Lerp` は `EasingUtil` との重複を解消）。
6. **`Plane` の二重定義を解消**: `Math/Frustum.h` 側（`normal, d`）に一本化し、`CollisionUtils::Plane` の利用箇所を移行。符号規約をヘッダコメントで明記。
7. `Engine/Src/Utility/Collision/` を削除し、旧ヘッダは `#include` 転送のみの deprecated シムを一時的に残す。

**完了条件**: `ObjectSelector` が独自レイ判定を持たず `Geometry::RayCast` を呼ぶ。ピッキング性能が退行していない（`object-picking-performance` の AABB 事前棄却＋ローカル空間レイという不変条件を維持）。

#### Phase 3: コライダーのコンポーネント化
1. `Collider` を仮想関数なしのデータクラスへ。`SphereCollider`/`AABBCollider` は削除（形状は `Geometry::Shape`）。
2. `ColliderComponent`（複数コライダー保持、ローカルオフセット、`isTrigger`/`isStatic`）を新設し、`GameObject` に持たせる。
3. `GetWorldShape()` で **scale/rotation を反映**（A-5 解決）。
4. `AddSphereCollider`/`AddAABBCollider` は「1 本目を追加して参照を返す」互換シムに（既存ドキュメントのコードが動くこと）。
5. `Collider& AddSphereCollider(...)` の戻り値型を維持するため、シムは `ColliderComponent` 内の要素への参照を返す（**要素追加で参照が無効化されないコンテナ**を選ぶ = `deque` か固定容量 + ハンドル）。

**完了条件**: 1 オブジェクトに本体判定と攻撃判定を別レイヤーで 2 本付けられる。既存サンプルコードが無修正で動く。

#### Phase 4: 接触情報と押し出し・TileCollider の統合
1. `Contact` を Enter/Stay コールバックへ渡せる経路を追加（`OnCollisionEnter(GameObject*)` は維持しつつ、`OnCollisionEnter(const CollisionInfo&)` を追加してデフォルト実装で旧版へ転送）。
2. `isTrigger == false` のペアに対する押し出し解決（`CollisionResolver`）を追加。まずは静的コライダー相手のみの単純解決から。
3. `TileCollider` は 2D 解決器として残すが、AABB 重なり判定と押し出し量の算出を `Geometry::Intersect` ベースへ寄せ、**押し出し量の計算式が 2 箇所に散らないようにする**。
4. `TileCollisionResult` の接地/壁フラグは維持（プラットフォーマの需要としてこの形が正しい）。

**完了条件**: 3D で壁にぶつかって止まるデモが動く。2D タイル挙動が退行していない。

#### Phase 5: ライフタイムとブロードフェーズ
1. `ColliderHandle`（index + generation）を導入し、ペア履歴のキーをハンドル化（**A-1 の ABA を根絶**）。
2. 遅延削除キューを正式化。`Step()` 末尾で処理し、破棄ペアには Exit を発火。
3. `IBroadPhase` を切り出し、`BruteForceBroadPhase`（既定）と `UniformGridBroadPhase` を実装。`isStatic` のコライダーはダーティ時のみ再挿入。
4. `LayerMask`（64bit）を導入し、ブロードフェーズ前段でビット AND による早期棄却。
5. ペア集合を毎フレーム再確保せず **2 枚を swap して再利用**。ハッシュを対称 mix（`splitmix64` 併用）に差し替え（B-6）。
6. `colliders_` に `reserve`。

**完了条件**: 1,000 コライダーでの判定時間を計測し、BruteForce 比で有意に改善。ハンドル化により弾プールで Enter が正しく発火する（Phase 0 のシーンで検証）。

#### Phase 6: デバッグ可視化とエディタ統合
1. `ColliderDebugDraw`（`Collision/Debug/`）を新設。Sphere/AABB/OBB/Capsule のワイヤ描画を `LineManager` のプリミティブとして実装。**`Collider` 自体はレンダラに依存しない**（B-9 の依存漏れ解消）。
2. `_DEBUG` 限定をやめ `USE_IMGUI`/Development でも有効に。死コード `AABBCollider::DrawDebug` は削除。
3. CVar 追加（`cvar-system` のファイルスコープ必須という制約に従う）:
   - `r.Collision.DebugDraw`（0/1）
   - `r.Collision.DebugDrawMode`（All / CollidingOnly / SelectedOnly）
   - `r.Collision.DebugColor*`
   - `r.Collision.BroadPhase`（BruteForce / UniformGrid）
   - `r.Collision.GridCellSize`
4. Inspector に **Collider セクション**（形状・サイズ・オフセット・レイヤー・isTrigger の編集）。保存は `editor-settings-autosave` / シーン保存の既存経路に乗せる。
5. **コリジョンマトリクス編集ウィンドウ**（Unity 風のチェックボックス格子）を Engine Settings 配下に追加し、`CollisionConfig` を JSON 永続化。
6. `Docs/Engine/Collision/Collision.md` を新 API に合わせて全面更新。移行ガイド（旧 API → 新 API）を追記。

**完了条件**: シーン上でコライダーがワイヤ表示され、Inspector からサイズを変えて保存・復元できる。

### 4.5 守るべき不変条件（各フェーズで破ってはいけない）

1. `AddSphereCollider(radius, layer)` / `AddAABBCollider(size, layer)` は**シグネチャと戻り値型を変えない**（`Collider&` を返し、返した参照は同一フレーム内で有効）。
2. `OnCollisionEnter/Stay/Exit(GameObject*)` の仮想関数は**残す**。新 API は追加のみ。
3. Enter は接触開始フレームに**ちょうど 1 回**、Exit は離脱フレームに**ちょうど 1 回**、両方のオブジェクトに対して発火する。
4. レイヤーマトリクスは常に対称（片方向のみ有効という状態を作らない）。
5. コリジョン判定は `SceneUpdatePhase::PostObjectUpdate`（GameObject 更新後・`OnLateUpdate` 前）で実行する。
6. コライダーの所有権は `GameObject` 側にあり、`CollisionWorld` は借用のみ（二重解放を作らない）。
7. コールバック内での GameObject 生成・破棄・コライダー着脱は**安全**でなければならない（遅延処理で担保）。
8. `ObjectSelector` のピッキングは AABB 事前棄却 → ローカル空間レイ → 三角形の順を維持する（性能退行禁止）。
9. `TileCollider` の軸独立解決順（X→Y）と接地/天井/壁フラグの意味を変えない。
10. `Geometry` 層は `GameObject`・レンダラ・ImGui に依存しない（純粋幾何を保つ）。

### 4.6 リスクと注意点

- **ODR 事故に注意**: `Collider` や形状構造体のサイズが変わる変更を増分ビルド中に中断すると、ヒープ破損（`c0000374`）が起きる前例がある（`build-system-gotchas`）。**Phase 2/3/5 の構造体変更を含むコミットではクリーンビルドを行う**。
- **vcxproj への手動登録が必要**: `SyncFilters` はファイルを追加しない。新規ファイル（`Math/Geometry/*`, `Collision/*`）は `CoreEngine.vcxproj` と `.filters` の両方へ手で登録する。
- **`Rebuild` は禁止**（DirectXTex の `.inc` 消失の罠）。クリーンが必要な場合は中間ディレクトリの選択削除で対応。
- **旧ヘッダの deprecated シム期間**: `Utility/Collision/CollisionUtils.h` を即削除せず 1 フェーズ分は転送ヘッダとして残し、include 漏れによる大量コンパイルエラーを避ける。
- **Phase 0 を省略しない**: 現状は実質未使用のため、動作シーンなしで改造すると「壊れたことに気づけない」。これが本リファクタリング最大のリスク。

### 4.7 推奨する着手順と規模感

| Phase | 内容 | 規模 | 依存 | 状態 |
|---|---|---|---|---|
| 0 | 安全網（テストシーン + 現状ドキュメント修正） | 小 | — | **完了**（§5） |
| 1 | 実バグ修正（A-5 を Phase 3 から前倒し） | 小〜中 | 0 | **完了**（§6） |
| 2 | `Math/Geometry` 一元化・Plane 統合・ObjectSelector 委譲 | 中 | 1 | **完了**（§7） |
| 3 | コライダーのコンポーネント化・scale/rotation 反映 | 中 | 2 | **完了**（§8） |
| 4 | 接触情報・押し出し・TileCollider 統合 | 中 | 3 | **完了**（§9） |
| 5 | ID 化・ブロードフェーズ・LayerMask・クエリ API・フォルダ改名 | 中 | 3 | **完了**（§10） |
| 6 | デバッグ描画・CVar・Inspector・マトリクス UI | 中 | 3（4/5 と並行可） | **完了**（§11） |

Phase 0 → 1 だけでも「既定レイヤーで当たらない」「Exit が飛ばない」「拡大しても判定が変わらない」という**目に見える不具合が消える**ため、まずここを切り出して着手することを推奨する。

---

## 5. Phase 0 実施結果（2026-08-06）

### 5.1 追加したもの

| ファイル | 役割 |
|---|---|
| `Application/Src/Scenes/CollisionTestScene/CollisionTestScene.h/.cpp` | 回帰テストシーン本体（T1〜T10 をフレームスケジュールで自動実行） |
| `.../CollisionProbeObject.h/.cpp` | 衝突イベントを数える可視プローブ（球 / 箱 / 見た目なし） |
| `.../CollisionUtilsSelfTest.h/.cpp` | `CollisionUtils` 純関数の自己テスト（U1〜U13） |
| `.../CollisionTestReport.h/.cpp` | 結果集約シングルトン + App Editor パネル + ログ出力 |

`MyGame.cpp` に `CollisionTestScene` を登録済み（初期シーンは変更していない。Scene Manager タブから切り替える）。
`CoreEngine.vcxproj` / `.filters` へも登録済み。

**シーン設計上の要点**

- プローブ位置は毎フレーム `OnUpdate()` で再指定する。シーン JSON の復元やギズモ操作でテスト条件がずれないようにするため（プローブは `SetSerializeEnabled(false)`）。
- 結果の評価は `OnLateUpdate()`（= `PostObjectUpdate` の後）で行う。`OnUpdate()` の時点では今フレームの判定結果がまだ出ていない。
- App Editor に登録するドロワーは**何もキャプチャしない**（シングルトンを読むだけ）。`GameDebugUI` に App Editor の登録解除 API が無いため、シーンの `this` をキャプチャすると破棄後にダングリングする。
- T10（A-2 の use-after-free 再現）はクラッシュし得るため既定 OFF。パネルのチェックボックスで明示的にオプトインする。

### 5.2 実測結果（Development ビルド, 189 フレーム）

`Cache/logs/Game/Physics/Physics_*.log` に出力。**PASS 16 / FAIL 6 / 進行中 1**。

| ID | 内容 | 結果 | 実測値 |
|---|---|---|---|
| U1〜U11 | 形状交差・レイ交差のベースライン | **PASS** | 期待どおり |
| U12 | Ray×AABB 軸平行レイ（A-6） | **PASS** | 交点 `(-1, 2, 0)` = 正しい入口点。誤答は再現せず（→ A-6 を「潜在的な脆さ」へ格下げ） |
| U13 | Slerp 反平行（A-7） | **FAIL** | 結果 `(0,0,-0)` 長さ `0.000`（NaN ではなく非単位ベクトル） |
| T1 | 球×球 通過 | **PASS** | Enter=1 Stay=40 Exit=1 |
| T2 | 球→箱（球側の `CheckCollision`） | **PASS** | Enter=1 Stay=40 Exit=1 |
| T3 | 箱→球（箱側の `CheckCollision`） | **PASS** | Enter=1 Stay=40 Exit=1（T2 と一致＝重複実装が現状は同値） |
| T4 | レイヤーフィルタ（Player×Item 無効） | **PASS** | Enter=0 Stay=0 Exit=0 |
| T5 | Default×Default（A-4） | **FAIL** | Enter=0 — 既定レイヤー同士は当たらない |
| T6 | 接触中の相手を破棄（A-1 前半） | **FAIL** | Enter=1 Exit=**0** — 生存側に Exit が来ない |
| T7 | 生成/破棄の繰り返し（A-1 後半 / ABA） | **FAIL**（誤検出） | 「6 回中 5 回」と出たが、原因は**テストハーネスのバグ**（最終サイクルの検証が丸ごとスキップされていた）。ログ上は 6 回とも Enter が発火しており、**ABA は再現していない**（→ §5.3-4） |
| T8 | scale=2 の反映（A-5） | **FAIL** | Enter=0 — 見た目は接触しているのに無反応 |
| T9 | `GetWorldPosition()` 未実装（A-3） | **FAIL** | Enter=1 — z=+100 と z=−100 に置いたのに原点で衝突 |
| T10 | コールバック中の `RemoveCollider()`（A-2） | 進行中 | 既定 OFF（オプトイン式） |

**A-1 前半（破棄時の Exit 欠落）が確定的に再現した**（T6）ことが最大の収穫。一方 ABA（後半）は
再現しなかった（下記 §5.3-4）。

### 5.3 レビュー内容の訂正

Phase 0 の実測により、§3 の記述を 4 点修正した。

1. **A-6**: 「NaN で誤判定」→ 現ビルドでは正しい入口点が返る。ゼロ除算という構造上の脆さは残るため修正対象には残し、U12 は現在の正しい答えを固定する回帰テストとして扱う。
2. **A-7**: 「NaN になる」→ `Normalize` のゼロ長ガードにより NaN にはならず、**長さ 0 の非単位ベクトル**になる。
3. **B-13**: 「`SphereObject` は空のコールバックを override しているだけ」→ 接触数に応じた色替えを実装している。ただしコライダーを付けるシーンが無いため一度も動いていない、という結論は変わらない。
4. **A-1 の ABA（後半）は再現しなかった**。当初 T7 の「6 回中 5 回」を ABA の実機再現と報告したが、これは**テストハーネス側のバグ**だった（スポーンと検証を同じ `t7SpawnCount_ < kAbaCycles` ガードに入れたため、最終サイクルの検証が丸ごと走らなかった）。ログを見ると 6 回とも Enter が発火している。ハーネスは Phase 1 で修正し、判定基準を「Enter/Exit のカウントが スポーン回数と一致すること」に変更した。**ABA は構造上ありうるが実測では踏んでいない潜在バグ**、というのが正しい位置づけ。なお A-1 前半（破棄時に Exit が飛ばない）は T6 で確定的に再現しており、こちらは実バグ。

---

## 6. Phase 1 実施結果（2026-08-06）

### 6.1 修正内容

| バグ | 修正 | 変更ファイル |
|---|---|---|
| **A-4** Default×Default が無効 | 初期化ループの `i != Default` ガードを削除 | `CollisionConfig.cpp` |
| **A-3** `GetWorldPosition()` の既定が原点 | **純粋仮想化**してオーバーライドを強制。位置を持たないクラスは「`{}` を返す」と明示 | `GameObject.h/.cpp` + 派生 6 クラス（`SpriteObject` / `UIImage` / `ParticleSystem` / `GpuParticleSystem` / `LineDrawable` / `GridRenderer`） |
| **A-5** scale が未反映（Phase 3 から前倒し） | `GameObject::GetWorldScale()` を追加し、`SphereCollider::GetWorldRadius()` / `AABBCollider::GetWorldSize()` で乗算 | `GameObject.h`, `ModelGameObject.h/.cpp`, `Collider.h/.cpp`, `SphereCollider.*`, `AABBCollider.*` |
| **A-1** 破棄時に Exit が飛ばない | `CheckAllCollisions()` の先頭で「前フレーム接触していたのに今フレーム登録されていないペア」へ Exit を配る。履歴も同時に消えるので ABA の温床も断つ | `CollisionManager.cpp` |
| **A-2** コールバック中の着脱で use-after-free | `RemoveCollider()` / `AddXxxCollider()` を即時 delete から**遅延解放**へ。実体は `retiredColliders_` が保持し、`CleanupDestroyed()`（衝突判定より後）で解放 | `GameObject.h/.cpp`, `GameObjectManager.cpp` |
| **A-6** `RayIntersectAABB` のゼロ除算 | 軸ごとに平行判定して `1/0` を作らない実装へ | `CollisionUtils.cpp` |
| **A-7** Slerp が単位長を壊す | 直交成分が消える縮退を分岐。平行なら `start`、反平行なら直交軸を選んで 180 度回す | `CollisionUtils.cpp` |
| **A-8** 既定値の規約不統一 | `Sphere()` / `Capsule()` を縮退形状（半径 0）へ。設定漏れが「それっぽく動く」のを防ぐ | `CollisionUtils.h` |
| **C** Feature の対称性 | `CollisionFeature::Finalize()` を追加（`Clear()` 呼び出し） | `CollisionFeature.h/.cpp` |

**A-1 の参照安全性**（コメントにも明記）: 登録から外れる 4 経路（`Destroy()` / `SetActive(false)` / `SetEnabled(false)` / `RemoveCollider()`）すべてで実体はそのフレーム中は生きている。破棄されたオブジェクトは「外れた最初のフレーム」で履歴から消えるため、実体が解放される次フレーム以降に `previousCollisions_` が触ることはない。

### 6.2 実測結果 — **PASS 22 / FAIL 0 / 進行中 1**

Phase 0 で FAIL だった 6 件がすべて PASS へ。ベースライン（U1〜U12 / T1〜T4）は PASS のまま。

| ID | Phase 0 | Phase 1 | 実測値 |
|---|---|---|---|
| U13 Slerp 反平行 | FAIL | **PASS** | 結果 `(1,0,-0)` 長さ `1.000` |
| T5 Default×Default | FAIL | **PASS** | Enter=1 |
| T6 破棄時の Exit | FAIL | **PASS** | Enter=1 **Exit=1** |
| T7 生成/破棄の繰り返し | FAIL（誤検出） | **PASS** | Enter=6 Exit=6 / スポーン 6 回 |
| T8 scale=2 の反映 | FAIL | **PASS** | Enter=1 |
| T9 非 ModelGameObject 派生 | FAIL | **PASS** | Enter=0（A-3 はコンパイル時強制へ） |
| T10 コールバック中の RemoveCollider | 未実施 | **クラッシュせず** | オプトインを有効にした検証ビルドで ExitCode=0 |

T9 は役割が変わった。A-3 が純粋仮想化で**コンパイルエラー**になったため実行時には再現できず、
「ModelGameObject 以外の派生でも、実装すれば位置が判定に効く」ことの確認に置き換えた。

### 6.3 検証時のハマりどころ

- **クラスのサイズを変えたら必ずクリーンビルド**。`CollisionTestScene` からメンバを 2 つ削っただけの
  増分ビルドで、起動 6〜9 秒後にアクセス違反（`0xC0000005`）が出た。中間ファイル
  （`generated/CoreEngine/obj/Development`）を消して再ビルドしたら消滅。`build-system-gotchas` の
  ODR 事故と同じ症状。**`Rebuild` は使わない**（DirectXTex の `.inc` が消える）。
- **プロセスの生存確認だけではクラッシュを検出できない**。`CloseMainWindow()` の後に
  プロセスが居なければ「正常終了」に見えてしまう。`ExitCode` を必ず見ること
  （`0` = 正常 / `-1073741819` = アクセス違反）。
- **クラッシュするとログが 0 バイトになる**。spdlog は非同期でシャットダウン時にフラッシュするため、
  ログが空＝途中で落ちた、という判別に使える。

### 6.4 Phase 2 への申し送り

- `GetWorldScale()` は Phase 3 の `GetWorldShape(matrix)` に置き換わる暫定 API。行ベクトル規約の
  ワールド行列から基底ベクトル長で求めているので、親階層のスケールも含む。
- 非等倍スケールの球は最大軸スケールを採用している（安全側）。OBB 対応時に見直す。
- A-1 の修正は「登録の有無」を毎フレームの `unordered_set` で判定している。Phase 5 の
  `ColliderHandle` 化でこの set は不要になる。

---

## 7. Phase 2 実施結果（2026-08-06）

### 7.1 新しい構成

`Engine/Src/Utility/Collision/`（CollisionUtils）を廃止し、`Engine/Src/Math/Geometry/` へ再編した。

| ファイル | 中身 |
|---|---|
| `Math/Geometry/Shapes.h` | `AABB`（= `BoundingBox` の別名）/ `Sphere` / `Capsule` / `LineSegment` / `Ray` / **`Plane`（唯一の定義）** |
| `Math/Geometry/Distance.h/.cpp` | 距離・最近接点・**2 線分間の最近接点ペア** |
| `Math/Geometry/Intersect.h/.cpp` | 形状同士の交差判定 + **`Contact`（法線・貫通深度・接触点）** |
| `Math/Geometry/RayCast.h/.cpp` | `Raycast(Ray, Sphere/AABB/Plane)` と `RaycastTriangle`。**`tMin`/`tMax` 対応**、`RayHit` で交点・法線・距離を返す |

`Math` へ移した汎用ユーティリティ:

| 移動元 | 移動先 |
|---|---|
| `CollisionUtils::Clamp` / `Lerp`（float / Vector3） | `MathCore::Clamp` / `MathCore::Lerp` |
| `CollisionUtils::Slerp`（Vector3） | `MathCore::Vector::Slerp` |
| `CollisionUtils::ExpandAABB` / `ExpandAABBWithPoint` / `CreateAABBFromPoints` | `Math/BoundingBox.h` の自由関数 |

### 7.2 解消した重複

1. **球×AABB の二重実装（B-1）** — `SphereCollider::CheckCollision` と `AABBCollider::CheckCollision` に
   同じロジックが書かれていた。実装は `Intersect(const Sphere&, const AABB&, Contact*)` の 1 箇所だけになり、
   `Intersect(const AABB&, const Sphere&, ...)` は引数を入れ替えて転送し**法線だけ反転**する。
   回帰テスト **U14** が両方向の一致（depth 同値・法線反転）を見張る。
2. **`Plane` の二重定義（B-8）** — `Math/Frustum.h`（`normal, d`）と `CollisionUtils`（`normal, distance` で
   符号が逆）の 2 つを `Geometry::Plane` に一本化。`Frustum.h` は `using Plane = Geometry::Plane;` で
   従来の `CoreEngine::Plane` を維持している。規約は **`dot(normal, p) + d = 0` が平面上・`DistanceTo()` が正なら法線側**。
3. **`ObjectSelector` の独自レイ判定（B-7）** — `RayIntersectsSphere` / `RayIntersectsAABB` /
   `RayIntersectsTriangle` を削除し `Geometry::Raycast` / `RaycastTriangle` へ委譲。
   `ObjectSelector` に残したのは**ピッキング固有の手順だけ**（ローカル AABB で事前棄却 →
   ローカル空間のレイで全三角形 → 最近ヒットをワールド距離へ戻す）。
   `object-picking-performance` の性能不変条件は維持し、さらに三角形ループへ `tMax = closestT` を
   渡して「今の最近ヒットより奥」を早期棄却するようにした。
4. **`Clamp`/`Lerp`/`Slerp` の置き場所（B-8）** — AABB を触るだけの `ForceModule` や、
   ジョイント補間をするだけの `AnimationBlender` が当たり判定ヘッダを include していた状態を解消。

### 7.3 計画からの逸脱

計画では「旧ヘッダを 1 フェーズ分 deprecated シムとして残す」としていたが、
**`CollisionUtils` の利用箇所が 6 ファイルしかなかったため全て移行して即削除した**。
「大量のコンパイルエラーを避ける」という当初の懸念が当てはまらない規模だったため。

移行した利用箇所: `SphereCollider.cpp` / `AABBCollider.cpp` / `SceneDebugEditor.cpp`（Ray×Plane）/
`ForceModule.h/.cpp`（点×AABB）/ `AnimationBlender.cpp`（Lerp）/ 自己テスト。

### 7.4 実測結果 — **PASS 24 / FAIL 0 / 進行中 1**

Phase 1 の 22 件に加えて、接触情報の回帰テストを 2 件追加した。

| ID | 内容 | 実測値 |
|---|---|---|
| U14 | Sphere×AABB と AABB×Sphere の対称性 | `hit=true/true depth=0.553/0.553 normal=(-0.894,-0.447,0)/(0.894,0.447,0)` |
| U15 | Sphere×AABB の貫通深度と法線 | `depth=0.500 normal=(-1,0,0)` |

U1〜U13 / T1〜T10 は Phase 1 と同じ結果（PASS）。実装を丸ごと移設しても答えが変わっていない。

### 7.5 検証時に見つけた自分のミス

**U14 が空振りで PASS していた。** 最初に選んだ配置（球の中心 (2.5,0,0)・半径 1、箱 ±1）は
実際には接触しておらず、`hit=false/false` `depth=0/0` `normal=(0,0,0)/(0,0,0)` で
「対称である」という条件を自明に満たしていた。**ログの実測値を読んで初めて気づいた**
（PASS の数だけ見ていたら見逃していた）。角にまたがる配置へ変更し、
判定条件にも `hitForward` を加えて空振り PASS ができないようにした。

**教訓: 「同じであること」を検査するテストは、両方が空でも通る。**
非自明な値が出ていることをテスト自身に確認させること。

### 7.6 Phase 3 への申し送り

- `Contact` の符号規約は **normal が A から B へ向かう向き**（B を normal 方向へ depth 動かすと分離する）。
  転送側（`AABB×Sphere`・`Sphere×Capsule`）は必ず法線を反転すること。U14 がこれを見張る。
- `Geometry::AABB` は `BoundingBox` の別名なので、既存の `BoundingBox::TransformBy` がそのまま使える。
  Phase 3 の `GetWorldShape()` はこれを土台にできる。
- カプセルの交差判定と `Contact` は実装済み。`CapsuleCollider` を足す準備はできている。
- `RayHit` は `tMin`/`tMax` を持つので、Phase 5 のレイキャスト問い合わせ API はこの上に薄く乗せられる。

---

## 8. Phase 3 実施結果（2026-08-06）

### 8.1 `BoundingBox` の置き場所を統一（形状定義の一元化）

Phase 2 の時点で **AABB だけが `Math/BoundingBox.h` に独立**しており、他の 5 形状
（Sphere / Capsule / LineSegment / Ray / Plane）が `Math/Geometry/Shapes.h` にあるという
不揃いが残っていた。これを解消した。

- `struct BoundingBox` の中身を `Geometry::AABB` として `Shapes.h` へ移動
- 自由関数だった `ExpandAABB` / `ExpandAABBWithPoint` / `CreateAABBFromPoints` は
  `AABB::Expanded()` / `Including()` / `FromPoints()` のメンバ・静的メンバへ（呼び出し元は
  当時 0 件だったので移行コストなし）
- `Math/BoundingBox.h` は**削除**。`#include` していた 7 ファイルを `Shapes.h` へ張り替え
- 互換のため `namespace CoreEngine { using BoundingBox = Geometry::AABB; }` を Shapes.h に置いた。
  描画・カリング側は歴史的に `BoundingBox` と呼んでおり、100 箇所超の一斉改名は
  得るものがないため**名前だけ残す**判断。新規コードは `Geometry::AABB` を使う。
- `ModelVisibility.h` の前方宣言 `struct BoundingBox;` は別名にできないので `Shapes.h` の
  include へ置き換えた（**別名は前方宣言できない**のが唯一の注意点）。

### 8.2 コライダーのコンポーネント化

| 変更 | 内容 |
|---|---|
| `Collider` を**仮想関数なしのデータクラス**へ | 形状は `CollisionShape`（タグ + オフセット + 寸法）。`type_` の手動代入と `static_cast` ダウンキャストが消滅 |
| `SphereCollider` / `AABBCollider` を**削除** | 派生クラスによる二重ディスパッチをやめた |
| 判定を**ディスパッチ表**へ | `kDispatch[ShapeType][ShapeType]` の関数ポインタ表。実装は Geometry 側の 1 箇所を呼ぶだけ。形状を増やすと表の次元が合わずコンパイルエラーになる＝埋め忘れに気づける |
| `ColliderComponent` を新設 | **1 オブジェクトに複数コライダー**。実体は個別ヒープ確保なので `Add()` が返す参照は以後の追加で無効化されない |
| ローカルオフセット対応 | `CollisionShape::offset`。スケールを掛けてワールド中心へ加算する |
| `isTrigger` / `isStatic` フラグ追加 | Phase 4（押し出し）と Phase 5（ブロードフェーズ）で使う置き場所を先に用意 |
| 同一オーナー間の判定をスキップ | 複数コライダー化に伴い必須。`CollisionManager` に 1 行追加 |
| 死コード `AABBCollider::DrawDebug` を削除 | コライダー層からレンダラ依存（`LineRendererPipeline` / `Camera`）が消えた（B-9 の依存漏れ解消） |

`Collider` の基底にあったダミー仮想関数（`SetSize` / `SetRadius`）は実体のあるメソッドになり、
形状が合わないときは何もしない点は変わらないが、**基底クラスの汚染ではなくなった**（B-2）。

### 8.3 API の互換性と 1 点の挙動変更

`AddSphereCollider` / `AddAABBCollider` / `HasCollider` / `GetCollider` / `RemoveCollider` は
シグネチャそのまま。ただし **`AddXxxCollider` の意味が「置き換え」から「追加」へ変わった**。

- 旧: 2 回呼ぶと 2 本目が 1 本目を破棄して置き換わる
- 新: 2 回呼ぶとコライダーが 2 本になる（置き換えたいなら `RemoveCollider()` してから呼ぶ）

呼び出し箇所は全て 1 回呼びだったため実害はないが、**不変条件 #1（シグネチャ不変）は守り、
意味は変えた**点を明記しておく。`RemoveCollider()` は「全部外す」に変わった。

複数本を扱う入口は `GetColliders()`（`ColliderComponent`）:

```cpp
boss->AddSphereCollider(1.0f, CollisionLayer::Boss);                      // 本体
boss->GetColliders().AddSphere(1.0f, CollisionLayer::BossAttack,
                               { 2.5f, 0.0f, 0.0f });                     // 攻撃判定
```

### 8.4 スコープ判断: カプセルはまだコライダーにしない

`Geometry` にはカプセルの交差判定と `Contact` があるが、**`AABB × Capsule` が未実装**のため
ディスパッチ表を埋められない。近似で埋めると「見た目は動くが正しくない判定」が入るので、
**Phase 3 では Sphere / Box の 2 形状に留めた**。`ColliderShapeType` に Capsule を足すのは
`AABB×Capsule`（線分と AABB の最短距離）を正しく実装してからにする。

### 8.5 実測結果 — **PASS 25 / FAIL 0 / 進行中 1**

Phase 2 の 24 件はすべて PASS のまま。複数コライダーの回帰テストを 1 件追加した。

| ID | 内容 | 実測値 |
|---|---|---|
| T11 | 1 オブジェクトに本体判定 + 攻撃判定 | `本数=2 / 本体側 Enter=1 / 攻撃側 Enter=1` |

`Boss` レイヤーの本体（中心）と `BossAttack` レイヤーの攻撃判定（+X に 2.5 オフセット）を
1 つのオブジェクトに持たせ、それぞれに触れる位置へ別々の相手を置いて検証している。
レイヤー enum に `Boss` と `BossAttack` が両方あるのに片方しか使えなかった矛盾（B-3）が解消した。

### 8.6 Phase 4 / 5 への申し送り

- **フォルダ名 `Engine/Src/Collider/` はまだ改名していない**（計画 §4.2 の目標は `Collision/`）。
  クラス構造の入れ替えと同じコミットでパス変更まで行うと切り分けが難しくなるため見送った。
  `CollisionWorld` / `BroadPhase` を新設する Phase 5 でまとめて行うのが自然。
- `Collider::Intersects()` は既に `Contact*` を受け取れる。Phase 4 の押し出しは
  `CollisionManager` が `Contact` を受け取ってコールバックへ渡す配線から始められる。
- `isTrigger` / `isStatic` はフラグを置いただけでまだ誰も読んでいない。

---

## 9. Phase 4 実施結果（2026-08-06）

### 9.1 接触情報をコールバックまで届ける

`CollisionInfo`（`Engine/Src/Collider/CollisionInfo.h`）を新設し、衝突コールバックへ
**法線・貫通深度・接触点・どのコライダー同士か**を渡せるようにした。

```cpp
struct CollisionInfo {
    GameObject* other;          // 相手
    Collider*   selfCollider;   // 自分側（複数持ちの識別用）
    Collider*   otherCollider;  // 相手側
    Vector3     normal;         // 自分 → 相手 の押し出し方向
    float       depth;          // 貫通深度
    Vector3     point;          // 代表接触点
};
```

**後方互換の作り**: `GameObject` に `OnCollisionEnter(const CollisionInfo&)` を追加し、
その**既定実装が従来の `OnCollisionEnter(GameObject*)` へ転送する**。判定システムは
常に `CollisionInfo` 版を呼ぶので、

- 相手だけ分かればよい既存コード → `GameObject*` 版の override がそのまま動く
- 法線が要るコード → `CollisionInfo` 版を override する

の両方が成立する。テストプローブは**両方を override**して片方から片方へ流しており、
新旧どちらの経路も毎回検証している（T13 が接触情報、T1〜T11 が旧 API 経由のカウンタ）。

### 9.2 めり込み解消（押し出し）

`CollisionResolver::Resolve(a, b, contact)` を新設。`CollisionManager` が衝突を検出したら
**コールバックより先に**呼ぶ（コールバックへは解決前の接触情報を渡す）。

| 条件 | 挙動 |
|---|---|
| どちらかが `isTrigger`（既定） | 何もしない — **既存の通知専用の挙動は不変** |
| 両方が動ける | 法線方向へ半分ずつ分担して離す |
| 片方が `isStatic` または押し出しを受け付けない | 動ける方を全量押し出す |
| 両方とも動かせない | 何もしない |

オブジェクトを動かす口は `GameObject::TryApplyCollisionPush(delta)`。**既定は `false`（動かせない）**で、
`ModelGameObject` が `translate` を更新して `true` を返す。false を返した側は解決器が
「動かせない相手」として扱い、もう一方を全量押し出す — 位置を持たないオブジェクト
（`GridRenderer` など）に押し出しを要求しても静かに壁として振る舞う。

**制限**: 反復ソルバではなく 1 ペアずつ順に解く単発方式。3 つ以上が同時に押し合う状況では
1 フレームで収束しない。また親を持つオブジェクトでは、ワールドの delta をローカル
`translate` にそのまま足すため親の回転・スケールを考慮していない。

### 9.3 押し出し量の計算式を 1 箇所に

`Geometry::OverlapOnAxis(aMin, aMax, bMin, bMax, out)` を追加し、以下の 2 箇所で共有した。

- `Intersect(AABB, AABB, Contact*)` — 3 軸それぞれで呼び、最小の軸を押し出し方向にする
- `TileCollider::Resolve` — X 軸・Y 軸それぞれで呼ぶ

`TileCollider` の**軸独立解決（X→Y の順）と接地/天井/壁フラグはそのまま維持**した。
3D 側の「最小移動量の軸ひとつだけで押し出す」方式と 2D プラットフォーマの
「軸ごとに独立して解く」方式は**目的が違うので統合してはいけない**（コーナーの
引っかかりを避けるための設計）。共有したのは重なり量と向きを求める式だけ。

なお `TileCollider` 側の「接しているだけなら衝突としない」早期 return は残してある。
`OverlapOnAxis` は接触（depth = 0）でも true を返すため、これを外すと
「ちょうど接した瞬間に壁フラグが立つ」という挙動変化になる。

### 9.4 実測結果 — **PASS 27 / FAIL 0 / 進行中 1**

Phase 3 の 25 件はすべて PASS のまま。2 件追加。

| ID | 内容 | 実測値 |
|---|---|---|
| T12 | 非トリガー同士は押し出されて止まる | `x = -2.000`（期待 -2.000） |
| T13 | `CollisionInfo` がコールバックへ届く | `normal=(1.00, 0.00, 0.00) depth=0.800` |

T12 は半径 1 の球が毎フレーム +X へ 0.1 進み続け、Static・非トリガーの壁に当たって
中心 x = −2（壁面 −1 − 半径 1）でぴたりと止まることを確認している。
**このテストだけは位置を毎フレーム「代入」せず「加算」している** — 代入すると
押し出された結果を上書きしてしまいテストにならない。

### 9.5 Phase 5 への申し送り

- 押し出しは単発解決。反復回数の設定や、速度を持つ物体の扱い（跳ね返り・摩擦）は未実装。
- `CollisionManager::CheckAllCollisions` はペアを順に処理しながら位置を書き換えるので、
  同一フレーム内で後のペアは更新後の位置で判定される。ブロードフェーズを入れるときは
  この副作用を壊さないこと（あるいは意図的に「検出フェーズ → 解決フェーズ」に分ける）。
- フォルダ `Engine/Src/Collider/` → `Collision/` の改名は Phase 5 で行う予定のまま。

---

## 10. Phase 5 実施結果（2026-08-06）

### 10.1 フォルダとクラスの改名

計画 §4.2 の目標構成に合わせた。

| 旧 | 新 |
|---|---|
| `Engine/Src/Collider/` | `Engine/Src/Collision/` |
| `CollisionManager` | `CollisionWorld` |

「登録して総当たりするマネージャ」から「問い合わせもできるワールド」に役割が変わったための改名。
`CheckAllCollisions()` は `Step()` に改めた。

### 10.2 ABA を構造的に排除（計画からの逸脱: ハンドル → 一意 ID）

計画では `ColliderHandle`（index + generation）としていたが、**単調増加の一意 ID** にした。

- **理由**: index+generation は O(1) の配列参照が要る場合の形。ここで必要なのは
  「解放済みコライダーと新しいコライダーを取り違えない」ことだけで、それは
  **再利用されない ID** で足りる。レジストリ（スロット表・フリーリスト）を持たずに済む。
- `Collider` 生成時に `++counter` を振り、衝突履歴のキーを `pair<uint64_t, uint64_t>` に。
  ポインタが再利用されても ID は一致しないので、古いペアと混ざらない。

なお Phase 1 の修正（登録から外れたら Exit を配って履歴から消す）で ABA は既に
実害の無い状態だった。今回のは**その正しさをポインタの寿命に依存させない**ための構造化。

あわせて Exit の配り方を整理した。Phase 1 では「登録から外れたぶん」と「離れたぶん」を
別ループで処理していたが、履歴を `unordered_map<PairKey, PairColliders>` にしたことで
**1 ループにまとまった**（外れたペアは候補にも上がらないので自動的に「今フレーム接触なし」に落ちる）。

### 10.3 ブロードフェーズ

`IBroadPhase` を切り出し、`BruteForceBroadPhase`（既定）と `UniformGridBroadPhase` を実装。

**設計上の要点: ブロードフェーズは `Collider` を知らない。** 登録するのは
`Proxy{ bounds, layerMask, layerIndex, ownerId, isStatic }` という純データで、返すのは
インデックスのペア。おかげで **GameObject を作らずに単体テストできる**（U16）。

足切り条件（静止同士・同一オーナー・レイヤーマスク）は `IBroadPhase.h` の
`ShouldTestPair()` に集約し、両実装が同じ関数を呼ぶ。実装ごとに条件が食い違う事故を防ぐ。

一様グリッドの注意点:
- セル境界をまたぐ物体は複数セルに入るので、同じペアが複数回出る → 列挙後に重複除去
- 巨大な AABB が何万セルにも展開されるのを防ぐため `kMaxCellsPerProxy = 512` の保険。
  超えたものは「常に候補」として全件と突き合わせる
- セル座標のハッシュは splitmix64 で混ぜる（そのまま XOR すると格子状に偏る）

### 10.4 LayerMask による早期棄却

`CollisionConfig` にビットマスク行（`uint64_t`）を追加し、マトリクスと常に同期させる。
ブロードフェーズは形状の AABB 判定より**先に**ビット演算 1 回で落とす。
`CollisionLayer` が 64 を超えたら `static_assert` で気づけるようにしてある。

### 10.5 問い合わせ API

`CollisionWorld` に追加:

- `Raycast(ray, maxDistance, layerMask, RaycastHit*)` — 最近ヒット
- `RaycastAll(...)` — 距離順に全ヒット
- `OverlapSphere(sphere, layerMask, out)` / `OverlapBox(box, layerMask, out)`

`BaseScene::GetCollisionWorld()` から取得できる。
**注意**: 問い合わせは直近の `Step()` 時点の登録内容を見る。`OnUpdate`（判定フェーズより前）
から呼ぶと 1 フレーム前の状態になる。

### 10.6 細かい改善

- ペア集合を毎フレーム作り直さず `swap` して使い回す（`clear()` は capacity を残す）
- ハッシュを `a ^ b` から **splitmix64 で混ぜてから合成**する形に（B-6）
- `colliders_` / ブロードフェーズの内部バッファも `clear()` のみで capacity 維持

### 10.7 実測結果 — **PASS 29 / FAIL 0 / 進行中 1**

| ID | 内容 | 実測値 |
|---|---|---|
| U16 | BruteForce と UniformGrid が同じ候補を返す | 両方 11 組で一致 |
| T14 | Raycast が手前のコライダーを返す | `T14_Near に distance=5.000` |

さらに **ブロードフェーズを `UniformGrid` に切り替えた状態でシーン全体を実行し、
29 件すべてが PASS のままであること**を確認した（実装を差し替えても挙動が変わらない）。

U16 には「そもそもペアが 1 組以上出ていること」という条件を入れてある。Phase 2 の U14 で
空振り PASS を作った反省から、比較系のテストには必ず非自明性のチェックを付ける。

### 10.8 検証時の事故: PowerShell の一括置換で 2 ファイルを破損させた

フォルダ改名の仕上げに PowerShell のハッシュテーブルで置換ペアを回したところ、
**要素が 1 つだけのネスト配列が展開され**、`$pair[0]` が配列ではなく文字列の 1 文字目を
指してしまった。さらに `-replace` は既定で**大文字小文字を区別しない**ため、
`'C'` → `'o'` の 1 文字置換が全体に走り、`CollisionFeature.h/.cpp` の
すべての `c`/`C` が `o` になった（`#pragma once` → `#pragma onoe`）。

- 影響は 2 ファイルのみ（置換ペアが 2 つ以上のファイルは配列が展開されず無事だった）
- 特徴的な壊れ方なので `inolude|namespaoe|Objeot` で全文検索して他に無いことを確認した
- 手で書き直して復旧

**教訓**: 一括置換は「置換前後の差分行数」を確認するか、`-creplace`（大小区別）を使う。
PowerShell で配列の配列を渡すときは `,` を先頭に付けるか `Write-Output -NoEnumerate` で
展開を止める。

### 10.9 Phase 6 への申し送り

- ブロードフェーズの切り替えは `CollisionWorld::SetBroadPhase()` で手動。
  Phase 6 で `r.Collision.BroadPhase` / `r.Collision.GridCellSize` の CVar にする。
- `UniformGridBroadPhase` の `isStatic` は「静止同士を候補から外す」までしか使っていない。
  「ダーティ時のみ再挿入」（静的コライダーのセル登録を毎フレーム作り直さない）は未実装。
  現状は毎フレーム全件を入れ直している。
- クエリ API は登録済みコライダーの線形走査。ブロードフェーズの空間構造を使った
  高速化はしていない（件数が増えたら `IBroadPhase` に領域問い合わせを足す）。

---

## 11. Phase 6 実施結果（2026-08-06）

### 11.1 デバッグ描画（依存の向きを反転）

`Engine/Src/Collision/Debug/ColliderDebugRenderer.h/.cpp` を新設。

以前は `AABBCollider` が `LineRendererPipeline` / `LineManager` / `Camera` を直接 include して
`DrawDebug()` を持っていた（しかも `_DEBUG` 限定で**誰も呼んでいない死コード**）。
これを **描く側がコライダーを見に行く向きへ反転**させた。`Collision/` のコア（`Collider` /
`CollisionWorld`）はレンダラを一切知らない。

- `GridRenderer` と同じ流儀（`RenderPassType::Line` の GameObject が
  `RenderManager::GetRenderer()` からパイプラインを取って `AddLines`）
- `CollisionFeature::Initialize` が生成し、シーンと一緒に消える
- 状態で色分け: **接触中 / トリガー（通知専用） / 通常**
- `CollisionWorld::IsColliding(collider)` を追加（判定をやり直さず直近の履歴を引くだけ）

### 11.2 CVar

`cvar-system` の流儀（ファイルスコープの `static CVar<T>`）で追加した。
Engine Settings の CVar ツリーに自動で並び、JSON へも自動保存される。

| CVar | 既定 | 内容 |
|---|---|---|
| `r.Collision.DebugDraw` | false | コライダーのワイヤ表示 |
| `r.Collision.DebugDrawOnlyColliding` | false | 接触中だけ描く |
| `r.Collision.DebugColorIdle` / `ColorHit` / `ColorTrigger` | 緑 / 赤 / 青 | 線色 |
| `r.Collision.DebugLineAlpha` | 0.85 | 線の不透明度 |
| `r.Collision.DebugSphereSegments` | 16 | 球の分割数 |
| `r.Collision.BroadPhase` | 0 | 0=総当たり / 1=一様グリッド |
| `r.Collision.GridCellSize` | 8.0 | グリッドのセルサイズ |

`_DEBUG` 限定をやめたので Development ビルドでも使える。

### 11.3 Inspector のコライダータブ

`Collision/Debug/ColliderInspector.h/.cpp`。`ModelGameObject` のインスペクタに 5 つ目の
タブとして追加した（形状・半径/サイズ・オフセット・レイヤー・有効/トリガー/静止の編集、
球/箱の追加と全削除、スケール適用後の実効値表示）。

`GameObject&` を受け取る自由関数にしてあるので、`ModelGameObject` 以外の派生からも
1 行で呼べる。

### 11.4 コリジョンマトリクス編集ウィンドウ

`Collision/Debug/CollisionMatrixPanel.h/.cpp`。Engine Settings に格子状のチェックボックスを出す。
対称行列なので下三角だけ描く。

**計画からの逸脱: JSON 永続化はしない。** マトリクスはシーンごとに `OnInitialize()` の
`SetCollisionEnabled()` で組み立てるものなので、保存した設定で上書きすると
**「コードに書いてあるのに違う挙動になる」**状態を作る。水面の `Water.json` を廃止したのと
同じ理由。このパネルは実行中の調整用と位置づけ、確定した内容はシーンのコードへ書き戻す運用にした。
その旨をパネル上にも明記してある。

パネルのドロワーは何もキャプチャせず、ファイルスコープの `s_activeConfig` を読む。
`CollisionFeature` が `Initialize` で自分の config を指し、`Finalize` で `nullptr` に戻す
（`GameDebugUI` にパネルの登録解除 API が無いため、シーンの寿命に縛られるポインタを
ラムダに持たせない）。

### 11.5 実測結果 — **PASS 29 / FAIL 0 / 進行中 1**

Phase 5 から件数は変わらず全 PASS。加えて、
**`r.Collision.DebugDraw` を有効にした状態で実行し、ワイヤが実際に描かれ状態で色分けされること**を
画面キャプチャで確認した（接触中の赤、トリガー非接触の青が区別できている）。

### 11.6 残した課題

- 静的コライダーの「ダーティ時のみ再挿入」は未実装（毎フレーム全件入れ直し）
- クエリはコライダーの線形走査（ブロードフェーズの空間構造を使っていない）
- カプセル形状は `AABB×Capsule` 未実装のためコライダーとして選べない
- 押し出しは単発解決（反復ソルバではない）・親を持つオブジェクト非対応
