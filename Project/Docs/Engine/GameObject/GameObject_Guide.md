# GameObject 使用ガイド（コンポーネント版）

> 2026-08-08 更新。`Engine/Src/GameObject/README.md`（継承前提の旧ガイド）を置き換えたもの。
> 経緯と設計判断は [Component_Refactoring_Plan.md](Component_Refactoring_Plan.md) を参照。

## 基本の考え方

`GameObject` は**それ自体が実体化できる器**で、機能はコンポーネントを載せて組む。
アセット 1 種につき派生クラスを 1 個作る方式は廃止した。

```cpp
// シーンの OnInitialize() 内
auto* sphere = CreateObject("Sphere");                       // 素の GameObject
sphere->AddComponent<MeshRendererComponent>("sphere.obj");    // 見た目
sphere->AddComponent<MaterialComponent>()->SetPBR(1.0f, 0.3f);
sphere->GetComponent<TransformComponent>()->Get().translate = { 0.0f, 1.0f, 0.0f };
sphere->SetActive(true);
```

`TransformComponent` は `MeshRendererComponent` が無ければ自動で足す（Unity の
`RequireComponent` 相当）ので、明示的に付ける必要はない。

---

## コンポーネント一覧

| コンポーネント | 場所 | 役割 |
|---|---|---|
| `TransformComponent` | `Component/Transform/` | 位置・回転・スケールと GPU 定数バッファ。`ITransformSource` を実装 |
| `EulerTransformComponent` | `Component/Transform/` | GPU バッファを持たない軽量版（Sprite / Particle 用） |
| `MeshRendererComponent` | `Component/Render/` | メッシュのロード・視錐台カリング・AABB・描画 |
| `MaterialComponent` | `Component/Render/` | PBR / 色 / IBL / ライティングの一括設定 |
| `AnimatorComponent` | `Component/Animation/` | クリップのロードとスケルトンアニメーションの駆動 |
| `SkeletonSocketComponent` | `Component/Animation/` | 他オブジェクトのジョイントへ追従（武器の手持ちなど） |
| `ColliderComponent` | `Collision/` | 当たり判定（1 オブジェクトに複数可）。イベントは購読形式 |
| `SceneTagComponent<T>` | `Component/Scene/` | 型付きタグ。`dynamic_cast` 走査の置き換え |

---

## メッシュの 3 つの出どころ

```cpp
// ① モデルファイル
obj->AddComponent<MeshRendererComponent>("sphere.obj");

// ② 手続き的メッシュ（プリミティブ）
obj->AddComponent<MeshRendererComponent>(std::make_unique<CubeMeshGenerator>(1.2f));

// ③ スケルトン付き（AnimatorComponent が MeshRenderer ごと面倒を見る）
obj->AddComponent<AnimatorComponent>("walk.gltf", "walkAnimation");
```

上書きテクスチャは `renderer->SetTexture("white.png")`。空ならモデル組み込みを使う。

---

## 当たり判定

コライダーは**位置ソース（`ITransformSource`）を持つオブジェクトにしか付けられない**
（`ColliderComponent::Add()` の assert で検出される）。イベントは継承ではなく購読で受ける。

```cpp
auto* player = CreateObject("Player");
player->AddComponent<MeshRendererComponent>("player.gltf");

auto* collider = player->GetOrAddComponent<ColliderComponent>();
collider->AddSphere(1.0f, CollisionLayer::Player);
collider->SetOnEnter([](const CollisionInfo& hit) {
    // hit.object / hit.normal / hit.depth
});
```

レイヤー同士の判定はシーン側で有効化する（`SetCollisionEnabled(A, B)`）。

---

## アニメーション

```cpp
// クリップ 1 本
auto* chara = CreateObject("Character");
chara->AddComponent<AnimatorComponent>("walk.gltf", "walkAnimation");

// 複数クリップ（先頭が初期クリップ・ブレンド切替できる）
auto* fox = CreateObject("Fox");
auto* animator = fox->AddComponent<AnimatorComponent>("Fox.gltf",
    std::vector<AnimationClipDesc>{ {"Survey","Survey",""}, {"Walk","Walk",""}, {"Run","Run",""} });
animator->SwitchWithBlend("Run", 0.35f);
```

クリップは**スケルトン生成より前に全部読む必要がある**ため、後から追加できない
（コンストラクタで渡し切る）。骨のデバッグ表示は
`animator->SetSkeletonDebugDrawEnabled(true)`。

### ジョイント追従（ソケット）

```cpp
auto* weapon = CreateObject("Weapon");
weapon->AddComponent<MeshRendererComponent>(std::make_unique<CubeMeshGenerator>(0.1f));
weapon->AddComponent<SkeletonSocketComponent>()
      ->Attach(animator, "mixamorig:RightHand");
```

追従は `LateUpdate()` で行い、`GameObjectManager::UpdateAll()` が
**全オブジェクトの Update を終えてから LateUpdate をまとめて回す**ので、
**追従元より先に武器を生成しても正しく動く**。

---

## ライフサイクル

```
CreateObject(name) / CreateObject<T>(args...)
  └─ GameObjectManager::SpawnRaw()
       ├─ 名前の自動付与（未設定なら "Name_0", "Name_1" ...）
       └─ Initialize()

AddComponent<T>()
  └─ T::Awake()      ← 即座に呼ばれる。**兄弟はまだ揃っていない**

毎フレーム（GameObjectManager::UpdateAll() は 2 パス構成）
  パス 1: 全オブジェクト分  Start（初回のみ）→ コンポーネント Update → GameObject::Update
  パス 2: 全オブジェクト分  コンポーネント LateUpdate
  フレーム末: CleanupDestroyed → ReleaseRetiredComponents
```

兄弟コンポーネントを参照する初期化は **`Awake()` ではなく `Start()`** に書く。

---

## コンポーネントを自作する

```cpp
class SpinComponent : public CoreEngine::IComponent {
public:
    const char* GetTypeName() const override { return "Spin"; }

    void Start() override { transform_ = Sibling<CoreEngine::TransformComponent>(); }

    void Update() override {
        if (!transform_) { return; }
        transform_->Get().rotate.y += 0.02f;
    }

private:
    CoreEngine::TransformComponent* transform_ = nullptr;
};
```

取得は `GetComponent<T>()`。**`IComponent` を継承していないミックスイン
（`ITransformSource` / `IRenderableComponent`）も引ける**（`dynamic_cast` の
兄弟インターフェースへのクロスキャストで解決している）。

取り外し（`RemoveComponent`）はスロットを nullptr 化してフレーム末に実体を解放する
遅延解放方式なので、衝突コールバックの中で着脱しても生ポインタが宙に浮かない。

---

## ⚠️ 注意事項

- **`new` による直接生成は禁止**。`Initialize()` が呼ばれず `GameObjectManager` にも載らない
- **`Initialize()` の手動呼び出しは禁止**（`CreateObject` が呼ぶので二重初期化になる）
- **`ModelGameObject` / `PrimitiveGameObject` は移行用の薄いシム**。`WaterPlaneObject` と
  `InfiniteGroundObject` が使っているだけで、新規コードでは使わない
- 他オブジェクトへの参照はコンストラクタではなくセッターで渡す（生成順に依存しないため）
- エンジン依存（`DirectXCommon` 等）はコンストラクタでは取れない。`Initialize()` /
  コンポーネントの `Awake()` 以降で `GetEngineSystem()->GetService<T>()` を使う

---

## 未配線の契約（既知の穴）

| 契約 | 状態 |
|---|---|
| `IComponent::DrawInspector()` | 宣言だけで**誰も呼んでいない**。Inspector のタブは `GameObject::GetInspectorTabs()` 側の仕組みのみ |
| `IComponent::OnSerialize()` | 同じく未配線。シリアライズは旧来の `"transform"` キー形式のまま |

どちらもコンポーネント化の残り穴。詳細は
[Component_Refactoring_Plan.md](Component_Refactoring_Plan.md) §11.3 / §10.9。
