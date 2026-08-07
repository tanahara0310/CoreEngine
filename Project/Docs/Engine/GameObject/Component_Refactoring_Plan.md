# GameObject コンポーネント化 — 調査結果と改修計画

作成日: 2026-08-06 / 対象ブランチ: `future/water-refactoring`
調査対象: `Engine/Src/**`, `Application/Src/**`（実測値はすべてこの時点のもの）

---

## 1. 結論

**コンポーネント化すべき領域は `Engine/Src/GameObject/` を中心とした「ゲームオブジェクト層」だけ**で、
描画・GPU・環境マネージャ群（いわゆるエンジンコア）は現状の設計のままが正しい。

そして良いニュースとして、**この規模のエンジンではコンポーネント化は思ったより安い**。理由は実測で出た:

| 実測項目 | 値 | 意味 |
|---|---|---|
| `GameObject` 派生の具象クラス数 | **約 29 クラス**（Engine 13 / App 16） | 書き換え対象の母数が小さい |
| `CreateObject<T>()` の呼び出し箇所 | **15 箇所** | オブジェクト生成の入口がここだけ |
| `GameObject.h` を include するファイル | **38 ファイル** | 影響範囲が閉じている |
| `GameObject/` 全体の行数 | **3,448 行 / 21 ファイル** | 中核はこれだけ |
| `dynamic_cast<派生型*>` による型判定 | **12 箇所**（GameObject 系のみ） | ← これが全部消えるのが主な見返り |

一方で **一番深いのは Transform**（4 系統に分裂しており、しかも GPU 定数バッファを抱えている）。
ここを最初に触ると全部止まるので、実行順は「重い順」とは変えるべき（§5）。

---

## 2. 境界線 — どこをコンポーネント化するか

### 2.1 コンポーネント化する層（ゲームオブジェクト層）

| ディレクトリ | 行数 | 理由 |
|---|---:|---|
| `Engine/Src/GameObject/` | 3,448 | 本体。継承ツリーが 4 段になっており責務が団子状 |
| `Engine/Src/Collider/` | 715 | 既に `ColliderComponent` が存在（が固定メンバで取り外せない） |
| `Engine/Src/WorldTransform/` | 302 | Transform の 4 系統のうち 1 つ |
| `Engine/Src/UI/` | 567 | `UIImage` が `GameObject` 直下・独自レイアウト |
| `Engine/Src/Particle/` の GameObject 接点のみ | 759 | `ParticleSystem.h` 353 + `GpuParticleSystem.h` 406。モジュール層（5,181 行）は対象外 |
| `Application/Src/GameObjects/` | — | 継承で書かれたゲームロジックの移設先 |

### 2.2 コンポーネント化しない層（エンジンコア）— 判断理由つき

| 対象 | しない理由 |
|---|---|
| `Graphics/Common`（DirectXCommon / DescriptorManager） | フレーム内の実行順序と GPU リソース寿命が本質。1 インスタンス前提で正しい |
| `Graphics/Render`（RenderGraph / RenderPipeline / RenderTargetManager） | 「誰が誰の後に走るか」を宣言する仕組みが既にある。これがコンポーネント合成の代替物として機能している |
| `Graphics/Pipeline` / `RootSignature` / `Shader` | PSO・ルートシグネチャはリソースキャッシュ。オブジェクト単位に持たせると PSO が爆発する |
| `Atmosphere` / `VolumetricCloud` / `Water` / `PostEffect` / `RayTracing` の各 Manager | 1 シーンに 1 個。パラメータは CVar（`r.*`）で既にデータ駆動化済み。EngineSystem の ComponentManager で足りている |
| `Math` / `Math/Geometry` | 純関数群。状態を持たせてはいけない（Collision Phase 2 でここへ移設した設計を壊す） |
| `Editor/**` | `ISceneFeature` とサブシステム登録という**別の合成軸**で既に整理済み。二重に軸を足すと迷子になる |
| `Camera` | `Camera` は `final`、`ICameraController` でストラテジ分離済み。無理に GameObject 化する価値が薄い（§4-⑨で薄い橋だけ検討） |

### 2.3 【最初に決めること】"Component" の語がすでに 2 つの意味で使われている

```cpp
// ① サービスロケータ（エンジンサブシステム取得）— 既存
engine->GetComponent<DirectXCommon>();
engine->GetComponent<ModelManager>();

// ② 本物のゲームプレイコンポーネント — 既存（Collision Phase 3 で追加）
gameObject->GetColliders();   // 型は ColliderComponent
```

ここに `gameObject->GetComponent<MeshRenderer>()` を足すと、**同名で意味が違う API が 2 つ並ぶ**。
着手前に方針を決めること。推奨は次のどちらか:

- **案 A（推奨）**: エンジン側を `GetSubsystem<T>()` にリネームする。呼び出し箇所は機械的置換で済む
- **案 B**: ゲーム側を `FindComponent<T>()` / `Get<T>()` にする。エンジン側を触らない代わりに一般的でない名前になる

---

## 3. 現状の棚卸し

### 3.1 継承ツリー（実測 / doc コメント内の例示クラスは除外）

```
GameObject                                     376行(h) + 275行(cpp)
├─ ModelGameObject                             184 + 495   ← Transform/Model/Texture/Blend/CustomShader/MaterialUI
│  ├─ AnimatedModelObject                      154 + 269   ← Skeleton/Clip/Blend/Joint/骨デバッグ描画
│  │  └─ (App) WalkModelObject, FoxObject, BrainStemObject
│  ├─ PrimitiveGameObject                       43         ← 手続き的メッシュ生成
│  │  ├─ InfiniteGroundObject                              ← エンジン
│  │  ├─ WaterPlaneObject                      208         ← エンジン（水面がGameObjectを継承）
│  │  └─ (App) Cube/Cylinder/Plane/Ring/PrimitiveSphere/WeaponObject
│  ├─ DynamicModelObject                                   ← エディタからのD&D生成用
│  └─ (App) ModelObject, PlaneModelObject, SphereObject
├─ SpriteObject                                232 + 494   ← 独自 SpriteTransform
├─ UIImage                                     136 + 293   ← 独自 UILayout
├─ ParticleSystem       : GameObject, IParticleSystem  353 ← 独自 emitterTransform_（多重継承）
├─ GpuParticleSystem    : GameObject, IParticleSystem  406 ← 同上
├─ LineDrawable                                 65         ← エンジン内部のデバッグ線
├─ GridRenderer                                105         ← エンジン内部のグリッド
└─ (App) SkyBoxObject, HeadlessProbe, ModelProbe<Base>
```

### 3.2 「継承では書けなかった」ことの実物証拠 3 件

これがコンポーネント化の必要性を示す最も強い根拠。すでにコード側で回避策が発生している。

**証拠① CRTP ミックスインが出現している**（`Application/Src/Scenes/CollisionTestScene/CollisionProbeObject.h:45`）
```cpp
template <class Base>
class ModelProbe : public Base {              // 見た目クラスをテンプレート引数で受ける
    static_assert(std::is_base_of_v<CoreEngine::ModelGameObject, Base>, ...);
    void OnCollisionEnter(GameObject* other) override { ... }
```
「球でもキューブでも同じ衝突カウント挙動を付けたい」を継承で表現できず、テンプレートに逃げている。
**これはまさに `ProbeComponent` を `AddComponent` するだけで済む話**。

**証拠② 生ポインタでの手動追従**（`Application/Src/GameObjects/AnimatedModel/WeaponObject.h`）
```cpp
class WeaponObject : public CoreEngine::PrimitiveGameObject {
    const CoreEngine::AnimatedModelObject* owner_ = nullptr;  // 追従元を生ポインタで保持
    std::string jointName_;
    void OnUpdate() override;   // TransferMatrix の後にワールド行列を上書き
```
「プリミティブメッシュ ＋ ジョイント追従」を継承で書けないため、片方を継承・片方を生ポインタにしている。
ヘッダのコメント自体が罠を明記している —「追従元キャラクターより後に生成すること（登録順に Update されるため）」。
**`SkeletonSocketComponent` なら更新順序を依存関係で解けるので、この罠が消える**。

**証拠③ 多重継承の発生**（`ParticleSystem.h:70`, `GpuParticleSystem.h`）
```cpp
class ParticleSystem : public GameObject, public IParticleSystem { ... };
```

### 3.3 Transform が 4 系統に分裂している

| 保持クラス | 型 | GPU定数バッファ |
|---|---|---|
| `ModelGameObject` | `WorldTransform`（親子・クォータニオン対応） | **持つ**（`ConstantBufferDataWorldTransform`） |
| `SpriteObject` | `SpriteTransform` | — |
| `UIImage` | `UILayout`（anchor / pivot / sortOrder） | — |
| `ParticleSystem` | `EulerTransform emitterTransform_` | — |

この分裂を吸収するために存在しているのが `Engine/Src/Editor/ImGui/GameObjectDebugAccess.h`:
```cpp
inline bool TryGetTransformAccess(GameObject* obj, TransformAccess& out) {
    if (auto* spriteObj = dynamic_cast<SpriteObject*>(obj)) { ...SpriteTransform...  return true; }
    if (auto* modelObj  = dynamic_cast<ModelGameObject*>(obj)) { ...WorldTransform... return true; }
    return false;   // ← UIImage と ParticleSystem はここで落ちる（ギズモが効かない）
}
```
**このファイルは TransformComponent が入った時点で丸ごと不要になる。**

### 3.4 消える `dynamic_cast` 一覧（GameObject 系 12 箇所）

| 箇所 | 現在の型判定 | コンポーネント化後 |
|---|---|---|
| `EngineSystem/Subsystem/RayTracingSubsystem.cpp:66` | `ModelGameObject*`（TLAS 構築） | `ForEach<MeshRenderer>()` |
| `EngineSystem/Subsystem/DebugSubsystem.cpp:200,220` | `ModelGameObject*`（IBL 強度の一括／個別） | 同上 |
| `Editor/Scene/SceneDebugEditor.cpp:465` | `ModelGameObject*` | 同上 |
| `Editor/ImGui/ObjectSelector.cpp:212` | `SpriteObject*` | `ForEach<SpriteRenderer>()` |
| `Editor/ImGui/GameObjectDebugAccess.h:28,35,49` | `SpriteObject*` / `ModelGameObject*` ×2 | `GetComponent<Transform>()` |
| `Editor/ImGui/CanvasViewport.cpp:202` | `UIImage*` | `ForEach<UIImageComponent>()` |
| `Scene/Feature/EnvironmentFeature.cpp:116,135` | `SkyBoxObject*` / `InfiniteGroundObject*` | タグ or 専用コンポーネント |
| `Graphics/Water/Render/WaterRenderFeature.cpp:86,207` | `SkyBoxObject*` / `WaterPlaneObject*` | 同上 |

---

## 4. 重い順の一覧（本題）

見積は「1 人日 = 集中して 1 日」換算。行数は **新規** と **改修**（既存の書き換え行）を分けて記載。

| # | 項目 | 新規 | 改修 | 触るファイル | 見積 | 依存 |
|---:|---|---:|---:|---:|---:|---|
| ① | **TransformComponent への一元化** | ~400 | ~700 | 15+ | **3〜5 日** | ③ |
| ② | **ModelGameObject の分解**（MeshRenderer / MaterialComponent） | ~500 | ~900 | 22 | **3〜4 日** | ③① |
| ③ | **コンポーネント基盤の新設**（IComponent / ホスト / 更新順序 / シリアライズ） | ~800 | ~300 | 6 | **2〜3 日** | なし |
| ④ | **データ駆動シーン・プレハブ**（任意・最後） | ~600 | ~200 | 8 | **2〜3 日** | ①②③ |
| ⑤ | **AnimatedModelObject の分解**（Animator / SkeletonSocket） | ~350 | ~500 | 6 | **1.5〜2 日** | ②③ |
| ⑥ | **エンジン内部の GameObject 継承の解消** | ~200 | ~450 | 8 | **1〜1.5 日** | ③ |
| ⑦ | **ColliderComponent の本格コンポーネント化** | ~150 | ~400 | 10 | **1〜1.5 日** | ③（Collision Phase4/5 と統合） |
| ⑧ | **ParticleEmitterComponent** | ~200 | ~300 | 5 | **1 日** | ①③ |
| ⑨ | **SpriteRenderer / UI の統合** | ~250 | ~400 | 6 | **1 日** | ①③ |
| ⑩ | **CameraComponent**（任意・薄い橋だけ） | ~150 | ~50 | 3 | **0.5 日** | ①③ |
| | **合計** | ~3,600 | ~4,200 | — | **16〜23 日** | |

---

### ① TransformComponent への一元化 — 最重量（3〜5 日）

**なぜ一番重いか**: 4 系統に分裂している上に、`WorldTransform` が**純データではない**。

```cpp
// WorldTransform.h — per-object の D3D12 定数バッファを抱えている
void Initialize(ID3D12Device* device);          // ← デバイスが必要
void TransferMatrix();                          // ← 毎フレーム GPU へ転送
D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;
```
`Model::Draw(const WorldTransform& transform, ...)` が直接この GPU アドレスを引くため、
「Transform を POD にして描画側で束ねる」には描画経路も同時に触ることになる。

**変わるもの**
- `GameObject::GetWorldPosition()` の**純粋仮想が不要になる**（現在 29 クラス全部に実装義務がある。しかも「実装忘れで全員が原点に重なる無音バグ」がヘッダに警告として残っている）
- `GetWorldScale()` も同様（Collision Phase 1 で追加した暫定 API）
- ギズモ / Undo・Redo / インスペクタ / `OnSerialize()` の `"transform"` キー
- `GameObjectDebugAccess.h` が**丸ごと削除**できる
- UIImage・ParticleSystem でもギズモが効くようになる（現在は効かない）

**実装の分割案（一度にやらない）**
1. `TransformComponent` を `WorldTransform` の**ラッパ**として追加。GPU バッファはまだ `WorldTransform` に持たせたまま
2. `ModelGameObject::transform_` を `TransformComponent` 経由アクセスに置換（`GetTransform()` の 59 箇所は戻り値型を維持すれば無変更で通せる）
3. Sprite / UI / Particle を順に移す（各々独立してテスト可）
4. 最後に GPU バッファを Transform から剥がすかを判断（**やらなくても良い**。剥がすと描画経路まで波及する）

**リスク**: シリアライズ互換。`Application/Assets/Scenes/*/*.json` と `Application/Saved/EditorSettings/` の既存データが読めなくなると、シーンの見た目が全部飛ぶ。**旧キーの読み取りフォールバックを必ず残す**。

---

### ② ModelGameObject の分解 — 2 番目に重い（3〜4 日）

**現状 1 クラスに 8 責務**（`ModelGameObject.h` 184 行 + `.cpp` 495 行）:
Transform 所有 / Model 所有 / Texture 所有 / BlendMode / CustomShaderProvider + PSO 構築 / MaterialDebugUI / 視錐台カリング / シリアライズ。

**追い風になる既存設計**: `Model` はすでに
```cpp
void Draw(const WorldTransform& transform, const DrawViewInfo& view, texHandle);
```
という形をしている。**つまり Model は既に「レンダラーコンポーネントの中身」の形**で、
所有関係を継承からメンバへ移すだけで `MeshRendererComponent` になれる。大きな再設計は要らない。

**分解先**
| コンポーネント | 中身 |
|---|---|
| `MeshRendererComponent` | `Model` 所有 + BlendMode + 視錐台カリング + `GetWorldBoundingBox()` |
| `MaterialComponent` | Texture ハンドル + textureName + MaterialInstance 操作 + MaterialDebugUI |
| `CustomShaderComponent` | `ICustomShaderProvider` + `CustomShaderPipeline`（使うオブジェクトは少数なので分離が効く） |

**変わる場所（22 ファイルが `ModelGameObject` を参照）**
- `RayTracingSubsystem.cpp` の TLAS 構築ループが `ForEach<MeshRenderer>()` になる（**RT シャドウの挙動が変わらないことを要検証**）
- `DebugSubsystem.cpp` の IBL 一括適用パネル 2 箇所
- `ObjectSelector.cpp` のピッキング（AABB 事前棄却 → ローカル空間レイの最適化を壊さないこと）
- `SceneDebugEditor.cpp` / `RayTracingDebugPanel.cpp`
- `PrimitiveGameObject` / `AnimatedModelObject` / `DynamicModelObject` の中間基底 3 つ
- App 側 7 クラス

**リスク**: Hi-Z オクルージョンカリングが `ModelVisibility` 経由でモデル単位 AABB を提出している。
`SubmitBounds` を毎フレーム続ける不変条件（既知）を壊すとカリングが誤爆する。

---

### ③ コンポーネント基盤の新設（2〜3 日）

**新規に書くもの**
```
Engine/Src/GameObject/Component/
  IComponent.h            Awake/Start/Update/LateUpdate/OnDestroy/OnSerialize/OnDeserialize
  ComponentHost.h/.cpp    AddComponent<T>(args...) / GetComponent<T>() / GetComponents<T>()
  ComponentRegistry.h     型名 ↔ ファクトリ（④ のプレハブで使う。ここでは器だけ）
```

**参照安定性と遅延解放は既存パターンをそのまま流用できる**。`ColliderComponent` が既にこれを解いている:
- `std::vector<std::unique_ptr<T>>` で個別ヒープ確保 → 追加しても既存の参照が無効化されない
- `retired_` へ退避してフレーム末に解放 → コールバック中の着脱で生ポインタが宙に浮かない

**更新順序**: 既存の `SceneUpdatePhase`（`PreObjectUpdate` / `PostObjectUpdate` / `PostLogic`）と噛み合わせる。
コンポーネントに独自のフェーズ体系を持ち込むと軸が 2 本になるので、**既存フェーズに従属させる**こと。

**横断イテレート**: `GameObjectManager` に `型 → コンポーネントポインタ配列` のインデックスを追加。
これがないと ② の `ForEach<MeshRenderer>()` が全オブジェクト走査になり、`dynamic_cast` を消した意味が薄れる。

**§2.3 の命名衝突をここで解決する。**

---

### ④ データ駆動シーン・プレハブ（2〜3 日・任意・最後）

**現状の制約**: `SceneSaveSystem` は「シーンの C++ コードが先にオブジェクトを生成し、JSON は値を上書きするだけ」。
型レジストリ／ファクトリは**存在しない**（`ObjectFactory` / `CreateObjectByTypeName` などを検索して 0 件）。
そのためエディタから作れるのは `DynamicModelObject` の複製だけ（`SceneDebugEditor.cpp:484, 536`）。

**コンポーネント化後に可能になること**: `{"components":[{"type":"MeshRenderer",...},{"type":"Collider",...}]}` で
オブジェクトを丸ごと復元できる → **エディタで組んだものが保存できる**。

これは「コンポーネント化の見返り」であって前提ではない。**①②③ が終わるまで着手しないこと。**

---

### ⑤ AnimatedModelObject の分解（1.5〜2 日）

**既にほぼ分離されている**: `AnimationPlayer` は `Model` が所有（`Model::SetAnimationPlayer`）。
`AnimatedModelObject`（154 + 269 行）に残っているのは クリップ登録 / 切替 / ブレンド / ジョイントワールド行列 / 骨デバッグ描画。

**分解先**
- `AnimatorComponent` — クリップ登録・`SwitchAnimation` / `SwitchAnimationWithBlend`・`GetSkeleton()`
- `SkeletonSocketComponent` — §3.2 証拠② の `WeaponObject` をここへ吸収。「ジョイント名 + オフセット」で親子付け

**効く点**: `WeaponObject`（プリミティブ + ジョイント追従）と `WalkModelObject::SetHandParticleSystem`
（手からパーティクル）が**同じ 1 つのコンポーネントで表現できる**。生成順の罠も更新依存で解ける。

**リスク**: Mixamo リグの cm 単位（ジョイント行列にスケール 0.01 が入る）補正を落とすと武器が消える（既知）。

---

### ⑥ エンジン内部の GameObject 継承の解消（1〜1.5 日）

`LineDrawable`(65) / `GridRenderer`(105) / `WaterPlaneObject`(208) / `InfiniteGroundObject` / App の `SkyBoxObject`
が `GameObject` を継承している。**デバッグ線描画とグリッドがゲームオブジェクトである必要はない。**

そして「シーンから特定の型を探す」ために `dynamic_cast` が発生している:
```cpp
EnvironmentFeature.cpp:116   dynamic_cast<SkyBoxObject*>(obj.get())
EnvironmentFeature.cpp:135   dynamic_cast<InfiniteGroundObject*>(obj.get())
WaterRenderFeature.cpp:86    dynamic_cast<SkyBoxObject*>(obj.get())
WaterRenderFeature.cpp:207   dynamic_cast<WaterPlaneObject*>(obj.get())
```

**方針**: 2 種類に分ける。
- **エンジン内部の描画物**（LineDrawable / GridRenderer）→ `GameObject` を降りて `RenderFeature` 直接管理へ
- **シーンに置かれる環境物**（SkyBox / Ground / WaterPlane）→ `SkyBoxComponent` / `WaterSurfaceComponent` を引く

**リスク**: 水面は「反射ビューで水面自身を描くと夜に大きな明暗斑が出る」既知バグの修正が入っている。
`WaterPlaneObject` の登録経路を変えるときは反射ビューでのパス実行条件を必ず維持すること。

---

### ⑦ ColliderComponent の本格コンポーネント化（1〜1.5 日）

**現状**: `ColliderComponent` という名前で存在するが、`GameObject` の**固定メンバ**である。
```cpp
protected:
    ColliderComponent colliders_;   // 取り外せない。IComponent 派生でもない
```
その結果、基底クラスに委譲 API が 9 本生えている（`AddSphereCollider` / `AddAABBCollider` / `HasCollider` /
`GetCollider`×2 / `RemoveCollider` / `ReleaseRetiredColliders` / `GetColliders`×2）。基底肥大の直接原因。

**重要**: `Docs/Engine/Collision/Collision_Refactoring_Plan.md` の **Phase 4（接触情報・押し出し・TileCollider 統合）**
と **Phase 5（ハンドル化・BroadPhase）** が設計上ここと重なる。
**別々にやると二重工事になる。Phase 4/5 と同時に進めること**（そうすれば追加コストは小さい）。

---

### ⑧ ParticleEmitterComponent（1 日）

`ParticleSystem`(353) / `GpuParticleSystem`(406) は既に `GameObject, IParticleSystem` の多重継承。
`Particle/` 全体は 5,940 行あるが、**GameObject 接点はこの 2 ヘッダだけ**でモジュール層（Shape / Force など）は無関係。

App 側の証拠: `WalkModelObject::SetHandParticleSystem(IParticleSystem*, jointName)` —
キャラが手のパーティクルを自前で追従させている。⑤ の `SkeletonSocketComponent` +
`ParticleEmitterComponent` の組み合わせで消える。

---

### ⑨ SpriteRenderer / UI の統合（1 日）

`SpriteObject`(232 + 494) と `UIImage`(136 + 293) が別系統で 2D を描いており、両方 `GameObject` 直下・
両方独自トランスフォーム。`ObjectSelector.cpp:212` と `CanvasViewport.cpp:202` が `dynamic_cast` で振り分けている。

① が終わっていれば、`SpriteRendererComponent` / `UIImageComponent` に落とすのは機械的な作業。

---

### ⑩ CameraComponent（0.5 日・任意）

`Camera` は `final` で `GameObject` 外。`CameraManager` + `ICameraController`（FreeLook / OrbitFly）で
既に整理されており、**無理にコンポーネント化する価値は低い**。

ただし「キャラに追従するカメラ」を書くには GameObject 側に足場が要る。
`TransformComponent` の値を `Camera` へ流すだけの薄い `CameraComponent` なら安く、既存設計を壊さない。

---

## 5. 推奨実行順（重い順とは違う）

重量順に着手すると基盤なしで最難関を触ることになるので、**実行順は次を推奨**:

```
③ 基盤 ＋ 命名方針の決定           （2〜3 日）  ← ここで既存挙動は一切変えない
  ↓
⑦ Collider（Collision Phase4/5 と同時）（1〜1.5 日）  ← 既にテストシーンがある領域で基盤を実証
  ↓
① Transform（段階分割・§4① の 4 ステップ）（3〜5 日）
  ↓
② MeshRenderer / Material          （3〜4 日）
  ↓
⑤ Animator / SkeletonSocket        （1.5〜2 日）
  ↓
⑧ ⑨ ⑥（独立・並行可）              （3〜4 日）
  ↓
⑩ ④（任意）
```

**⑦ を 2 番目に置く理由**: `CollisionTestScene`（PASS 25 / FAIL 0 の回帰スイート）が既にあるため、
**「基盤が正しいか」を客観的に検証できる唯一の領域**。ここで基盤を試して失敗を早く出す。

**最小で止めても価値が出る地点**: ③ + ⑦ + ① まで（6〜9 日）。
この時点で `GameObjectDebugAccess.h` が消え、UI/Particle にギズモが効き、`GetWorldPosition()` の
実装義務（無音バグの温床）が全 29 クラスから消える。

---

## 6. 不変条件と罠

| # | 内容 |
|---|---|
| 1 | **シリアライズ互換を壊さない**。`Application/Assets/Scenes/*/*.json` と `Application/Saved/EditorSettings/` の旧キーに読み取りフォールバックを残す。落とすとシーンの見た目が全部飛ぶ |
| 2 | **ODR 事故に注意**。`GameObject` / `Collider` などのサイズが変わる変更を増分ビルド中に中断するとヒープ破損（`c0000374`）になる前例あり。**構造体サイズが変わるコミットではクリーンビルド必須**（VS では通るのに コマンドライン MSBuild で落ちる） |
| 3 | **`WorldTransform` は純データではない**（GPU 定数バッファ持ち）。POD 化を目標にしないこと。①の段階 4 は「やらない」も正解 |
| 4 | **Hi-Z カリングの不変条件**: `SubmitBounds` を毎フレーム継続すること・GameView 限定であること。② で壊すとカリングが誤爆する |
| 5 | **反射ビューでの水面**: `WaterPlaneObject` の登録経路を変えるとき、反射ビューで水面自身を描かない条件を維持すること（夜の明暗斑の既知原因） |
| 6 | **RT シャドウの TLAS**: ② で `dynamic_cast<ModelGameObject*>` を消すとき、半透明除外（`GetBlendMode() != kBlendModeNone`）の条件を落とさないこと |
| 7 | **`GetComponent` の意味の衝突**（§2.3）。着手前に決める |
| 8 | **`ObjectSelector` の最適化を維持**: AABB 事前棄却 → ローカル空間レイ。② で素直に書き直すと 48fps へ戻る |
| 9 | **App Editor に登録解除 API が無い**（既知）。オブジェクト構成を動的に変える ④ で踏む可能性がある |
| 10 | **`vcxproj` への手動登録が必要**。`SyncFilters` はファイルを追加しない仕様。新規コンポーネントファイルは毎回手で足すこと |

---

## 7. 検証方法

| 対象 | 手段 |
|---|---|
| ⑦ Collider | `CollisionTestScene` の PASS/FAIL ログ（現在 PASS 25 / FAIL 0）。**ExitCode を必ず見る**（見ないとクラッシュを見逃す罠） |
| ① Transform | 各シーンを起動して JSON 復元後の見た目が一致するか。ギズモ操作 → 保存 → 再起動の往復 |
| ② MeshRenderer | RT シャドウの有無・Hi-Z カリングの棄却数・`ObjectSelector` 連打時の FPS（ステータスバー読み取り） |
| ⑤ Animator | `AssignmentScene`（武器のジョイント追従・手のパーティクル）が破綻しないか |
| ⑥ 環境物 | 昼夜での水面・スカイボックス・無限床の見た目（夜の明暗斑が再発しないか） |
| 全体 | 起動 → 各シーン巡回 → GPU 統計（`EngineStatsWindow`）で GBuffer 時間が退行していないか |

---

## 8. まとめ

- **コンポーネント化すべきは `GameObject/` を中心とした約 5,700 行のゲームオブジェクト層**のみ。
  Graphics / Render / 環境マネージャ群は現状設計が正しく、触らない
- **全部やると 16〜23 日**。ただし ③ → ⑦ → ① の **6〜9 日で主な痛みは消える**
- 母数が小さい（派生 29 クラス / 生成 15 箇所 / include 38 ファイル）ので、この手の refactoring としては安い
- すでに CRTP ミックスイン・生ポインタ追従・多重継承という**3 つの回避策が発生している**。
  これは「継承の限界に達している」という客観的な兆候
