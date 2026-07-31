# カメラシステム レビューとリファクタリング案

対象ブランチ: `fix/camera-refactoring` / 調査日: 2026-07-31

---

## 1. 調査範囲

### コア
| ファイル | 役割 |
|---|---|
| `Engine/Src/Camera/ICamera.h/.cpp` | カメラ抽象。ビュー差し替え・TAA ジッタの共有状態も保持 |
| `Engine/Src/Camera/Camera.h/.cpp` | Release（ゲーム）カメラ。SRT ベース |
| `Engine/Src/Camera/Debug/DebugCamera.h/.cpp` | デバッグカメラ。軌道（target/distance/pitch/yaw）ベース＋入力処理内蔵 |
| `Engine/Src/Camera/Camera2D.h/.cpp` | 2D 正射影カメラ |
| `Engine/Src/Camera/CameraManager.h/.cpp` | 名前→カメラの辞書＋タイプ別アクティブカメラ |
| `Engine/Src/Camera/CameraStructs.h` | `CameraParameters` / `CameraSnapshot` / `CameraForGPU` |
| `Engine/Src/Camera/CameraShake.*`, `CameraPresetManager.*` | **呼び出し元ゼロ（デッドコード）** |
| `Engine/Src/Camera/Debug/DebugCameraSettingsSection.*` | エディタ設定の自動保存 |

### 生成・解決側
- `Scene/BaseScene.cpp` — `SetupCamera()` で "Release"/"Debug"/"Camera2D" を生成。`ResolveGameViewCameraName()` / `GetGameViewCamera3D()` / `GetDefaultGameViewCamera3D()` / `DrawWithCamera()`
- `Scene/SceneManager.cpp`, `Scene/IScene.h` — シーンへの委譲
- `Editor/Scene/SceneDebugEditor.cpp:201-207` — キー 1/2 で Debug/Release 切り替え

### 消費側
- `Graphics/Render/RenderManager.cpp` — `GetCameraForPass()` / `GetViewMatrix()` / `GetProjectionMatrix()` / `SetCamera()`
- `Graphics/Render/Pass/RenderPipeline.cpp` — `GetRenderingCamera()`（TAA ジッタ・レンズフレア）
- `EngineSystem/Subsystem/RayTracingSubsystem.cpp` — 全 RT ディスパッチ
- `Graphics/Render/Culling/ModelVisibility.cpp` — `camera->GetFrustum()`
- `Graphics/Render/RenderingTechnique/SSAO/*`, `Lighting/*` — 深度復元行列
- `Particle/Core/ParticleRenderDataBuilder.cpp` — ビルボード
- `Editor/ImGui/Gizmo.cpp`, `ObjectSelector.cpp` — ギズモ・ピッキング
- `Editor/Camera/Module/*`（7 モジュール）— 一覧／Transform／追従／パラメータ／GameView 操作／キーフレーム／クリップ再生

---

## 2. 現状の構造

```
BaseScene ──owns──> CameraManager ──owns──> { "Release": Camera,
                                              "Debug"  : DebugCamera,
                                              "Camera2D": Camera2D }
                          │  activeCamera3D_ / activeCamera2D_ / gameViewCameraOverride_
                          │
描画で使うカメラを決める経路が 4 本、互いに独立:
  ① BaseScene::ResolveGameViewCameraName()   override → gameViewCameraName_ → active
  ② BaseScene::GetDefaultGameViewCamera3D()  ①と別ロジック
  ③ RenderManager::GetCameraForPass()        camera_ → active
  ④ RenderPipeline::GetRenderingCamera()     scene->GetGameViewCamera3D() → active
```

`RenderManager.cpp:60` と `RenderPipeline.cpp:100` に「**GetCameraForPass と一致させること**」という
コメントが入っている。これは設計で保証できていないものを規約で守ろうとしている状態で、
実際に SSAO の黒ちらつき（`Docs` の RenderManager カメラ行列バグ）として発火済み。

---

## 3. 設計上の問題点

重大度順。★★★ = 実バグを生む／既に生んだ、★★ = 拡張の障害、★ = 掃除対象。

### ★★★ A. 「どのカメラで描くか」の解決経路が 4 本ある
上図の通り。さらに `BaseScene::GetDefaultGameViewCamera3D()` は
```cpp
return cameraManager_->GetGameViewCameraOverride().empty()
    ? cameraManager_->GetActiveCamera(CameraType::Camera3D)
    : cameraManager_->GetCamera(gameViewCameraName_);   // ← override 名ではない
```
と、「override が有効なとき override 名を無視して既定名を引く」という名前と実装が食い違う分岐を持つ。
到達条件が狭いため現在は表面化していないが、`ResolveGameViewCameraName()` と結論が変わり得る。

### ★★★ B. ギズモ／ピッキングと描画が別のカメラを見る
ギズモ／ピッキング（`ObjectSelector`, `Gizmo`）は `CameraManager::GetActiveCamera(Camera3D)` を、
実際の描画は `SceneManager::GetGameViewCamera3D()`（override 優先）を見る。**この 2 つは
別の規則なので食い違い得る。**
カメラ一覧 UI（`CameraListEditorModule`）のラジオボタンは `SetActiveCamera` だけ呼び
`SetGameViewCameraOverride` を呼ばないため、キー 1/2 を一度でも押した後は
「一覧で選んでも画は変わらないのにギズモだけズレる」状態になる。

> **調査時の訂正（2026-07-31）**: 当初この項を「`BaseScene::DrawWithCamera()` が描画のたびに
> アクティブカメラを差し替えて戻すのが原因」と書いたが、**`BaseScene::Draw()` はメインループから
> 呼ばれていない**（`Framework::Run` は `Update` → `PrepareRender` → `ExecuteRenderPipeline` のみ。
> 実描画は RenderGraph の `GeometryPass` → `RenderManager::DrawMainQueuePass` 経由）。
> つまり差し替えコードは死んだ経路にあり、症状の原因は上記「2 つの解決規則の食い違い」の方。
> 差し替え自体は Phase 1 で削除済み。

### ★★★ C. カメラの「複数ビュー」概念が無い
反射ビュー・RT・カスケードのように 1 フレームで複数の視点が必要なのに、
「カメラ = ビュー」で 1 つしか持てない。その回避策が積み上がっている:

- `ICamera::BeginViewOverride()` — カメラのビュー行列を一時的に差し替えるハック。
  **現在の呼び出し元はゼロ**（平面反射が SSR へ移行して死んだ）だが、
  `ICamera` の protected に 5 個の状態変数が残り、`Camera`/`DebugCamera` の
  `GetViewMatrix()`/`GetProjectionMatrix()`/`GetPosition()` は全部この三項演算子分岐を抱えている。
- `DeferredLightingTechnique::UpdateDepthReconstruction()` は
  「gCamera はフリッカー防止で更新しない」ためビュー別の invViewProj バッファを別途持つ、という個別対処。
- `RayTracingSubsystem` は全ディスパッチで `GetGameViewCamera3D()` を使うので
  ReflectionView では行列が壊れる（`RayTracing_Refactoring_Review.md` に記載済みの潜在バグ）。

### ★★ D. Camera と DebugCamera が別表現で、共通の型が無い
| | Camera | DebugCamera |
|---|---|---|
| 姿勢 | `scale_/rotate_/translate_`（オイラー＋アフィン） | `target_/distance_/pitch_/yaw_`（軌道） |
| 行列 | `Inverse(MakeAffine(...))` | `LookAt(eye,target,up)` |
| GPU | `TransformationMatrix` + `CameraForGPU` | `CameraForGPU` のみ |
| 操作 | 外部モジュール（`CameraGameViewControlModule`） | クラス内蔵 |

結果として:
- `CameraSnapshot` が両方のフィールドを全部持ち `isDebugCamera` フラグで分岐する**手書きタグ付きユニオン**になっている
- 具象型への `dynamic_cast` が **21 箇所**（`BaseScene`, `CameraPresetManager`, 編集モジュール 5 種）
- 「デバッグカメラの操作（Blender 風）をゲームカメラで使う」ことが構造的にできない。
  ユーザーが感じている使い勝手の悪さの直接の原因。

### ★★ E. カメラが入力・ImGui・ウィンドウを直接知っている（依存が逆向き）
`DebugCamera::HandleMouseInput()` の中身:
```cpp
ImGuizmo::IsOver() || ImGuizmo::IsUsing()        // エディタ都合のガード
ImGui::GetMouseCursor() == ImGuiMouseCursor_ResizeEW  // ドッキング都合のガード
ImGui::FindWindowByName("Game")                  // ウィンドウ名の直書き
engineSystem_->GetComponent<InputManager>()      // エンジン全体への参照
```
- `Update()` の**本体が丸ごと `#ifdef USE_IMGUI`**。Release ビルドでは行列更新すら走らないので、
  ウィンドウリサイズでアスペクトが追随しない（登録だけは残り CB を消費する）。
- `Camera` / `DebugCamera` が両方 `WinApp::GetCurrentClientWidthStatic()` を直接叩き、
  同じアスペクト比計算をコピペしている。
- カメラ単体でのテストが不可能。

### ★★ F. 操作系が 3 系統に分散し、優先順位が未定義
| 操作元 | 対象の選び方 | 操作 |
|---|---|---|
| `DebugCamera` 内蔵 | 自分自身 | 中ドラッグ=回転 / Shift+中=パン / ホイール=ドリー / WASD=飛行 |
| `CameraGameViewControlModule` | **最初に見つかった `Camera` 型** | 右ドラッグ=回転 / WASD / Q・E |
| `CameraFollowEditorModule` | **アクティブ 3D カメラ** | 追従・注視で毎フレーム上書き |
| `CameraClipPlayerModule` / `KeyframeEditorModule` | アクティブ 3D カメラ | 再生中に姿勢を上書き |

対象の選び方すら 3 通り違う。同時に有効化したときどれが勝つかは更新順序依存で、
「フライトモード ON のまま追従を有効にすると操作が効かない」類の事故が起きる構造。

### ★ G. 死んでいるコード・毎フレームの無駄
- `Camera::cameraResource_` / `cameraData_`（`TransformationMatrix` world+WVP）:
  **GPU アドレスを取得する手段が無い**（`GetGPUVirtualAddress()` が返すのは `cameraGPUResource_` の方）。
  毎フレーム `Multiply(view, proj)` して書き込んでいるが誰も読まない完全な死にリソース。
- `CameraShake` / `CameraPresetManager` — 呼び出し元ゼロ。
- `DebugCamera::CameraSettings::useGameView == false` の分岐は `"Scene"` ウィンドウを探すが、
  **そのウィンドウはどこにも存在しない**（`DockingUI` が登録するのは `"Game"` と `"Canvas"` のみ）。
  `BaseScene::SetupCamera()` が常に `true` を入れているから動いているだけのデッドパス。
- `draggingLeft_` は**中ボタンによる回転**の状態。命名が実装と食い違っている（`draggingMiddle_` はパン）。
- `ICamera::GetFrustum()` は毎回 VP 乗算＋6 平面抽出。`ModelVisibility` からモデル単位で毎フレーム呼ばれる。
- `CameraDebugUI::Draw()` は内部で `UpdateModules()` を呼ぶが、`SceneDebugEditor::Update()` も
  `UpdateDebugModules()` を呼ぶ。両経路が有効化されるとモジュール更新が二重に走る。

### ★ H. 所有と識別
- `CameraManager` はシーン所有なので、シーン切替でカメラが全部作り直される
  （`RenderManager::SetCameraManager()` にダングリング回避のクリアが要る、というコメント済みの負債）。
- `"Release"` / `"Debug"` / `"Camera2D"` のマジックストリングが `BaseScene` / `SceneDebugEditor` /
  各編集モジュールに散在。
- `RegisterCamera()` は同名上書き時にアクティブ参照だけクリアするが、
  他所がキャッシュした `ICamera*` は無効化されない。

---

## 4. リファクタリング案

### 目標: 「カメラ＝データ」「操作＝コントローラ」「描画が使うのは ViewInfo」の 3 分割

```
[入力] ─> ICameraController ─> CameraTransform ─┐
                                                 ├─> ViewInfo（不変スナップショット）─> 描画/カリング/RT
              CameraProjection ──────────────────┘
```

### Phase 0: 掃除（低リスク・単独マージ可・半日）— **実施済み（2026-07-31）**
1. ✅ `BeginViewOverride` / `EndViewOverride` と `ICamera` の override 用 protected 状態（5 変数）を削除。
   `Camera`/`DebugCamera` の `GetViewMatrix()`/`GetProjectionMatrix()`/`GetPosition()` から三項分岐が消えた。
2. ✅ `Camera::cameraResource_` / `cameraData_` / `TransformationMatrix` 依存を削除。
   毎フレームの `Multiply(view, proj)` と 1 バッファ分の CB が不要になった。
3. ✅ `CameraShake` / `CameraPresetManager` を削除（`.vcxproj` / `.filters` からも除去）。
4. ✅ `CameraSettings::useGameView` と `"Scene"` 分岐を削除。判定は常に `"Game"`。
   `BaseScene::SetupCamera()` のフラグ設定ブロックも不要になった。
5. ✅ `draggingLeft_/draggingMiddle_` → `orbiting_/panning_`、`IsMouseInSceneWindow()` →
   `IsMouseInGameWindow()` にリネーム（命名と実装の食い違いを解消）。
6. ✅ `CameraDebugUI::Draw()` から `UpdateModules()` を外し、更新経路を
   `SceneDebugEditor::Update()` → `CameraManager::UpdateDebugModules()` の 1 本に統一。
7. ✅ 削除した API を参照していたコメント（`DeferredLightingTechnique.h` / `DeferredLighting.PS.hlsl`）を
   `Camera::TransferMatrix` 参照へ更新。**「gCamera はフレーム 1 回だけ書く」という不変条件自体は維持**。

> ⚠️ このフェーズは `ICamera` / `Camera` / `DebugCamera::CameraSettings` のクラスレイアウトを変える。
> 増分ビルドだと ODR 不整合でヒープ破損（`c0000374`）を起こすため、**クリーンリビルド必須**。

### Phase 1: ViewInfo の導入 — **実施済み（2026-07-31）**

**追加した型**（`Engine/Src/Camera/View/`）
```cpp
struct ViewInfo {                  // フレーム内不変のスナップショット
    const ICamera* camera;         // 移行期の互換用（Phase 2 で撤去）
    RenderViewType type;
    Matrix4x4 viewMatrix, projection, viewProjection, invViewProjection;
    Vector3 position;
    Frustum frustum;               // 構築時に 1 回だけ抽出
    float nearZ, farZ;
    Vector2 jitterNdc;
    D3D12_GPU_VIRTUAL_ADDRESS cameraCBV;
    bool isValid;
};
class FrameViews { ... };          // ビュー種別で引く。2D ビューは独立に保持
class ViewBuilder { static ViewInfo Build(const ICamera*, RenderViewType); };
```

**唯一の解決点**: `RenderPipeline::PrepareFrameViews()`。
`EngineSystem::ExecuteRenderPipeline()` がフレーム先頭で呼び、
「カメラ解決 → TAA ジッタ注入 → ViewInfo スナップショット」を 1 回だけ行い、
`RenderContext::frameViews` と `RenderManager::SetFrameViews()` で配る。

1. ✅ 解決経路の統合: `RenderManager::GetCameraForPass()` / `RenderPipeline::GetRenderingCamera()` /
   `BaseScene::DrawWithCamera()` を削除。**「GetCameraForPass と一致させること」というコメントが不要になった。**
2. ✅ `DrawViewInfo::camera`（`const ICamera*`）→ `const ViewInfo* view` に置換。
   `RenderContext::cameraManager` → `const FrameViews* frameViews`。
3. ✅ 消費側の切り替え: SSAO / SSAOBlur / DeferredLightingPass / WaterCausticsTechnique /
   PostEffectPass(Outline) / RayTracingSubsystem / Model / ModelGameObject / ModelVisibility。
4. ✅ 視錐台は ViewInfo 構築時に 1 回だけ抽出（`ICamera::GetFrustum()` のモデル単位呼び出しを廃止）。
5. ✅ VP 乗算の重複除去: `Model` は `view.view->viewProjection` を使い、モデルごとに掛け直さない。
6. ✅ RT はビュー種別に対応する ViewInfo を引く（`context.viewSettings.viewType`）。
   ReflectionView を復活させる場合は `FrameViews` へ 1 つ足すだけでよくなった。

**この過程で見つかった既存バグ**
- `RenderContext::cameraManager` は**どこからも代入されておらず常に nullptr** だった。
  そのため `PostEffectPass` の Outline は `SetCameraClipPlanes()` が一度も呼ばれず、
  クリップ面が既定値のままだった（Phase 1 で `ViewInfo` 経由になり解消）。
  SSAO / SSAOBlur / RenderPipeline にあった `cameraManager` フォールバックも全て死んでいた。
- `BaseScene::Draw()` はメインループから呼ばれていない（B の訂正欄参照）。

**維持した不変条件**
- `gCamera` CBV は 1 フレームに 1 回だけ書く（インフライトのフリッカー対策）。ビュー別に必要な
  invViewProj は `DeferredLightingTechnique` のビュー別バッファ方式のまま。
- モデルの WVP・invViewProj・Frustum が**同じ**（ジッタ込みの）行列から導かれる
  → ViewBuilder がジッタ注入後にスナップショットすることで構造的に保証。

**検証**: Development / Release 両構成ビルド成功。実機で `git stash` による A/B（HEAD vs Phase 0+1）を
取り、グリッド・床・空・モデル・影・パーティクル・SSAO すべて同一描画を確認。

### Phase 2: カメラのデータ化 — **実施済み（2026-07-31）**

**削除**: `ICamera` / `DebugCamera` / `Camera2D`（4 型 → 1 型）
**追加**: `Camera/Control/OrbitFlyController`（旧 DebugCamera の操作部）

```cpp
class Camera final {                 // 継承なし・具象 1 つ
    Vector3 scale_, rotate_, translate_;   // 正射影時は zoom / Z回転 / 中心座標
    CameraParameters parameters_;          // projectionType で 3D / 2D を表す
    // 行列導出 + TAA ジッタ + GPU 定数バッファのみ。入力も操作方法も持たない
};
class OrbitFlyController {           // 「どう動かすか」はこちら
    Settings settings_; OrbitState state_;  // target / distance / pitch / yaw
    void Update(Camera* camera);            // 入力 → 軌道状態 → Camera の Transform
};
```

1. ✅ 姿勢表現の統一。軌道パラメータは `OrbitFlyController::OrbitState` の内部状態へ移動し、
   毎フレーム `Camera` の Transform（位置・オイラー角）へ書き出す。
2. ✅ `CameraSnapshot` から `isDebugCamera` 分岐を撤去（Transform + 投影パラメータのみ）。
3. ✅ **具象型への `dynamic_cast` 21 箇所 → 0 箇所**。
4. ✅ 2D は `CameraParameters::projectionType == Orthographic` で表現し `Camera2D` を畳んだ。
   `GetGPUVirtualAddress()` が 0 / `TransferMatrix()` が空、という「実装できないインターフェース」も消滅。
5. ✅ カメラの取り付け先を `CameraManager` が管理（`AttachOrbitController(name)`）。
   同じ操作を任意のカメラへ付け替えられる。
6. ✅ 更新順序を `CameraManager::Update()` で「コントローラ → Transform → 行列」に一本化。

**姿勢変換の等価性（この変更で最も behavior-sensitive な点）**
旧 `DebugCamera` は `Matrix::LookAt(eye, target, +Y)` で直接ビュー行列を作っていた。
新実装は軌道角をオイラー角へ変換して `Inverse(MakeAffine(scale, rotate, translate))` に載せる。
`MakeAffine` の回転は `Rx*Ry*Rz`（行ベクトル規約）で、roll = 0 のとき前方軸は
`(cosX sinY, -sinX, cosX cosY)`。ここへ `rotate = (pitch, yaw + π, 0)` を入れると
前方軸・右軸・上軸の 3 本すべてが `LookAt` の出力と一致する（pitch は ±0.49π にクランプされ
`cos(pitch) > 0` が保証されるため場合分け不要）。**数式上は厳密に同じビュー行列**。

**保存ファイルの互換**
- `DebugCamera.json` はキー名を変えていない（`target`/`distance`/`pitch`/`yaw` は
  コントローラ側の状態として同じキーで保存される）。既存ファイルをそのまま読める。
- カメラクリップ（`CameraSequenceAssetIO`）は旧フォーマット（`isDebugCamera` + 軌道パラメータ）を
  読み込み時に視点位置＋オイラー角へ変換する。保存は新フォーマットのみ。

**この過程で見つかった／作り込みかけた問題**
- `CameraGameViewControlModule` は「最初に見つかった `Camera` 型」を操作対象にしていた。
  型が 1 つになったことで対象が不定（Debug や 2D を拾い得る）になるため、
  「アクティブ 3D カメラ、ただし軌道コントローラ付きなら対象外」へ修正した。

> ⚠️ `Camera` / `CameraParameters` / `CameraSnapshot` のレイアウトが変わる。
> 増分ビルドの途中中断を挟んだ場合は ODR 不整合を避けるため
> `-t:Clean -p:BuildProjectReferences=false` → `-t:Build`（外部プロジェクトを巻き込まない）で通すこと。

**検証**: Development / Release 両構成クリーンビルド成功。実機で通常描画がベースラインと一致することを確認。
さらに Debug カメラを一時的に既定へ差し替えて起動し、保存状態（高度 151m・俯角 45°）どおりの
見下ろし画になることを確認（オイラー変換の実機検証。確認後に一時変更は撤去済み）。

### Phase 3: 操作の分離 — **実施済み（2026-07-31）**

**追加**
```cpp
struct CameraInputState {          // Camera/Control/CameraInputState.h
    Vector2 mouseDelta; int wheelDelta;
    bool orbitButton, panButton, lookButton;
    bool forward, back, left, right, up, down, boost;
    bool active;                   // ビューポート上かつギズモ/ドッキング操作中でない
};
class ICameraController {          // Camera/Control/ICameraController.h
    virtual void Update(const CameraInputState&, float dt, Camera&) = 0;
    virtual void ApplyTo(Camera&) const = 0;
    virtual void Reset() = 0;
    virtual const char* GetDisplayName() const = 0;
};
class OrbitFlyController;          // Blender 風（旧 DebugCamera の操作部）
class FreeLookController;          // 一人称フライ（旧 CameraGameViewControlModule の操作部）
class EditorCameraInput;           // Editor/Camera/。ImGui 依存はここだけ
```

1. ✅ 入力の正規化層 `EditorCameraInput::Collect()` を新設。
   **ImGui / ImGuizmo / InputManager / ウィンドウ判定への依存はこの 1 ファイルに閉じた。**
   `OrbitFlyController` から `#ifdef USE_IMGUI` が消え、全構成で同じコードパスが走る。
2. ✅ `ICameraController` を導入し `OrbitFlyController` / `FreeLookController` が実装。
3. ✅ `CameraManager::AttachController<T>(name)` で **1 カメラにつきコントローラは 1 つ**。
   更新は `CameraManager::Update(input, dt)` の 1 経路のみ。
   → 「DebugCamera 内蔵の操作 / GameView 操作モジュール / 追従モジュールが同じカメラを
   別々に書き換え、どれが勝つか未定義」という状態が構造的に起きなくなった。
4. ✅ `CameraGameViewControlModule` は入力処理を手放し、**設定 UI だけ**になった。
   対象カメラも「最初に見つかった Camera 型」からアクティブ 3D カメラへ。
5. ✅ 「Blender 操作をゲームカメラでも」は `AttachController<OrbitFlyController>("Release")` で実現できる。

**この過程で作り込んだリグレッションと修正**
入力を正規化する際、旧実装にあった「ドラッグは**押し込みエッジ**で開始する」判定を落とし、
「ボタン押下中なら操作中」にしてしまった。その結果ビューポート外で押されたままの状態や
入力デバイスの状態がフレーム途中で有効になった場合に軌道カメラが勝手に回り、
**エディタ設定の自動保存によって `DebugCamera.json` の姿勢が書き換わった**（実際に発生）。
`OrbitFlyController` / `FreeLookController` の両方でエッジ判定を復活させ、
非アクティブ時はボタン状態も忘れる（＝ビューポートへ戻った瞬間に再開しない）ようにした。

**検証**: Development / Release 両構成ビルド成功。実機で通常描画がベースラインと一致。
起動 → 20 秒放置 → 終了で `DebugCamera.json` が 1 バイトも変化しないことを確認（ドリフト無し）。

### Phase 3 の残り（未実施）
- `CameraFollowEditorModule` / `CameraClipPlayerModule` / `CameraKeyframeEditorModule` は
  まだコントローラ化しておらず、カメラの Transform／軌道状態を直接書いている。
  既定では無効なので現状の競合リスクは低いが、`FollowController` / `ClipPlaybackController`
  として `ICameraController` に載せれば「1 カメラ 1 コントローラ」の保証が全操作へ及ぶ。

### Phase 4: エディタ側の整理 — **実施済み（2026-07-31）**

「Debug カメラ／Release カメラを `SetActiveCamera` + `SetGameViewCameraOverride` の
**2 つの状態**で切り替える」をやめ、役割 2 つ + フラグ 1 つにした。

```cpp
// CameraManager
SetSceneCameraName(name);   // エディタ視点（既定 "Debug"）
SetGameCameraName(name);    // ゲーム視点（既定 "Release"）
SetUseSceneCamera(bool);    // ← どちらを覗いているかはこのフラグ 1 つだけ
Camera* GetViewCamera();    // 描画・ギズモ・ピッキングが見る唯一の 3D カメラ
```

1. ✅ **状態を 1 つに統合**。`gameViewCameraOverride_` と 3D の「アクティブカメラ名」を廃止。
   キー 1/2 は `SetUseSceneCamera(true/false)` のトグルになった。
2. ✅ **`GetActiveCamera(Camera3D)` を `GetViewCamera()` に一本化**。
   ギズモ／ピッキング（`ObjectSelector` / `Gizmo`）と描画が同じカメラを見ることが
   型のレベルで保証され、**問題 B（一覧で選んでもギズモだけズレる）が消えた**。
3. ✅ シーン側の解決規則を撤去。`BaseScene::ResolveGameViewCameraName()` /
   `GetDefaultGameViewCamera3D()`（呼び出し元ゼロの死に API）/ `gameViewCameraName_` を削除し、
   `IScene` / `SceneManager` からも `GetDefaultGameViewCamera3D` を除去。
4. ✅ マジックストリングを `CameraNames::Scene / Game / Camera2D` に集約。
   登録キーの文字列は従来のまま（"Debug" / "Release"）で、役割は API 名で表す。
5. ✅ カメラ一覧 UI を「ビューの切り替え（エディタ視点／ゲーム視点）」＋
   「各カメラを役割へ割り当てる」構成に変更。選択が必ず描画へ反映される。

**残り（未実施）**: Scene カメラをエディタ（`EngineSystem`）所有にしてシーンをまたいで生存させる件。
現在も `BaseScene` が両方を所有しており、シーン切替でエディタ視点がリセットされる。
姿勢自体は `DebugCamera.json` から復元されるため実害は小さい。所有権と
`EditorSettingsSubsystem` への登録タイミングを動かす変更になるため、独立した作業として切り出す。

**検証**: Development / Release 両構成ビルド成功。実機で描画がベースラインと一致。
起動 → 20 秒放置 → 終了で `DebugCamera.json` に変化なし。

### 推奨順序と規模感（実績）
```
Phase 0 掃除            振る舞い不変。単独マージ可
Phase 1 ViewInfo        ★描画側の整合性が構造で保証される。最優先
Phase 2 カメラのデータ化 dynamic_cast 21 箇所 → 0
Phase 3 操作の分離       ImGui 依存がカメラ層から消える
Phase 4 エディタ統合     Scene/Game カメラの整理（使い勝手の本丸）
```
Phase 1 を先に済ませると、Phase 2/3 でカメラ型を触っても描画側へ波及しない（境界が ViewInfo で切れる）ため、
この順序が最も安全だった。

---

## 5. 検証観点（各フェーズ共通）

- **SSAO / DeferredLighting の深度復元**: 画面空間パスの行列は「実際に G-Buffer を描いたビュー」から
  取れているか（過去バグの再発ポイント）。
- **TAA**: ジッタ込み行列で WVP・invViewProj・Frustum が揃っているか。収束するか。
- **反射・RT**: ビュー別に正しい invViewProj が渡っているか。
- **エディタ**: キー 1/2 の切替、カメラ一覧での切替、ギズモ位置、`DebugCamera.json` の復元。
- **Release ビルド**: `USE_IMGUI` 無しでもカメラが更新され、リサイズでアスペクトが追随するか。
