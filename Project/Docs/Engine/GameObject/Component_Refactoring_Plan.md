# GameObject コンポーネント化 — 調査結果と改修計画（改訂版）

> **実装状況（2026-08-07）**: ③⑦①②⑤⑧⑨ 実装完了。⑥⑩④ 未着手。
> 実施記録は **§9「実施結果」** を参照。各ステップでビルド成功 → 起動 → 描画確認 →
> `CollisionTestScene` の PASS 29 / FAIL 0 を確認しながら進めた。

作成日: 2026-08-06 / **改訂: 2026-08-07（当たり判定リファクタリング Phase 0-6 完了を反映）**
対象ブランチ: `master`（`4c7f940` 当たり判定システムの大規模リファクタリング完了、以降）
調査対象: `Engine/Src/**`, `Application/Src/**`（実測値はすべて改訂時点のもの）

> **改訂の要点**: 初版⑦は「Collision Phase 4/5 と同時にやらないと二重工事」としていたが、
> Phase 0-6 が**すべて完了**した（`Docs/Engine/Collision/Collision_Refactoring_Plan.md` §5〜§11、
> PASS 29 / FAIL 0）。これにより ⑦ は「コライダーシステムを作る仕事」から
> **「完成品を新基盤へ搭載し直す仕事」へ縮小**（1〜1.5日 → 1日）。
> 一方で GameObject 基底には衝突イベント・押し出し・位置ソースの仮想関数が増え、
> **基底の肥大はむしろ進んだ**（コンポーネント化の必要性は強まった）。

---

## 1. 結論

**コンポーネント化すべき領域は `Engine/Src/GameObject/` を中心とした「ゲームオブジェクト層」だけ**で、
描画・GPU・環境マネージャ群、そして**完成したばかりの `Collision/` コア**は現状の設計のままが正しい。

| 実測項目 | 値 | 意味 |
|---|---|---|
| `GameObject` 派生の具象クラス数 | **約 30 クラス**（Engine 14 / App 16） | 書き換え対象の母数が小さい |
| `CreateObject<T>()` の呼び出し箇所 | **15 箇所** | オブジェクト生成の入口がここだけ |
| `GameObject.h` を include するファイル | **38 ファイル** | 影響範囲が閉じている |
| `GameObject/` 全体の行数 | **3,492 行 / 21 ファイル** | 中核はこれだけ |
| `GameObject` 基底の仮想関数 | **26 個**（376 行） | うち衝突系 9 個・描画系 6 個・UI 系 5 個 |
| `dynamic_cast<派生型*>` による型判定 | **13 箇所** | ← これが全部消えるのが主な見返り |

一番深いのは変わらず **Transform**（4 系統に分裂 + GPU 定数バッファ持ち）。
実行順は「重い順」ではなく ③基盤 → ⑦Collider搭載替え → ①Transform → ②MeshRenderer（§5）。

---

## 2. 境界線 — どこをコンポーネント化するか

### 2.1 コンポーネント化する層（ゲームオブジェクト層・約 5,100 行 + 縫い目）

| ディレクトリ | 行数 | 理由 |
|---|---:|---|
| `Engine/Src/GameObject/` | 3,492 | 本体。継承ツリーが 4 段・基底 virtual 26 個 |
| `Engine/Src/WorldTransform/` | 302 | Transform 4 系統のうち 1 つ |
| `Engine/Src/UI/` | 567 | `UIImage` が `GameObject` 直下・独自レイアウト |
| `Engine/Src/Particle/` の GameObject 接点のみ | 768 | `ParticleSystem.h` 359 + `GpuParticleSystem.h` 409。モジュール層は対象外 |
| **`Collision/` の GameObject 側の縫い目のみ** | — | `colliders_` 固定メンバ・委譲 API・イベント仮想関数・位置ソース（§4-⑦） |
| `Application/Src/GameObjects/` | — | 継承で書かれたゲームロジックの移設先 |

### 2.2 コンポーネント化しない層 — 判断理由つき

| 対象 | しない理由 |
|---|---|
| **`Collision/` コア（2,008 行）** | **完成したばかりの安定資産**。CollisionWorld（ID ベース履歴で ABA 構造排除）・BroadPhase 2 種 + CVar・クエリ API・デバッグ描画/Inspector/マトリクス UI まで揃い、PASS 29 / FAIL 0 の回帰スイートで保護されている。**中を開けない。搭載口だけ差し替える** |
| `Graphics/Common`（DirectXCommon / DescriptorManager） | フレーム内の実行順序と GPU リソース寿命が本質。1 インスタンス前提で正しい |
| `Graphics/Render`（RenderGraph / RenderPipeline / RenderTargetManager） | 実行順の宣言機構が既にあり、コンポーネント合成の代替物として機能している |
| `Graphics/Pipeline` / `RootSignature` / `Shader` | PSO・ルートシグネチャはリソースキャッシュ。オブジェクト単位に持たせると PSO が爆発する |
| `Atmosphere` / `Cloud` / `Water` / `PostEffect` / `RayTracing` の各 Manager | 1 シーンに 1 個。CVar（`r.*`）で既にデータ駆動化済み |
| `Math` / `Math/Geometry` | 純関数群。Collision Phase 2 で Shapes/Intersect/Distance/RayCast へ一元化されたばかり。状態を持たせてはいけない |
| `Editor/**` | `ISceneFeature` とサブシステム登録という別の合成軸で整理済み |
| `Camera` | `Camera` は `final`、`ICameraController` でストラテジ分離済み（§4-⑩で薄い橋だけ検討） |

### 2.3 【最初に決めること】"Component" の語がすでに 2 つの意味で使われている

```cpp
// ① サービスロケータ（エンジンサブシステム取得）— 既存
engine->GetComponent<DirectXCommon>();

// ② 本物のゲームプレイコンポーネント — 既存（Collision Phase 3 で導入済み）
gameObject->GetColliders();   // 型は ColliderComponent
```

ここに `gameObject->GetComponent<MeshRenderer>()` を足すと、**同名で意味が違う API が 2 つ並ぶ**。
着手前に方針を決めること。推奨は次のどちらか:

- **案 A（推奨）**: エンジン側を `GetSubsystem<T>()` にリネーム。呼び出し箇所は機械的置換で済む
- **案 B**: ゲーム側を `FindComponent<T>()` / `Get<T>()` にする。一般的でない名前になる

---

## 3. 現状の棚卸し（改訂）

### 3.1 継承ツリー（実測）

```
GameObject                                     376行(h) + 275行(cpp)  virtual 26個
├─ ModelGameObject                             196 + 520   ← Transform/Model/Texture/Blend/CustomShader/押し出し/Inspector5タブ
│  ├─ AnimatedModelObject                      154 + 269   ← Skeleton/Clip/Blend/Joint/骨デバッグ描画
│  │  └─ (App) WalkModelObject, FoxObject, BrainStemObject
│  ├─ PrimitiveGameObject                       43         ← 手続き的メッシュ生成
│  │  ├─ InfiniteGroundObject / WaterPlaneObject(208)      ← エンジン
│  │  └─ (App) Cube/Cylinder/Plane/Ring/PrimitiveSphere/WeaponObject
│  ├─ DynamicModelObject                                   ← エディタD&D生成用
│  └─ (App) ModelObject, PlaneModelObject, SphereObject
├─ SpriteObject                                238 + 494   ← 独自 SpriteTransform
├─ UIImage                                     142 + 293   ← 独自 UILayout
├─ ParticleSystem       : GameObject, IParticleSystem  359 ← 独自 emitterTransform_（多重継承）
├─ GpuParticleSystem    : GameObject, IParticleSystem  409 ← 同上
├─ LineDrawable                                 70         ← エンジン内部のデバッグ線
├─ GridRenderer                                109         ← エンジン内部のグリッド
├─ ColliderDebugRenderer                        37+96      ← ★新顔。コライダーワイヤ表示（GridRenderer と同じ流儀）
└─ (App) SkyBoxObject, HeadlessProbe, ModelProbe<Base>
```

### 3.2 「継承では書けなかった」ことの実物証拠 — 4 件に増えた

**証拠① CRTP ミックスイン**（`CollisionProbeObject.h:45`）
```cpp
template <class Base>
class ModelProbe : public Base {   // 球でもキューブでも同じ衝突カウント挙動を付けたい
```
継承で表現できずテンプレートに逃げている。`ProbeComponent` を Add するだけで済む話。

**証拠② 生ポインタでの手動追従**（`WeaponObject.h`）
```cpp
class WeaponObject : public CoreEngine::PrimitiveGameObject {
    const CoreEngine::AnimatedModelObject* owner_ = nullptr;  // 追従元を生ポインタ保持
```
ヘッダに「追従元より後に生成すること（登録順に Update されるため）」という罠が明記されている。

**証拠③ 多重継承**（`ParticleSystem.h:70` ほか）
```cpp
class ParticleSystem : public GameObject, public IParticleSystem { ... };
```

**証拠④ ★新規: 基底 API のボイラープレートが全系統へ拡散した**
当たり判定リファクタリングで `GetWorldPosition()` が純粋仮想になり（原点重なりの無音バグ対策として
正しい判断）、`GetWorldScale()` / `TryApplyCollisionPush()` も基底に生えた。その結果:

```cpp
// UIImage.h — スクリーン座標系なのに 3D 用 API の実装を強いられ、コメントで自衛している
/// @note 3D の当たり判定に使う想定は無い。座標系が違うので混ぜないこと。
Vector3 GetWorldPosition() const override {
    return { layout_.anchoredPos.x, layout_.anchoredPos.y, 0.0f };
}
```
ワールド座標系（Model）・スクリーン座標系（UI/Sprite）・エミッタ座標系（Particle）の
**3 座標系に同じ基底 API が引き伸ばされている**。「Transform を持つものだけが持つ」構造
（= TransformComponent）ならこの実装義務ごと消える。実装ファイルは 10 ファイルに分散。

### 3.3 Transform が 4 系統に分裂している（変化なし・①の根拠）

| 保持クラス | 型 | GPU定数バッファ |
|---|---|---|
| `ModelGameObject` | `WorldTransform`（親子・クォータニオン対応） | **持つ** |
| `SpriteObject` | `SpriteTransform` | — |
| `UIImage` | `UILayout`（anchor / pivot / sortOrder） | — |
| `ParticleSystem` | `EulerTransform emitterTransform_` | — |

この分裂を吸収する `Editor/ImGui/GameObjectDebugAccess.h`（dynamic_cast 3 連発）は健在で、
**UIImage と ParticleSystem はギズモが効かないまま**。TransformComponent 導入で丸ごと消える。

### 3.4 消える `dynamic_cast` 一覧（GameObject 系 13 箇所）

| 箇所 | 現在の型判定 | コンポーネント化後 |
|---|---|---|
| `RayTracingSubsystem.cpp:66` | `ModelGameObject*`（TLAS 構築） | `ForEach<MeshRenderer>()` |
| `DebugSubsystem.cpp:200,220` | `ModelGameObject*`（IBL 強度） | 同上 |
| `SceneDebugEditor.cpp:465` | `ModelGameObject*` | 同上 |
| `ObjectSelector.cpp:212` | `SpriteObject*` | `ForEach<SpriteRenderer>()` |
| `GameObjectDebugAccess.h:28,35,49` | `SpriteObject*` / `ModelGameObject*` ×2 | `GetComponent<Transform>()` |
| `CanvasViewport.cpp:202` | `UIImage*` | `ForEach<UIImageComponent>()` |
| `EnvironmentFeature.cpp:116,135` | `SkyBoxObject*` / `InfiniteGroundObject*` | タグ or 専用コンポーネント |
| `WaterRenderFeature.cpp:86,207` | `SkyBoxObject*` / `WaterPlaneObject*` | 同上 |

### 3.5 ★当たり判定リファクタリングが残した「GameObject 側の縫い目」

Collision 側は完成したが、**GameObject 基底に載った受け口はむしろ増えた**。これが ⑦ の対象。

```cpp
class GameObject {
    // イベント 6 本（GameObject* 版 3 + CollisionInfo 版 3。Info 版が既定で旧版へ転送）
    virtual void OnCollisionEnter(GameObject* other);
    virtual void OnCollisionEnter(const CollisionInfo& info);  // ほか Stay/Exit
    // 押し出し受け入れ（Transform を持つ派生がオーバーライド）
    virtual bool TryApplyCollisionPush(const Vector3& delta);
    // コライダーの位置ソース（純粋仮想 + 既定等倍）
    virtual Vector3 GetWorldPosition() const = 0;
    virtual Vector3 GetWorldScale() const;
    // 委譲 API 9 本 + 固定メンバ
    ColliderComponent& GetColliders();          // ×2 (const)
    Collider& AddSphereCollider(...); Collider& AddAABBCollider(...);
    bool HasCollider() const; Collider* GetCollider();  // ×2 (const)
    void RemoveCollider(); void ReleaseRetiredColliders();
protected:
    ColliderComponent colliders_;   // ← 全 GameObject が無条件に所持
};
```

一方で流用できる完成品も増えた:
- `ColliderComponent` の**参照安定 + 遅延解放**パターン（③基盤がそのまま真似る手本）
- `ColliderInspector::Draw(GameObject&)` — **自由関数によるタブ内容の外出し**（②のタブ分解の前例）
- `CollisionMatrixPanel` の `s_activeConfig` 方式 — GameDebugUI に登録解除 API が無い問題の解法
- `CollisionFeature` の 毎フレーム「収集→判定」モデル — コンポーネント横断イテレーションの搭載口

---

## 4. 重い順の一覧（改訂）

見積は「1 人日 = 集中して 1 日」換算。

| # | 項目 | 新規 | 改修 | 触るファイル | 見積 | 依存 |
|---:|---|---:|---:|---:|---:|---|
| ① | **TransformComponent への一元化** | ~400 | ~750 | 15+ | **3〜5 日** | ③ |
| ② | **ModelGameObject の分解**(MeshRenderer/Material) | ~500 | ~900 | 22 | **3〜4 日** | ③① |
| ③ | **コンポーネント基盤の新設** | ~800 | ~300 | 6 | **2〜3 日** | なし |
| ④ | **データ駆動シーン・プレハブ**（任意・最後） | ~600 | ~200 | 8 | **2〜3 日** | ①②③ |
| ⑤ | **AnimatedModelObject の分解**(Animator/SkeletonSocket) | ~350 | ~500 | 6 | **1.5〜2 日** | ②③ |
| ⑥ | **エンジン内部の GameObject 継承の解消**（対象 +1） | ~200 | ~500 | 9 | **1〜1.5 日** | ③ |
| ⑦ | **ColliderComponent の搭載替え**（★縮小） | ~100 | ~350 | 8 | **1 日** | ③ |
| ⑧ | **ParticleEmitterComponent** | ~200 | ~300 | 5 | **1 日** | ①③ |
| ⑨ | **SpriteRenderer / UI の統合** | ~250 | ~400 | 6 | **1 日** | ①③ |
| ⑩ | **CameraComponent**（任意・薄い橋だけ） | ~150 | ~50 | 3 | **0.5 日** | ①③ |
| | **合計** | ~3,550 | ~4,250 | — | **15〜22 日** | |

---

### ① TransformComponent への一元化 — 最重量（3〜5 日）

**なぜ一番重いか**: 4 系統分裂 + `WorldTransform` が**純データではない**（per-object の D3D12 定数バッファ、
`Model::Draw` が GPU アドレスを直接引く）。

**★改訂: 当たり判定完了で ① の担当範囲が 2 つ増えた**
1. **押し出しの受け皿**: `TryApplyCollisionPush(delta)` は現在 `ModelGameObject` が
   `translate += delta` で実装しており、**「親を持つ場合、親の回転・スケール非考慮」という
   Phase 4 時点の制限が明記されている**。TransformComponent なら
   ワールド delta → 親のワールド行列の逆でローカルへ落とす変換を 1 箇所で書けて、この制限が解消する
2. **コライダーの位置ソース**: `Collider::GetWorldCenter()/GetWorldScale()` は
   `owner_->GetWorldPosition()/GetWorldScale()`（GameObject 仮想関数）を引いている。
   ① 完了後は TransformComponent 参照へ差し替え、**純粋仮想 `GetWorldPosition()` を基底から撤去**できる
   （実装義務 10 ファイル分が消える。ただし「Transform を持たないオブジェクトにコライダーを
   付けたらどうなるか」を Add 時の assert で置き換えること — 無音バグ対策の精神は維持する）

**変わるもの**（初版から継続）
- ギズモ / Undo・Redo / インスペクタ / `OnSerialize()` の `"transform"` キー
- `GameObjectDebugAccess.h` が**丸ごと削除**、UIImage・ParticleSystem でもギズモが効くようになる

**実装の分割案（一度にやらない）**
1. `TransformComponent` を `WorldTransform` の**ラッパ**として追加（GPU バッファはそのまま）
2. `ModelGameObject::transform_` を経由アクセスに置換（`GetTransform()` 59 箇所は戻り値型維持で無変更）
3. Sprite / UI / Particle を順に移す（各々独立してテスト可）
4. GPU バッファ分離は**やらなくてもよい**（描画経路まで波及するため）

**リスク**: シリアライズ互換。旧キーの読み取りフォールバック必須（§6-1）。

---

### ② ModelGameObject の分解 — 2 番目に重い（3〜4 日）

**現状 1 クラスに 9 責務**（196 + 520 行）: Transform / Model / Texture / BlendMode /
CustomShader+PSO 構築 / MaterialDebugUI / 視錐台カリング / シリアライズ / **押し出し実装（★新規）**。

**追い風**: `Model::Draw(const WorldTransform&, const DrawViewInfo&, texHandle)` は既に
「レンダラーコンポーネントの中身」の形。所有を継承からメンバへ移すだけ。

**★改訂: タブ分解の前例ができた** — Inspector は現在 5 タブで、5 つ目のコライダータブは
`ColliderInspector::Draw(GameObject&)` という**自由関数への委譲で実装済み**。
Material / Texture / Render タブも同じ形で各コンポーネントへ切り出せばよい（発明不要）。

**分解先**
| コンポーネント | 中身 |
|---|---|
| `MeshRendererComponent` | `Model` 所有 + BlendMode + 視錐台カリング + `GetWorldBoundingBox()` |
| `MaterialComponent` | Texture ハンドル + MaterialInstance 操作 + MaterialDebugUI |
| `CustomShaderComponent` | `ICustomShaderProvider` + `CustomShaderPipeline`（利用オブジェクトは少数） |

**変わる場所**: TLAS 構築（`RayTracingSubsystem.cpp:66`）・IBL 一括適用・ピッキング・
中間基底 3 つ・App 側 7 クラス。

**リスク**: Hi-Z の `SubmitBounds` 毎フレーム継続、RT シャドウの半透明除外条件（§6）。
ピッキングは Phase 2 で `Math/Geometry` 委譲済みだが、**ローカル AABB 事前棄却 → 三角形判定の
構造（`ObjectSelector.cpp:361-404`）を崩さない**こと。

---

### ③ コンポーネント基盤の新設（2〜3 日）

**新規に書くもの**
```
Engine/Src/GameObject/Component/
  IComponent.h            Awake/Start/Update/LateUpdate/OnDestroy/OnSerialize/OnDeserialize
  ComponentHost.h/.cpp    AddComponent<T>(args...) / GetComponent<T>() / GetComponents<T>()
  ComponentRegistry.h     型名 ↔ ファクトリ（④のプレハブで使う。ここでは器だけ）
```

**★手本が実戦投入済みになった**: 参照安定性（個別ヒープ確保）と遅延解放（`retired_` 墓場 +
フレーム末 `ReleaseRetired()`）は `ColliderComponent` が PASS 29 のスイートの下で実証済み。
**同じパターンをそのまま一般化する**（発明しない）。

**更新順序**: 既存の `SceneUpdatePhase`（PreObjectUpdate / PostObjectUpdate / PostLogic）に従属させる。
コリジョンが PostObjectUpdate で走る前提（判定はオブジェクト更新後）を崩さない。

**横断イテレート**: `GameObjectManager` に `型 → コンポーネント配列` のインデックスを追加。
**最初の顧客は ⑦**: `RegisterAllColliders(CollisionWorld*)` の中身が
`ForEach<ColliderComponent>` に置き換わるのが自然な搭載口になる。

**§2.3 の命名衝突をここで解決する。**

---

### ④ データ駆動シーン・プレハブ（2〜3 日・任意・最後）

型レジストリ／ファクトリは依然として存在せず、エディタから作れるのは
`DynamicModelObject` の複製だけ（`SceneDebugEditor.cpp:484,536`）。
`{"components":[...]}` 形式でオブジェクトを丸ごと復元できるようにする。

**★改訂の注意**: Phase 6 は**コリジョンマトリクスを意図的に JSON 永続化しなかった**
（「シーンのコードが正、パネルは実行中の調整用」という設計判断）。④でコライダーの
レイヤーをプレハブに保存する場合、**この判断と矛盾しない線引き**
（コライダー形状・レイヤーは保存する / レイヤー間マトリクスはシーンコードのまま）を明記すること。

---

### ⑤ AnimatedModelObject の分解（1.5〜2 日）

初版から変更なし。`AnimationPlayer` は `Model` 所有で分離済み。残りは
`AnimatorComponent`（クリップ登録・切替・ブレンド）と `SkeletonSocketComponent`
（`WeaponObject` の生ポインタ追従 + `WalkModelObject` の手パーティクルを吸収）。
Mixamo リグの cm 単位（スケール 0.01）補正を落とすと武器が消える（既知）。

---

### ⑥ エンジン内部の GameObject 継承の解消（1〜1.5 日・対象 +1）

**★`ColliderDebugRenderer` が仲間入りし、対象は 6 クラスになった**:
`LineDrawable`(70) / `GridRenderer`(109) / **`ColliderDebugRenderer`(133)** /
`WaterPlaneObject`(208) / `InfiniteGroundObject` / (App)`SkyBoxObject`。

Phase 6 のドキュメント自体が「`GridRenderer` と同じ流儀」と明記しており、
**Line パスへ AddLines する GameObject という同型ボイラープレートが 3 つ**になっている。
方針:
- **デバッグ描画物**（LineDrawable / GridRenderer / ColliderDebugRenderer）→ GameObject を降りて
  RenderFeature 直接管理の `IDebugDrawable` へ集約（`CollisionFeature` が
  `gameObjectManager->AddObject` している生成箇所も同時に差し替え）
- **シーン環境物**（SkyBox / Ground / WaterPlane）→ `SkyBoxComponent` / `WaterSurfaceComponent` 化で
  `EnvironmentFeature.cpp:116,135` / `WaterRenderFeature.cpp:86,207` の dynamic_cast 4 箇所を解消

**リスク**: 反射ビューで水面自身を描かない条件の維持（§6-5）。
`ColliderDebugRenderer` は `SetSerializeEnabled(false)` 前提 — 移設後もシーン保存に混ぜない。

---

### ⑦ ColliderComponent の搭載替え（1 日・★大幅縮小）

**初版からの変化**: 初版は「Collision Phase 4/5 と同時にやらないと二重工事」だったが、
**Phase 3〜6 がすべて完了**した。コライダーの複数化・接触情報・押し出し・ID 化・BroadPhase・
クエリ API・デバッグ描画・Inspector・マトリクス UI は**もう存在する**。
`Collision/` コアは開けない。残っているのは GameObject 側の縫い目（§3.5）だけ:

1. **固定メンバ → 搭載式へ**: `protected: ColliderComponent colliders_;` を
   `AddComponent<ColliderComponent>()` に置き換える。移行中は基底の委譲 API 9 本を
   「無ければ作って転送する」薄いラッパとして残し、呼び出し側（テストシーン含む）を壊さない
2. **収集経路の置換**: `GameObjectManager::RegisterAllColliders(CollisionWorld*)` を
   コンポーネント横断イテレーションへ（③の最初の実証）。
   **毎フレーム `ClearColliders()` → 登録 → `Step()` のモデルは変えない**
   （履歴は ID キーなので登録し直しても Enter/Stay/Exit は正しく出る設計）
3. **イベント経路の維持**: `Collider` → owner の GameObject 仮想関数という現行経路を
   ColliderComponent 経由に変え、**`GameObject*` 版・`CollisionInfo` 版の両シグネチャと
   「Info 版既定実装が旧版へ転送する」互換を保つ**
4. **位置ソースは触らない**（① 完了後に TransformComponent 参照へ差し替え）

**検証**: `CollisionTestScene` の **PASS 29 / FAIL 0** が同数で通ること（ExitCode 必須確認）＋
`r.Collision.DebugDraw` のワイヤ表示と状態色分けが出ること。

---

### ⑧ ParticleEmitterComponent（1 日）

変更なし。`ParticleSystem` / `GpuParticleSystem` の多重継承解消。
GameObject 接点は 2 ヘッダ（計 768 行）だけで、モジュール層 5,100 行は無関係。

---

### ⑨ SpriteRenderer / UI の統合（1 日）

変更なし。`SpriteObject`(238+494) と `UIImage`(142+293) を ① 完了後にコンポーネント化。
`ObjectSelector.cpp:212` / `CanvasViewport.cpp:202` の dynamic_cast が消える。

---

### ⑩ CameraComponent（0.5 日・任意）

変更なし。`Camera` は `final` + Controller 分離済みで、TransformComponent の値を流す薄い橋のみ。

---

## 5. 推奨実行順（改訂 — 理由がより強くなった）

```
③ 基盤 ＋ 命名方針の決定           （2〜3 日）  ← ColliderComponent のパターンを一般化するだけ
  ↓
⑦ Collider 搭載替え                （1 日）     ← PASS 29 スイート + デバッグ描画 + Inspector が
  ↓                                              全部揃った「最良の実証場」で基盤を検証する
① Transform（4 段階分割）          （3〜5 日）  ← 押し出しの親対応と純粋仮想撤去もここ
  ↓
② MeshRenderer / Material          （3〜4 日）
  ↓
⑤ Animator / SkeletonSocket        （1.5〜2 日）
  ↓
⑧ ⑨ ⑥（独立・並行可）              （3〜4 日）
  ↓
⑩ ④（任意）
```

**⑦ を 2 番目に置く理由（初版より強化）**: 初版時点の検証手段は PASS/FAIL ログだけだったが、
現在は **回帰 29 件 + ワイヤ表示の目視 + Inspector からの実地操作**が揃っており、
基盤の破綻を最速で検出できる。しかも作業量が 1 日に縮んだので、失敗してもやり直しが安い。

**最小で止めても価値が出る地点**: ③ + ⑦ + ① まで（**6〜9 日**）。
この時点で `GameObjectDebugAccess.h` が消え、UI/Particle にギズモが効き、
`GetWorldPosition()` の実装義務（10 ファイル）と押し出しの親制限が消える。

---

## 6. 不変条件と罠（改訂）

| # | 内容 |
|---|---|
| 1 | **シリアライズ互換**。`Application/Assets/Scenes/*/*.json` と `Saved/EditorSettings/` の旧キーに読み取りフォールバックを残す |
| 2 | **ODR 事故**。`GameObject` / `Collider` などサイズが変わるコミットは**クリーンビルド必須**（増分中断でヒープ破損 `c0000374`、VS は通るのにコマンドライン MSBuild で落ちる前例） |
| 3 | **`WorldTransform` は純データではない**（GPU 定数バッファ持ち）。POD 化を目標にしない |
| 4 | **Hi-Z カリング**: `SubmitBounds` 毎フレーム継続・GameView 限定を維持 |
| 5 | **反射ビューでの水面**: `WaterPlaneObject` の登録経路変更時、水面自身を反射に描かない条件を維持（夜の明暗斑の既知原因） |
| 6 | **RT シャドウの TLAS**: 半透明除外（`GetBlendMode() != kBlendModeNone`）を落とさない |
| 7 | **`GetComponent` の意味衝突**（§2.3）。着手前に決める |
| 8 | **ピッキング構造**: ローカル AABB 事前棄却 → `Geometry::RaycastTriangle` の順序を維持（崩すと連打 48fps へ退行） |
| 9 | **GameDebugUI に登録解除 API が無い**。シーン寿命のポインタをラムダに持たせない。解法は `CollisionMatrixPanel` の file-scope `s_activeConfig` 方式を踏襲 |
| 10 | **`vcxproj` 手動登録**。`SyncFilters` はファイル追加しない仕様。新規ファイルは毎回手で足す |
| 11 | ★**コリジョンの収集モデル**: 毎フレーム `ClearColliders()` → 全登録 → `Step()`。履歴は**再利用されない一意 ID** がキー（ABA 構造排除）。コンポーネント化で Collider の生成経路を変えても ID 採番を壊さない |
| 12 | ★**シーン終了順**: `CollisionFeature::Finalize` が **GameObject より先に** World をクリアする（生ポインタ履歴を残さない）。コンポーネント破棄順でもこの順序を維持 |
| 13 | ★**クエリの鮮度**: `Raycast` / `Overlap*` は**直近 `Step()` 時点**のスナップショット。OnUpdate から呼ぶと 1 フレーム前。コンポーネントの Update から呼ぶ場合も同じ制約を文書化して引き継ぐ |
| 14 | ★**イベント互換**: `CollisionInfo` 版の既定実装が `GameObject*` 版へ転送する契約を維持（両方 override 時は明示的に基底を呼ぶ運用込み） |
| 15 | ★**マトリクス非永続の設計判断**: レイヤー間マトリクスはシーンコードが正で JSON 保存しない。④のプレハブ設計はこれと矛盾しない線引きを引く |

---

## 7. 検証方法（改訂）

| 対象 | 手段 |
|---|---|
| ⑦ Collider | `CollisionTestScene` の **PASS 29 / FAIL 0** 維持（**ExitCode を必ず見る** — 見ないとクラッシュを見逃す）＋ `r.Collision.DebugDraw` のワイヤと状態色分け（接触=赤/トリガー=青）＋ Inspector のコライダータブから形状編集が効くこと |
| ① Transform | 各シーン起動 → JSON 復元後の見た目一致。ギズモ→保存→再起動の往復。押し出し挙動（`CollisionTestScene` の押し出しテスト）が変わらないこと |
| ② MeshRenderer | RT シャドウ有無・Hi-Z 棄却数・ピッキング連打時 FPS（ステータスバー読み取り） |
| ⑤ Animator | `AssignmentScene`（武器ジョイント追従・手パーティクル）の破綻なし |
| ⑥ 環境物 | 昼夜での水面・スカイボックス・無限床。コライダーワイヤ表示が引き続き出ること |
| 全体 | 各シーン巡回 + `EngineStatsWindow` で GBuffer 時間の退行チェック |

---

## 8. まとめ（改訂）

- 当たり判定リファクタリング完了により、**⑦ は「作る」から「搭載し替える」へ縮小**（合計 15〜22 日）
- 引き換えに GameObject 基底は virtual 26 個まで肥大し、**座標系の違う派生にまで実装義務が
  拡散した（証拠④）**。コンポーネント化の必要性はむしろ強まった
- ③ の基盤は発明不要 — **PASS 29 で実証済みの `ColliderComponent` パターンを一般化するだけ**
- 実行順 ③ → ⑦ → ① → ② は不変。**6〜9 日で主な痛みが消える**地点も不変
- `Collision/` コア・Graphics・Math は完成した安定資産として**開けない**

---

## 付録A: GameObject 基底のビフォーアフター

### A.1 現状の API 表面（Before: 376 行 / virtual 26 本 / コライダー委譲 9 本 / メンバ 11 個）

`GameObject.h` の全 API を役割で分類すると、**6 つの無関係な役割が 1 クラスに同居**している。

| 役割 | API | virtual |
|---|---|---:|
| ライフサイクル | `Initialize` / `Update` / `Draw(Camera*)` / `Draw(DrawViewInfo&)` | 4 |
| 描画制御 | `GetRenderPassType` / `GetBlendMode` / `SetBlendMode` / `BuildRenderItem`（+ RenderOrder 3 本） | 4 |
| 衝突イベント | `OnCollisionEnter/Stay/Exit` × **2 系統**（`GameObject*` 版 / `CollisionInfo` 版） | 6 |
| 衝突の物理 | `TryApplyCollisionPush` / `GetWorldPosition`（**純粋仮想**）/ `GetWorldScale` | 3 |
| コライダー所有 | `GetColliders`×2 / `AddSphereCollider` / `AddAABBCollider` / `HasCollider` / `GetCollider`×2 / `RemoveCollider` / `ReleaseRetiredColliders` + `colliders_` メンバ | 0 |
| シリアライズ | `OnSerialize` / `OnDeserialize` / `GetObjectName` | 3 |
| インスペクタ | `DrawImGui` / `DrawImGuiExtended` / `GetInspectorTabs` / `DrawInspectorTabContent` / `OnImGuiActiveChanged` | 5 |
| その他 | デストラクタ / 名前 / アクティブ / 破棄 / `Spawn<T>` / エンジン参照 | 1 |

**問題の構造**: SkyBox を書く人も UI を書く人も、この 26 本全部が「自分に関係あるかもしれない API」
として見える。しかも `GetWorldPosition` は純粋仮想なので、**座標を持たないオブジェクトにまで実装を
強制する**（UIImage は「3D の当たり判定に使う想定は無い」とコメントで自衛している）。

### A.2 改修後の GameObject.h（After: 約 130〜150 行 / virtual 0 / 純粋仮想 0）

```cpp
namespace CoreEngine
{
    /// @brief コンポーネントの入れ物 + アイデンティティ。継承しない。
    class GameObject final {
    public:
        // ===== アイデンティティ =====
        void SetName(const std::string& name);
        const std::string& GetName() const;
        const std::string& GetSerializeKey() const;

        // ===== アクティブ / 破棄 =====
        void SetActive(bool active);
        bool IsActive() const;
        void Destroy();                    // 実体破棄はフレーム末（現行と同じ）
        bool IsMarkedForDestroy() const;

        // ===== コンポーネント =====
        // 参照安定（個別ヒープ確保）+ 遅延解放（retired_ 墓場）は
        // ColliderComponent で実証済みのパターンをそのまま使う。
        template<typename T, typename... Args>
        T* AddComponent(Args&&... args);
        template<typename T> T*       GetComponent();
        template<typename T> const T* GetComponent() const;
        template<typename T> std::vector<T*> GetComponents();   // 同型複数
        void RemoveComponent(IComponent* component);             // 解放はフレーム末

        // ===== シリアライズ（virtual ではなく全コンポーネント巡回） =====
        json Serialize() const;            // {"components":[{"type":"Transform",...},...]}
        void Deserialize(const json& j);

        // ===== スポーン（現行の Spawn<T> と同じ立ち位置） =====
        GameObject* SpawnEmpty(const std::string& name);

    private:
        std::string name_;
        std::string serializeKey_;
        bool isActive_          = true;
        bool markedForDestroy_  = false;
        bool shouldSerialize_   = true;

        std::vector<std::unique_ptr<IComponent>> components_;
        std::vector<std::unique_ptr<IComponent>> retired_;
        IObjectSpawner* spawner_ = nullptr;
        friend class GameObjectManager;
    };
}
```

**virtual 26 本 → 0 本**。`final` にできるので vtable すら不要になる。
機能が消えるのではなく、**各コンポーネントの狭いインターフェースへ再配置**される:

| 現在の virtual（26 本） | 本数 | 行き先 |
|---|---:|---|
| `Initialize` / `Update` / `Draw`×2 | 4 | `IComponent::Awake/Start/Update`、描画は `MeshRendererComponent::Draw`（RenderManager がコンポーネント索引を直接巡回） |
| `GetRenderPassType` / `GetBlendMode` / `SetBlendMode` / `BuildRenderItem` | 4 | 描画系コンポーネントが**ただのデータ**として所持（virtual 不要になる） |
| `OnCollisionEnter/Stay/Exit` ×2系統 | 6 | `ColliderComponent::SetOnEnter/Stay/Exit(callback)`（`CollisionInfo` 1 系統に統一） |
| `TryApplyCollisionPush` | 1 | `TransformComponent`（CollisionWorld が Transform を直接押す） |
| `GetWorldPosition`（純粋仮想）/ `GetWorldScale` | 2 | `TransformComponent`。**実装義務そのものが消滅**（Transform の無いオブジェクトへの Collider 追加は Add 時 assert で弾く＝無音バグ対策の精神は維持） |
| `GetObjectName` | 1 | ComponentRegistry の型名（ただのデータ） |
| `OnSerialize` / `OnDeserialize` | 2 | `IComponent::OnSerialize/OnDeserialize`（コンポーネント単位） |
| `DrawImGui` / `DrawImGuiExtended` / `GetInspectorTabs` / `DrawInspectorTabContent` / `OnImGuiActiveChanged` | 5 | `IComponent::DrawInspector`（**1 コンポーネント = 1 タブ**を自動生成。`ColliderInspector::Draw(GameObject&)` の外出しが前例） |
| デストラクタ | 1 | 不要（`final` 化） |

### A.3 受け皿となる IComponent（新設・virtual 8 本だが**各コンポーネントは使う分だけ override**）

```cpp
class IComponent {
public:
    virtual ~IComponent() = default;

    virtual void Awake() {}                    // AddComponent 直後
    virtual void Start() {}                    // 最初の Update 前（他コンポーネント参照はここから）
    virtual void Update() {}                   // SceneUpdatePhase::ObjectUpdate 内
    virtual void OnDestroy() {}

    virtual json OnSerialize() const { return {}; }
    virtual void OnDeserialize(const json&) {}

#ifdef USE_IMGUI
    virtual const char* GetInspectorName() const = 0;   // タブ名
    virtual bool DrawInspector() { return false; }
#endif

    GameObject* GetOwner() const { return owner_; }
    template<typename T> T* Sibling() { return owner_->GetComponent<T>(); }

private:
    GameObject* owner_ = nullptr;
    friend class GameObject;
};
```

---

## 付録B: 派生クラス全 30 個の行き先

### B.1 行き先マップ

**分類A: クラス消滅 → データ化（12 クラス）** — 「アセットパスを返すだけのクラス」は
生成コード 3 行（またはプレハブ JSON 1 個）になる。

| 現在のクラス | 置き換え |
|---|---|
| `ModelObject` / `PlaneModelObject` / `SphereObject` | `Transform + MeshRenderer(パス)` |
| `DynamicModelObject` | 同上（エディタ D&D は同じ組を生成するだけ） |
| `WalkModelObject` / `FoxObject` / `BrainStemObject` | `Transform + MeshRenderer + Animator(クリップ表)` |
| `CubeObject` / `CylinderObject` / `PlaneObject` / `RingObject` / `PrimitiveSphereObject` | `Transform + PrimitiveMesh(形状パラメータ)` |

**分類B: コンポーネントへ変換（振る舞い持ち・10 クラス相当）**

| 現在のクラス | 変換先コンポーネント | 備考 |
|---|---|---|
| `WeaponObject` | `SkeletonSocketComponent` | 生ポインタ追従と生成順の罠が消える（証拠②） |
| `WalkModelObject` の手パーティクル | `SkeletonSocketComponent` + `ParticleEmitterComponent` | `SetHandParticleSystem` 専用 API が不要に |
| `ModelProbe<Base>`(CRTP) / `HeadlessProbe` / `SphereProbe` / `BoxProbe` | `ProbeComponent` | CRTP 解消の本丸（証拠①） |
| `SkyBoxObject` | `SkyBoxComponent` | `EnvironmentFeature` / `WaterRenderFeature` の dynamic_cast 2 箇所解消 |
| `WaterPlaneObject` | `WaterSurfaceComponent` | 反射ビュー条件は維持（§6-5） |
| `InfiniteGroundObject` | `InfiniteGroundComponent` | |
| `SpriteObject` | `SpriteRendererComponent` | |
| `UIImage` | `UIImageComponent`（UILayout 持ち） | |
| `ParticleSystem` / `GpuParticleSystem` | `ParticleEmitterComponent`（backend 選択） | 多重継承解消（証拠③） |

**分類C: GameObject から降りる（エンジン内部・3 クラス）**

| 現在のクラス | 行き先 |
|---|---|
| `LineDrawable` / `GridRenderer` / `ColliderDebugRenderer` | RenderFeature 直接管理の `IDebugDrawable`。GameObjectManager を経由しない |

**分類D: 中間基底 → 移行用の互換シム → 最終削除（4 クラス）**

| 現在のクラス | 移行中の姿 | 最終 |
|---|---|---|
| `ModelGameObject` | `Transform + MeshRenderer + Material` を内蔵し、`GetTransform()` 等の従来 API をコンポーネントへ転送するシム | 削除 |
| `AnimatedModelObject` | + `Animator` を内蔵するシム | 削除 |
| `PrimitiveGameObject` | + `PrimitiveMesh` を内蔵するシム | 削除 |
| `SpriteObject`（基底として見た場合） | `SpriteRenderer` 転送シム | 削除 |

シム方式により **App 側 16 クラスと `CreateObject<T>()` 15 箇所は移行中もコンパイルが通り続け**、
書き換えは最後にまとめて 1 コミットで行える（ODR 対策のクリーンビルドもその 1 回で済む）。

### B.2 使う側コードのビフォーアフター（実コード）

**例1: 汎用モデル（`TestScene.cpp:85` 現物）**

```cpp
// ===== Before: アセット1種につきクラス1個（ModelObject.h 55行 + .cpp） =====
class ModelObject : public CoreEngine::ModelGameObject {
public:
    explicit ModelObject(const std::string& modelPath = "") : modelPath_(modelPath) {}
    const char* GetObjectName() const override { return "Model"; }
    void SetPBRParameters(float metallic, float roughness, float ao = 1.0f);
    // ...
protected:
    std::string GetModelPath() const override { return modelPath_; }
};
auto sphere = CreateObject<ModelObject>("sphere.obj");
sphere->GetTransform().translate = { 0.0f, 1.0f, 0.0f };

// ===== After: クラス定義が不要（ModelObject.h/.cpp 削除） =====
auto* sphere = CreateObject("Sphere");
sphere->AddComponent<TransformComponent>()->translate = { 0.0f, 1.0f, 0.0f };
sphere->AddComponent<MeshRendererComponent>("sphere.obj");
// SetPBRParameters 相当は MaterialComponent へ:
sphere->GetComponent<MaterialComponent>()->SetPBR(0.9f, 0.2f);
```

**例2: 武器のジョイント追従（`AssignmentScene.cpp:70` 現物）**

```cpp
// ===== Before: 生成順コメントが必要（罠がコードの外にある） =====
// キャラクターより後に生成することで、Update 順が
//   キャラクター（スケルトン更新） → 武器（ジョイント追従）になり、追従が 1 フレーム遅れない。
weapon_ = CreateObject<WeaponObject>();          // WeaponObject.h/.cpp 約120行のクラスが必要
weapon_->AttachToJoint(character_, kRightHandJointName);
weapon_->SetSocketOffset({ 0.0f, 0.32f, 0.0f }, { 0.0f, 0.0f, 0.0f });

// ===== After: クラス不要 + 生成順不問（Socket が Animator 更新後に評価される） =====
auto* weapon = CreateObject("Weapon");
weapon->AddComponent<TransformComponent>();
weapon->AddComponent<PrimitiveMeshComponent>(MakeCylinderGenerator(/*刃の形状*/));
auto* socket = weapon->AddComponent<SkeletonSocketComponent>();
socket->Attach(character->GetComponent<AnimatorComponent>(), kRightHandJointName);
socket->SetOffset({ 0.0f, 0.32f, 0.0f }, { 0.0f, 0.0f, 0.0f });
```

**例3: 衝突カウントプローブ（CRTP の解消・`CollisionProbeObject.h:45` 現物）**

```cpp
// ===== Before: 見た目クラスごとにテンプレート実体化 =====
template <class Base>
class ModelProbe : public Base {
    static_assert(std::is_base_of_v<CoreEngine::ModelGameObject, Base>, ...);
    void OnCollisionEnter(CoreEngine::GameObject* other) override { /* カウント+色替え */ }
};
using SphereProbe = ModelProbe<PrimitiveSphereObject>;
using BoxProbe    = ModelProbe<CubeObject>;

// ===== After: どの見た目にも同じ 1 行で付く =====
class ProbeComponent : public IComponent {
    void Start() override {
        Sibling<ColliderComponent>()->SetOnEnter(
            [this](const CollisionInfo& info) { stats_.enter++; ApplyColor(); });
    }
};
sphereObj->AddComponent<ProbeComponent>();
cubeObj->AddComponent<ProbeComponent>();
```

### B.3 クラス数の収支

| | Before | After |
|---|---:|---:|
| `GameObject` 派生クラス | 30（基底 1 + 中間基底 4 + 具象 25） | **1（final の入れ物のみ）** |
| コンポーネント | 1（ColliderComponent） | **約 14** |
| 基底の virtual | 26 | **0**（IComponent に 8、各コンポーネントは必要分のみ） |
| アセット追加時に書くもの | クラス 1 個（.h/.cpp） | **生成 3 行 or プレハブ JSON** |

クラスの総数はほぼ同じだが、**深さ 4 の継承ツリー**（どの層に何があるか探す必要がある）が
**平らなコンポーネント 14 個**（名前 = 機能）に変わる。

---

## 9. 実施結果（2026-08-07）

実装順は §5 の推奨どおり **③ → ⑦ → ① → ② → ⑤ → ⑧⑨**。
各ステップで「ビルド成功 → 起動 → 描画確認 → `CollisionTestScene` の PASS 29 / FAIL 0」を確認した。

### 9.0 事前準備: 命名衝突の解決（`GetService`）

計画（§2.3）では `EngineSystem::GetComponent<T>()` を `GetSubsystem<T>()` へ改名する案 A を
推していたが、**`GetSubsystem<T>()` は既に別用途で存在していた**
（`IEngineSubsystem` 派生を `dynamic_cast` の線形走査で引く。`componentManager_` とは別コンテナ）。
同名・同シグネチャのテンプレートが 2 つ並んで即コンパイルエラーになったため、
**`GetService<T>()` / `HasService<T>()` に変更**した（106 箇所 / 36 ファイルを機械置換）。

| 名前 | 引くもの | 実装 |
|---|---|---|
| `EngineSystem::GetService<T>()` | 常駐サービス（DirectXCommon, TextureManager…） | `type_index` マップ O(1) |
| `EngineSystem::GetSubsystem<T>()` | `IEngineSubsystem` 派生（Debug, RayTracing） | 線形 `dynamic_cast`（既存） |
| `GameObject::GetComponent<T>()` | このオブジェクトの機能単位 | 線形 `dynamic_cast` |

### 9.1 ③ コンポーネント基盤

新設: `Engine/Src/GameObject/Component/IComponent.h` / `ComponentHost.h` / `ComponentHost.cpp`。
`GameObject` が `ComponentHost` を継承する形にした。

**計画からの逸脱 2 点**

1. **型別インデックスを作らなかった**。`GameObjectManager::ForEachComponent<T>()` は
   オブジェクト × コンポーネントの線形走査。従来のダウンキャストループ
   （オブジェクト × 1）と同じオーダーで係数がコンポーネント数（実測 1〜5）倍になるだけ。
   追加・削除・破棄でインデックスを同期する必要がある実装はバグ源になるので、
   **実測で問題になるまで作らない**方針にした。
2. **問い合わせ系の `static_assert` を外した**。`GetComponent<ITransformSource>()` のように
   **`IComponent` を継承していないミックスインのインターフェース**を引きたいケースがある
   （`IComponent` が多態なので兄弟インターフェースへのクロスキャストが成立する）。
   制約が残っているのは実体を作る `AddComponent` / `GetOrAddComponent` だけ。

**イテレーション中の着脱**: 取り外しは配列から要素を消さず**スロットを nullptr にする**
（インデックスをずらさない）。詰め直しは `ReleaseRetiredComponents()` がフレーム末に行う。

### 9.2 ⑦ ColliderComponent の搭載替え

`ColliderComponent` を `IComponent` 派生にし、`GameObject` の固定メンバを撤去。
`SetOwner()` は廃止（`IComponent::GetOwner()` から引く）。

**新たに分かった罠: オンデマンド生成の副作用**
`GetColliders()` を「無ければ生成する」にしたところ、
- 毎フレームの収集ループ（`RegisterAllColliders`）
- インスペクタのコライダータブ表示

がそれぞれ**全オブジェクトに空の ColliderComponent を生やしてしまう**ことが判明。
**読み取り用 `TryGetColliders()`（生成しない）と生成用 `GetColliders()` を分離**して解決した。
問い合わせ系（`HasCollider` / `GetCollider` / `RemoveCollider` / `ReleaseRetiredColliders`）と
インスペクタの表示は前者、`AddSphereCollider` / `AddAABBCollider` と「追加」ボタンは後者を使う。

`RegisterAllColliders` は `ForEachComponent<ColliderComponent>` に置換。
副産物として、コライダーを持たないオブジェクト（スカイボックス・UI・デバッグ線）の
毎フレーム空振りが消えた。

### 9.3 ① TransformComponent

`TransformComponent` は `WorldTransform` を**内包するラッパ**にした（計画どおり GPU 定数バッファは剥がさない）。

**移行の要: 参照束縛**
`ModelGameObject` のメンバを「コンポーネントが持つ実体への参照」にすることで、
派生クラス・呼び出し側の `transform_.xxx` / `GetTransform().xxx` を **1 行も変えずに**
実体だけコンポーネント側へ移せた。

```cpp
ModelGameObject()
    : transformComponent_(AddComponent<TransformComponent>())
    , meshRenderer_(AddComponent<MeshRendererComponent>())
    , transform_(transformComponent_->Get())
    , model_(meshRenderer_->ModelPtr())
    , texture_(meshRenderer_->TextureRef())
    , textureName_(meshRenderer_->TextureNameRef())
    , blendMode_(meshRenderer_->BlendModeRef()) {}
```

参照が安定なのは `ComponentHost` がコンポーネントを個別ヒープへ確保しているため。
**この 2 つのコンポーネントは取り外してはいけない**（参照が宙に浮く）。

**押し出しの親対応を実装**: `TransformComponent::ApplyWorldDelta()` が、親を持つ場合に
ワールド量 delta を親のワールド行列の逆行列で方向変換してからローカル translate へ足す。
Phase 4 時点の「親の回転・スケールを無視」という制限が解消した。
（`TransformNormal` は `MathCore::CoordinateTransform` 名前空間。`Matrix` ではない）

**`ITransformSource` の導入（計画に無かった判断）**
計画は「Sprite / UI / Particle も `TransformComponent` へ移行」としていたが、実装時に
**それが誤ったモデリングだと判明**した:

- `SpriteObject` のスケールはテクスチャ寸法との積で意味が決まる
- `UIImage` はアンカー相対のスクリーン座標（`Vector2`）で、3D の行列階層ではない
- `ParticleSystem` はエミッタの発生位置

`WorldTransform`（親子階層 + per-object GPU 定数バッファ）に押し込むと
「使わない定数バッファを毎フレーム更新する」無駄と「座標系の意味が違うものを同じ型で扱う」
危険が生まれる。そこで **共通化するのは編集インターフェースだけ**にした:

```
ITransformSource（Translate()/Rotate()/Scale() の参照を返すだけ）
├─ TransformComponent      … WorldTransform 内包（3D モデル。GPU 定数バッファつき）
└─ EulerTransformComponent … EulerTransform 内包（スプライト・パーティクル。軽量）
```

**純粋仮想 `GetWorldPosition()` の撤去は見送った**。撤去には「位置の読み手」全員
（ピッキング・コライダー・RT）がコンポーネント経由になる必要があり、かつ
`HeadlessProbe` のように `TransformComponent` を持たず独自に位置を返す派生も存在する。
基底にデフォルト実装（`{}` を返す）を置くと**「実装忘れで全員が原点に重なる無音バグ」が復活する**
——これは当たり判定リファクタリングで意図的に純粋仮想化して潰したもの。
実装義務が消える見返り（見た目のコード量）より、無音バグの防止を優先した。

### 9.4 ② MeshRendererComponent

**計画からの逸脱: 3 分割せず 1 コンポーネントにした**。
`MeshRenderer` / `Material` / `CustomShader` に割る想定だったが、
`Material` の実体は `Model` が持つ `MaterialInstance` であり、コンポーネントに残るのは
「上書き用テクスチャハンドル 1 個」だけになる。`CustomShader` も利用者が数個。
**2 人目の利用者が現れるまで割らない**（用も無く分割して階層を深くしたのが元の問題なので、
同じ失敗をコンポーネントで繰り返さない）。

`dynamic_cast<ModelGameObject*>` を 6 箇所で撤去し、`AsModelObject()` 自体を削除:

| 箇所 | 置き換え後 |
|---|---|
| `RayTracingSubsystem.cpp`（TLAS 構築） | `ForEachComponent<MeshRendererComponent>` |
| `DebugSubsystem.cpp` ×2（IBL 一括／個別） | 同上 |
| `SceneDebugEditor.cpp`（コピー可否判定） | `HasComponent<MeshRendererComponent>()` |
| `Gizmo.cpp` | `GetComponent<ITransformSource>()` |
| `ObjectSelector.cpp`（ピッキング） | `GetComponent<MeshRendererComponent>()` + `TransformComponent` |

ピッキングの「ローカル AABB 事前棄却 → 三角形判定」の構造は維持した（不変条件 §6-8）。

### 9.5 ⑤ Animator / SkeletonSocket

**更新順序を 2 パスに変更（計画に無かった重要な設計変更）**

`GameObjectManager::UpdateAll()` を

```
[パス1] 全オブジェクト: Start → コンポーネント Update → GameObject::Update
[パス2] 全オブジェクト: コンポーネント LateUpdate
```

に分けた。1 オブジェクトずつ「Update → LateUpdate」と回すと、LateUpdate から
他オブジェクトを参照したとき相手がまだ Update されていない可能性が残り、
**生成順への依存が消えない**。

これにより `SkeletonSocketComponent`（`LateUpdate` で動く）は追従元の生成順に依存しなくなった。
**実測で確認**: `AssignmentScene` で武器をキャラクターより**先に**生成しても
（Hierarchy が `Weapon_0` → `WalkModel_0` の順）武器は正しく手に追従した。
旧実装のヘッダにあった「追従元キャラクターより後に生成すること」という注意書きは削除。

`WeaponObject` は 120 行 → 25 行になり、生ポインタ `owner_` と `OnUpdate()` の
行列上書きが消えて「刃のメッシュを作るだけ」のクラスになった。

### 9.6 ⑧⑨ Sprite / Particle

`SpriteObject` / `ParticleSystem` / `GpuParticleSystem` に `EulerTransformComponent` を
参照束縛で導入。`Gizmo::Manipulate` を `ITransformSource` ベースに書き換えた
（`TransformComponent` があれば階層込みのワールド行列を使い、無ければ SRT から組む）。

**成果: ギズモが効く対象が広がった**。以前は `GameObjectDebugAccess` の
`dynamic_cast` 分岐に載っている型（Model / Sprite）だけがギズモ対象で、
**`ParticleSystem` / `GpuParticleSystem` は無言で効かなかった**。実測で
HandParticles（GPU パーティクル）を選択してギズモが手の位置に出ることを確認した。

**`UIImage` は意図的に対象外**。`UILayout` はアンカー・ピボット・サイズ（すべて `Vector2`）で
位置が決まり、3D の translate/rotate/scale（`Vector3`）とは意味論が違う。
無理に載せると `Vector2` ↔ `Vector3` のミラーを同期し続ける必要があり同期漏れバグの温床になる。
UI の編集は専用の `CanvasViewport`（2D エディタ）が担当する。

`Editor/ImGui/GameObjectDebugAccess.h` は **`dynamic_cast` ゼロ**になった
（`TryGetTransformAccess` が `GetComponent<ITransformSource>()` 1 本になり、
`AsModelObject()` は削除）。

### 9.7 実測: dynamic_cast の削減

| | Before | After |
|---|---:|---:|
| GameObject 系 `dynamic_cast`（コード実体） | 13 | **6** |
| 撤去した 7 箇所 | — | RayTracingSubsystem / DebugSubsystem×2 / SceneDebugEditor / Gizmo / GameObjectDebugAccess×2 |
| 残る 6 箇所 | — | `EnvironmentFeature`×2・`WaterRenderFeature`×2（**⑥の対象**）、`CanvasViewport`（UI 専用経路）、`ObjectSelector`（2D スプライト選択の専用経路） |

### 9.8 新設ファイル一覧

```
Engine/Src/GameObject/Component/
  IComponent.h                   ライフサイクル・シリアライズ・インスペクタの契約
  ComponentHost.h / .cpp         参照安定 + 遅延解放のコンテナ（GameObject が継承）
  ITransformSource.h             編集可能な translate/rotate/scale の共通インターフェース
  TransformComponent.h / .cpp    WorldTransform 内包（3D モデル用・押し出しの親対応）
  EulerTransformComponent.h      EulerTransform 内包（スプライト・パーティクル用）
  MeshRendererComponent.h / .cpp Model 所有 + BlendMode + カリング + AABB
  AnimatorComponent.h / .cpp     クリップ切替・ジョイント行列・骨デバッグ描画
  SkeletonSocketComponent.h/.cpp ジョイント追従（LateUpdate。生成順に依存しない）
```

### 9.9 残作業

| # | 項目 | 状態 | 備考 |
|---|---|---|---|
| ⑥ | エンジン内部の GameObject 継承の解消 | **未着手** | 残る `dynamic_cast` 4 箇所。**水面の反射ビュー条件・夜の明暗斑・SkyBox/IBL という既知バグが密集した領域**に触るため、独立したセッションで慎重に行うべき |
| ⑩ | CameraComponent | 未着手 | 任意。既存設計を壊さない薄い橋のみ |
| ④ | データ駆動シーン・プレハブ | 未着手 | `IComponent::OnSerialize/OnDeserialize` と `GetTypeName()` の器は用意済み。シリアライズは現状まだ `ModelGameObject::OnSerialize` が `"transform"` キーを書く旧形式のまま（**互換維持のため意図的**） |

**②の残り**: `MaterialComponent` / `CustomShaderComponent` への分割は §9.4 の理由で保留。

### 9.10 検証記録

| ステップ | ビルド | 描画確認 | CollisionTestScene |
|---|---|---|---|
| 0（ベースライン） | OK | OK 60 FPS | — |
| ③ 基盤 | OK | OK ベースラインと同一 | — |
| ⑦ Collider | OK | OK | **PASS 29 / FAIL 0** ＋ワイヤ表示・状態色分け確認 |
| ① Transform | OK | OK ＋Inspector のトランスフォーム値が JSON から復元されることを確認 | **PASS 29 / FAIL 0** |
| ② MeshRenderer | OK | OK ＋ビューポートクリックでピッキング成功・ギズモ表示 | **PASS 29 / FAIL 0** |
| ⑤ Animator | OK | OK AssignmentScene で武器追従・手パーティクル・アニメーション ＋**生成順逆転でも追従** | **PASS 29 / FAIL 0** |
| ⑧⑨ Sprite/Particle | OK | OK **パーティクルにギズモが出る**・エミッタ位置が手に追従 | **PASS 29 / FAIL 0** |

新規クラッシュダンプ 0 件、ログの `[error]` / `[critical]` 0 件（既存の
テクスチャ圧縮フォールバック警告のみ）。

### 9.11 作業中に踏んだ実務的な罠

| 内容 |
|---|
| **`GetSubsystem` は既に使われていた**（§9.0）。計画の案 A をそのまま実行するとコンパイルエラー |
| **`static_assert(is_base_of<IComponent,T>)` がミックスイン取得を阻む**。問い合わせ系では外す |
| **アプリ起動中はリンクできない**（`LNK1104`）。ビルド前に必ずプロセスを終了する |
| **`perl -i -pe 's/...$/.../'` が CRLF ファイルで空振り**する（`$` が `\r` の前に来る）。行末アンカーを使う置換は Edit ツールで行う |
| **spdlog は 2 秒間隔フラッシュ**なので、実行中にログを読むと途中で切れる。`CloseMainWindow()` で正常終了させてから読む |
| **`python` / `python3` は Store スタブ**で使えない。JSON 加工は PowerShell の `ConvertFrom-Json` を使う |
