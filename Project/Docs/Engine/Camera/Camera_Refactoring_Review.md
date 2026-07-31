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

### Phase 2: カメラのデータ化（`Camera` / `DebugCamera` の統合）
```cpp
struct CameraTransform { Vector3 position; Vector3 eulerRotation; };
struct CameraProjection {                       // 現 CameraParameters を拡張
    enum class Type { Perspective, Orthographic } type = Type::Perspective;
    float fovY = 0.45f, nearZ = 0.1f, farZ = 1000.0f;
    float orthoHeight = 10.0f;
    float aspect = 0.0f;                        // 0 = ビューポートから供給
};
class Camera final {                            // 具象 1 つ。継承なし
    CameraTransform transform;
    CameraProjection projection;
    ViewInfo BuildView(float aspect) const;     // 純粋関数
};
```
- 軌道パラメータ（target/distance/pitch/yaw）は**カメラの状態ではなく `OrbitController` の内部状態**へ移す。
  コントローラが毎フレーム `CameraTransform` を出力する。
- `CameraSnapshot` の `isDebugCamera` 分岐と 21 箇所の `dynamic_cast` が消える。
- 2D は `CameraProjection::Orthographic` で表現し、`Camera2D` を畳む
  （`GetGPUVirtualAddress()` が 0、`TransferMatrix()` が空実装、という「実装できないインターフェース」も消える）。

### Phase 3: 操作の分離
```cpp
struct CameraInputState {   // ImGui / InputManager から変換して渡す
    Vector2 mouseDelta; int wheelDelta;
    bool orbitButton, panButton, lookButton;
    bool forward, back, left, right, up, down, boost;
    bool hovered;           // ビューポート上か（ギズモ・ドッキング操作中は false）
};
class ICameraController {
    virtual void Update(const CameraInputState&, float dt, CameraTransform& io) = 0;
};
class OrbitFlyController;      // 現 DebugCamera の操作部（Blender 風）
class FreeLookController;      // 現 CameraGameViewControlModule
class FollowController;        // 現 CameraFollowEditorModule
class ClipPlaybackController;  // 現 CameraClipPlayerModule
```
- **どのカメラにどのコントローラが付くかを 1 箇所で決める**ので、F（3 系統の競合）が構造的に起きない。
- ImGui / ImGuizmo / InputManager / EngineSystem / WinApp への依存がカメラ本体から消え、単体テスト可能になる。
- 「Blender 操作をゲームカメラでも使いたい」が `OrbitFlyController` を付け替えるだけで実現する。
- Release ビルドでも姿勢更新（＝行列・アスペクト追随）が走るようになる。

### Phase 4: エディタ側の整理（使い勝手の本丸）
現状の「Debug カメラ／Release カメラを `SetActiveCamera` + `SetGameViewCameraOverride` の
**2 つの状態**で切り替える」をやめ、Unity/UE と同じ役割分担にする。

| | 所有者 | 寿命 | 保存 |
|---|---|---|---|
| **Scene カメラ** | エディタ（`EngineSystem` 側） | シーンをまたいで生存 | `DebugCameraSettingsSection` を流用 |
| **Game カメラ** | シーン | シーンと同じ | シーン JSON |

- Game ウィンドウに流す ViewInfo を Scene カメラ由来にするかどうか、を**フラグ 1 つ**にする
  （現在のキー 1/2 はこのフラグのトグルになる）。二重管理と復元処理が消える。
- ギズモ／ピッキングは「そのウィンドウを実際に描いた ViewInfo」を使う → B のギズモずれが消える。
- カメラ一覧はシーンの Game カメラの選択 UI にする（選択＝描画にも反映される、が保証される）。
- マジックストリングを `CameraId`（強い型 or ハンドル）へ。`ICamera*` の生キャッシュをやめる。

### 推奨順序と規模感
```
Phase 0 掃除            半日   単独マージ可・振る舞い不変
Phase 1 ViewInfo        2-3日  ★描画側の整合性が構造で保証される。最優先
Phase 2 カメラのデータ化 2日    dynamic_cast 21 箇所が消える
Phase 3 操作の分離       2日    使い勝手の改善が入るのはここ
Phase 4 エディタ統合     2日    Scene/Game カメラの整理
```
Phase 1 を先に済ませると、Phase 2/3 でカメラ型を触っても描画側へ波及しない（境界が ViewInfo で切れる）ため、
この順序が最も安全。

---

## 5. 検証観点（各フェーズ共通）

- **SSAO / DeferredLighting の深度復元**: 画面空間パスの行列は「実際に G-Buffer を描いたビュー」から
  取れているか（過去バグの再発ポイント）。
- **TAA**: ジッタ込み行列で WVP・invViewProj・Frustum が揃っているか。収束するか。
- **反射・RT**: ビュー別に正しい invViewProj が渡っているか。
- **エディタ**: キー 1/2 の切替、カメラ一覧での切替、ギズモ位置、`DebugCamera.json` の復元。
- **Release ビルド**: `USE_IMGUI` 無しでもカメラが更新され、リサイズでアスペクトが追随するか。
