# エディタ設定 自動保存システム 設計書

作成日: 2026-07-24
ステータス: **Phase 1〜3 実装・検証済み（2026-07-24）**。Phase 4（Default 層との2層化）は任意・未着手

実装ファイル:
- `Engine/Src/EngineSystem/Settings/IEditorSettingsSection.h`（登録先バックリンク＋デストラクタ自己解除つき）
- `Engine/Src/EngineSystem/Settings/EditorSettingsSubsystem.h/.cpp`（USE_IMGUI 時のみ EngineSystem に登録）
- `Engine/Src/Camera/Debug/DebugCameraSettingsSection.h/.cpp`
  - 接続: `BaseScene::SetupCamera`（登録）/ `BaseScene::Finalize`（解除）
- `Engine/Src/Editor/Environment/AtmosphereSettingsSection.h/.cpp`
  - 大気物性＋トグル（エンジン寿命。DebugSubsystem が登録）
- `Engine/Src/Editor/Environment/AtmosphereLightsSettingsSection.h/.cpp`
  - 太陽/月ライト（シーン寿命。EnvironmentFeature が
    PostSceneInitialize で登録 / Finalize で解除。月は moonEnabled 保存時に復元生成）
- `Engine/Src/Editor/Environment/VolumetricCloudSettingsSection.h/.cpp`
  - 雲パラメータ＋enabled＋プリセット index（エンジン寿命。DebugSubsystem が登録）

- Water セクション（Phase 3）
  - `WaterSurfaceParameterPanel::SerializeSettings / DeserializeSettings`（UI キャッシュ＝ベース水質
    σa/σs＋濁度が唯一の情報源。見た目/水質/FFT/DXR屈折/コースティクスを保存。デバッグ表示系は対象外。
    実装は `WaterSurfaceParameterPanelSerialization.cpp` に TU 分離）
  - `WaterTestScene/WaterSettingsSection.h/.cpp` がパネル・runtime・facade へ委譲。
    登録は `WaterSceneController::Initialize`（既定プリセット適用後）、解除はデストラクタ
- PostEffects セクション（2026-07-24 追加）
  - `Graphics/PostEffect/Effect/PostEffectSettingsSection.h/.cpp`（エンジン寿命。DebugSubsystem が登録）
  - シリアライズは `PostEffectPresetManager::CaptureToJson / ApplyFromJson` をプリセットの明示保存と
    共用（実装は `PostEffectSerialization.cpp` に TU 分離）。
    ToneMapping / LensFlare / Outline / Dissolve をプリセット対応に追加
  - FadeEffect の fadeAlpha は実行時のシーン遷移状態のため保存対象外（黒画面起動の事故防止)。
    enabled 状態の欠損キーは「現在値維持」（旧: 強制 false）
- RenderingTechniques セクション（2026-07-24 追加）
  - `Graphics/Render/RenderingTechnique/RenderingTechniqueSettingsSection.h/.cpp`
    （エンジン寿命。DebugSubsystem が登録）
  - 各技術の有効状態 + SSAO / SSAOBlur のチューニング値（行列・画面サイズは実行時値のため対象外）
  - 常時有効の技術（DeferredLighting）は復元時に enabled を変更しない
  - WaterCaustics のパラメータは Water セクション（WaterEditorFacade）所有のため enabled のみ扱う
- Engine Settings「Editor Settings」管理パネル（Phase 3）
  - `EditorSettingsSubsystem::GetSectionStatuses()`（名前/ファイル有無/バックアップ有無/最終保存時刻）
  - パネル描画は `Editor/ImGui/EditorSettingsPanel.h/.cpp`（`DebugSubsystem::Initialize` が
    `RegisterEnginePanel("Editor Settings", ...)` で登録）。
    セクション一覧テーブル＋「リセット」（コードデフォルトへ。直前の保存は .bak に退避）＋
    「バックアップ復元」（1世代前へ）

関連修正: `SceneManager::Finalize` がシーンの `Finalize()` を呼ばず破棄していた欠落を修正
（終了時に登録解除が走らずダングリングポインタでクラッシュしていた根本原因）。
`Application/Saved/` は .gitignore 登録済み（ローカルのエディタ状態のため）。

## 1. 目的

大気・雲・水・デバッグカメラなどの**エディタ/環境設定パラメータ**を、Unreal Engine / Unity と
同様に「変更するたびに自動保存」し、次回起動時に前回の状態から再開できるようにする。

現状はシーン内 GameObject の明示保存（`SceneSaveSystem`）のみが永続化されており、
それ以外のパラメータは毎起動でコード内デフォルト値に戻る。

### 非目標（やらないこと）

- **GameObject / シーンの自動保存はしない**。意図しない配置が強制セーブされることを防ぐため、
  Save ボタン / Ctrl+S による明示保存フローは現状のまま一切変更しない。
- Undo/Redo スタックの実装（将来課題。本設計の Reset / Backup で誤変更リスクを担保する）。

### 参考: 商用エンジンの分離方針

| エンジン | シーン/レベル | 設定 |
|---|---|---|
| Unity | 明示保存（Ctrl+S） | ProjectSettings / EditorPrefs は変更即保存 |
| UE | 明示保存 | Config ini（`SaveConfig()`）は変更即書き込み。`Config/Default*.ini`（プロジェクト共有）と `Saved/Config/`（ローカル状態）の2層 |

「シーンデータ＝明示保存」「設定＝自動保存」の分離は両エンジン共通。設定側の誤変更は
**リセット手段とバックアップで担保**するのが定石であり、本設計もこれに倣う。

## 2. 現状分析

### 2.1 既存の永続化

| 対象 | 仕組み | 備考 |
|---|---|---|
| GameObject | `SceneSaveSystem`（`Engine/Src/Scene/SceneSaveSystem.cpp`） | JSON・オブジェクト単位のファイル分割。`Assets/Scenes/{scene}/_scene.json` + `{key}.json` |
| ImGui レイアウト | `imgui.ini` | ImGui 標準機能 |
| カメラ/パーティクル/ポストエフェクトのプリセット | 各 PresetManager | **手動でファイル指定して保存/読込**。起動時自動復元ではない |

上記以外の設定ファイル（ini/json）は存在しない。

### 2.2 自動保存対象パラメータのオーナー

| 対象 | パラメータ実体 | 寿命 | 備考 |
|---|---|---|---|
| デバッグカメラ | `DebugCamera`: `CameraSettings`（感度/flySpeed 等）+ 姿勢（distance/pitch/yaw/target）+ `CameraParameters`（FOV/クリップ） | シーン（`BaseScene::SetupCamera` で毎回生成、`BaseScene.cpp:303-350` でハードコード初期化） | `CaptureSnapshot()/RestoreSnapshot()` が流用可能 |
| 大気（物性） | `AtmosphereManager::AtmosphereParameters` | エンジン常駐 | レイリー/ミー/オゾン/月/星など |
| 大気（太陽・月） | `LightManager` のライト（方向・強度） | **シーン** | `AtmosphereEditor::SyncFromLights()` が毎フレーム再同期している通り、二重オーナー構造 |
| 雲 | `VolumetricCloudManager::VolumetricCloudParameters` + `VolumetricCloudEditor::activePresetIndex_` | エンジン常駐 | |
| 水 | `WaterSceneController`（`Application/Src/Scenes/WaterTestScene/`）→ `WaterEditorFacade` 経由 | **シーン（アプリ側所有）** | エンジン常駐ではない点に注意 |

### 2.3 流用できる既存基盤

- **nlohmann/json** + ラッパー `JsonManager`（`Engine/Src/Utility/JsonManager/JsonManager.h`）
  - `SafeGet<T>` によるデフォルト値付き読み取り → 欠損キー耐性（後方互換）が最初から得られる
  - `Vector3ToJson` 等の数学型変換ヘルパ
- **EngineSystem 所有のサブシステムパターン**（Hi-Z 脱シングルトンと同型）
- **owner 付き登録/解除パターン**（`GameDebugUI` のエディタ登録と同じ流儀）
- **`ISceneFeature` ライフサイクル**（`PostSceneInitialize` / `Finalize`）— シーン寿命の対象の登録点

## 3. アーキテクチャ

### 3.1 全体像

```
EngineSystem
  └─ EditorSettingsStore (新設・EngineSystem所有)
       ├─ sections_: 登録された IEditorSettingsSection* のリスト (owner付き)
       ├─ lastSavedJson_: セクション名 → 最後に保存したJSON
       └─ Update(): 一定間隔で Serialize→差分比較→変更セクションのみ書き込み

登録側（例）:
  DebugSubsystem::Initialize      → Atmosphere / VolumetricCloud セクション登録（エンジン寿命）
  BaseScene::SetupCamera 直後     → DebugCamera セクション登録（シーン寿命）
  WaterSceneController::Initialize → Water セクション登録（シーン寿命）
  各所有者の Finalize              → UnregisterSections(owner)
```

### 3.2 インターフェース

```cpp
// Engine/Src/EngineSystem/Settings/IEditorSettingsSection.h
class IEditorSettingsSection {
public:
    virtual ~IEditorSettingsSection() = default;
    virtual const char* GetSectionName() const = 0;          // 例: "DebugCamera" (=ファイル名)
    virtual void Serialize(nlohmann::json& out) const = 0;
    virtual void Deserialize(const nlohmann::json& in) = 0;  // SafeGet使用。欠損キーは現値維持
};

// Engine/Src/EngineSystem/Settings/EditorSettingsStore.h
class EditorSettingsStore {
public:
    void Initialize();                        // 保存ディレクトリ確保
    void Finalize();                          // FlushAll() を含む

    // 登録時: ファイルが存在すれば即 Deserialize が走る（＝復元）
    void RegisterSection(IEditorSettingsSection* section, const void* owner);
    void UnregisterSections(const void* owner); // 解除時にそのセクションを最終Flush

    void Update(float deltaTime);             // 変更検知＋保存（毎フレーム呼び出し）
    void FlushAll();                          // 全セクション即時保存（終了時）

    // 管理UI向け
    void ResetSectionToDefault(const std::string& name); // 起動時スナップショットへ戻す
    void RestoreSectionBackup(const std::string& name);  // .bak から復元
    // 各セクションの最終保存時刻などの照会API
};
```

設計のポイント:

- **登録時に即 Deserialize** — エンジン常駐物は起動時に、シーン所有物（カメラ・水・ライト）は
  シーン初期化時に登録されるため、復元タイミングの問題が自然に解決する。
- **登録時に「コードデフォルト」スナップショットを取得** — Deserialize を呼ぶ**前**に一度
  Serialize してデフォルト JSON として保持。Reset to Default の復元元にする。

### 3.3 変更検知: ポーリング差分方式（採用）

| 候補 | 内容 | 判断 |
|---|---|---|
| **ポーリング差分（採用）** | 一定間隔（1秒）で全セクションを Serialize し、前回保存 JSON と比較。差分があるセクションのみ書き込み | UI コードを一切触らずに全パラメータが対象になる。設定構造体は数百B〜数KB なので毎秒シリアライズのコストは計測不能レベル。網羅漏れが構造的に起きない |
| MarkDirty 明示通知 | 各 ImGui ウィジェットの戻り値で `MarkDirty()` を呼ぶ | 精密だが全ウィジェット箇所への仕込みが必要で、**追加漏れ＝保存されないバグ**が恒常的に発生する。不採用（将来、巨大セクションが出た場合の最適化オプションとして残す） |
| 毎フレーム保存 | 変更検知なしで常時書き込み | ドラッグ中に毎フレームディスク I/O。不採用 |

動作詳細:

- チェック間隔: 1.0 秒（`Update` 内で累積時間管理）
- 差分検出後の書き込みは**即時**でよい（1秒間隔チェック自体がデバウンスとして機能する）。
  スライダードラッグ中でも書き込みは最大 1回/秒 に抑えられる
- アプリ終了時（`Finalize`）とセクション解除時（シーン遷移）は差分有無に関わらず
  最終チェック＋Flush を行い、取りこぼしを防ぐ

### 3.4 ファイル配置と書き込み安全性

```
Application/Saved/EditorSettings/
  DebugCamera.json
  Atmosphere.json
  VolumetricCloud.json
  Water.json
  _backup/
    DebugCamera.json.bak      ← 前回保存分（書き込み直前に退避）
    ...
```

- **セクション単位のファイル分割**（`SceneSaveSystem` のオブジェクト分割と同じ流儀）
  - 「カメラだけ初期化したい」＝ファイル削除 1 つで完結
  - 将来、git 管理の切り分けが可能（カメラ姿勢＝個人状態→ignore、大気＝プロジェクト設定→コミット）
- **アトミック書き込み必須**: `{name}.json.tmp` に書き込み → 成功後 rename で置換。
  書き込み途中のクラッシュでファイルが破損して全設定を失う事故を防ぐ
- 置換直前に旧ファイルを `_backup/{name}.json.bak` へコピー（1世代バックアップ）
- 各 JSON に `"version": 1` フィールドを持たせる。読み込み側は `SafeGet` により
  欠損キー＝現値（デフォルト）維持となるため、**フィールド追加は無変更で後方互換**。
  破壊的変更時のみ version 分岐でマイグレーションする

### 3.5 「意図しない変更」への担保

自動保存の副作用（誤操作の永続化）は以下の 3 点で担保する:

1. **Reset to Default**: セクションごとに、起動時スナップショット（3.2）へ戻すボタン
2. **Restore Backup**: `.bak` からの 1 段階復元ボタン
3. **GameObject の明示保存は不変**: リスクの高いシーンデータには自動保存を適用しない

管理 UI は `GameDebugUI::RegisterEnginePanel("Editor Settings", ..., Settings)` で
Engine Settings ウィンドウに 1 パネル追加し、セクション一覧（名前 / 最終保存時刻 /
Reset / Restore ボタン）を表示する。

## 4. 各セクションの実装詳細

### 4.1 DebugCamera（Phase 1）

- 保存内容: `CameraSettings` 全項目、姿勢（distance / pitch / yaw / target）、
  `CameraParameters`（FOV / nearClip / farClip）
- 登録点: `BaseScene::SetupCamera()` の Debug カメラ生成直後（`BaseScene.cpp:326-333` 付近）。
  シーン寿命のため owner はシーン（または `DebugEditorFeature`）とし、`Finalize` で解除
- セクション実装はカメラ自身に持たせず、**アダプタクラス**
  （例: `DebugCameraSettingsSection`）として分離する。`DebugCamera` に nlohmann/json への
  依存を持ち込まないため
- 注意: 復元は登録時（＝シーン初期化時）に走るため、`SetupCamera` のハードコード初期値は
  「ファイルがない場合のデフォルト」として自然にフォールバックになる

### 4.2 Atmosphere（Phase 2）

- 保存内容:
  - `AtmosphereManager::AtmosphereParameters` 一式（エンジン常駐側）
  - 太陽・月ライトの方向（正準順の維持に注意）・強度（`LightManager` 側）
- **二重オーナー対応**: セクション実装（`AtmosphereSettingsSection`）が両方をまとめて
  Serialize / Deserialize する。ライトはシーン寿命のため:
  - Deserialize 時にライトが未生成なら、ライト分は**保留値として保持**し、
    `EnvironmentFeature::PostSceneInitialize`（またはライト取得可能になった最初の適用機会）で反映
  - もしくは実装を単純化するなら、ライト分の適用は `AtmosphereEditor::SyncFromLights` と
    対になる `ApplyToLights` をセクションに持たせ、ライト存在ガード付きで登録時に試行する
- 較正定数（1.75 / 100000 等）はコード側の不変条件であり**保存対象にしない**

### 4.3 VolumetricCloud（Phase 2）

- 保存内容: `VolumetricCloudParameters` 一式 + `activePresetIndex_`
- プリセット index と個別パラメータの両方を保存し、復元時は「パラメータをそのまま適用し、
  index は UI 表示状態としてのみ復元」とする（プリセット定義が変わっても実パラメータが優先）

### 4.4 Water（Phase 3）

- 所有がアプリ側（`WaterSceneController`）のため、登録もアプリ側で行う:
  `WaterSceneController::Initialize` で登録、終了時に解除
- パラメータ取得/適用は既存の `WaterEditorFacade` を経由
- 現状は単一グローバルセクション `"Water"` とする。将来シーンごとに水設定を分けたい場合は
  セクション名を `"Water_{sceneName}"` にする拡張で対応（設計上の変更は不要）
- cbuffer 一致必須箇所（水色 4 箇所 / コースティクス 3 箇所）はランタイム反映側の既存不変条件
  であり、本システムは値の復元のみ行う（反映は既存の適用経路に任せる）

## 5. 実装フェーズ

| Phase | 内容 | 完了条件 |
|---|---|---|
| 1 | `EditorSettingsStore` + `IEditorSettingsSection` + ポーリング差分保存 + アトミック書き込み + `.bak`。DebugCamera セクション接続 | カメラを動かして終了→再起動で姿勢・FOV・感度が復元される |
| 2 | Atmosphere（ライト込み・二重オーナー対応）/ VolumetricCloud セクション | 大気・雲の編集値が再起動で復元される。ライト未生成シーンでもクラッシュしない |
| 3 | Water セクション + Engine Settings「Editor Settings」管理パネル（一覧 / Reset / Restore） | 水設定の復元。パネルから Reset / Restore が機能する |
| 4（任意） | デフォルト値ファイル層（`Config/Default*.json` 相当）とローカル層の 2 層化、version マイグレーション実装 | — |

## 6. リスクと注意点

| リスク | 対策 |
|---|---|
| 書き込み中クラッシュでファイル破損 | tmp 書き込み → rename のアトミック置換（3.4） |
| 誤操作の永続化 | Reset to Default / `.bak` 復元（3.5） |
| シーン寿命オブジェクト（ライト・水・カメラ）への復元タイミング | 「登録時に即 Deserialize」方式により、登録点をシーン初期化に置くことで解決（3.2） |
| フィールド追加/削除による読み込み互換 | `SafeGet` による欠損キー耐性 + version フィールド（3.4） |
| ポーリングのシリアライズコスト | 対象は数 KB の設定構造体のみ。1 秒間隔なら無視できる。巨大セクションが将来出た場合のみ MarkDirty 併用を検討（3.3） |
| デバッグカメラの姿勢を保存したくないユーザー操作（一時的な視点移動） | 仕様として許容（UE/Unity も同様にビューポートカメラは復元される）。嫌なら `DebugCamera.json` を削除すれば初期値に戻る |
| 構造体サイズ変更を伴う実装時の増分ビルド | ODR 事故（ヒープ破損 c0000374）の既知の罠。ヘッダ変更後はクリーンビルドを徹底 |
