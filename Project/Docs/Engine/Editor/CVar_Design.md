# CVar（コンソール変数）システム 設計書

作成日: 2026-08-01
ステータス: **コア実装＋ポストエフェクト全 13 種＋エンジン設定 5 セクションの移行 完了**
（残: DebugCamera / Water）

## 目的

エンジン層のパラメータ 1 個を追加するたびに発生していた定型作業を、5 箇所から 1 箇所に減らす。

**導入前**（Vignette に「強度」を追加する場合）

1. `struct VignetteParams` にメンバを追加
2. `Vignette::DrawImGui()` にスライダーを 1 行追加（忘れると調整できない）
3. `PostEffectSerialization.cpp` の保存側に 1 行追加
4. 同ファイルの読み込み側に 1 行追加（忘れると保存されない）
5. セクションの対象リストに追記（忘れるとセクションごと保存されない）

**導入後**

```cpp
CVar<float> cvIntensity{ "r.Vignette.Intensity", 0.8f, "ヴィネットの強さ", CVarRange{0.0f, 2.0f} };
```

この 1 行だけで ImGui のスライダー・JSON への自動保存・復元がすべて有効になる。
定義場所とパラメータの実体が同じ場所にあるため、**追記漏れという事故が構造的に起きない**。

## 実装ファイル

| ファイル | 役割 |
|---|---|
| `Engine/Src/Utility/CVar/CVar.h` | `ICVar`（型消去インターフェース）と `CVar<T>`。ImGui 非依存 |
| `Engine/Src/Utility/CVar/CVar.cpp` | 登録/通知、値の等価判定 |
| `Engine/Src/Utility/CVar/CVarRegistry.h/.cpp` | 全 CVar を保持するレジストリ（Meyers シングルトン） |
| `Engine/Src/Editor/ImGui/CVarPanel.h/.cpp` | レジストリから ImGui ウィジェットを自動生成 |
| `Engine/Src/EngineSystem/Settings/CVarSettingsSection.h/.cpp` | 全 CVar を 1 ファイルへ自動保存（`CVars.json`） |

接続は `DebugSubsystem::Initialize`：
- `CVarSettingsSection` を `EditorSettingsSubsystem` へ登録（＝起動時に前回値が復元される）
- `CVarRegistry::FlushPendingWarnings()` で静的初期化中の警告と登録数をログ出力

**横断パネルは作らない。** 全 CVar を並べる「Console Variables」パネルは一度作ったが削除した。
機能ごとのパネル（Post Effects タブ、Atmosphere エディタ等）と同じ値を操作する UI が
2 つ並び、どちらから触ればよいか曖昧になるため。各パネルが自分の接頭辞で
`CVarUI::DrawTree("r.Bloom")` を呼ぶ形に一本化してある。

## 仕組み

自動化の正体は「全変数を 1 つのリストに登録し、UI と JSON がそのリストを走査する」だけ。

```
CVar<float> の コンストラクタ
        │  CVarRegistry::Register(this)
        ▼
   CVarRegistry （名前 → ICVar* / 登録順の配列）
        │                       │
        │ 走査                   │ 走査
        ▼                       ▼
   CVarPanel               CVarSettingsSection
   （ImGui 自動生成）         （JSON 保存/復元）
```

`ICVar` が名前・型・範囲・説明・値へのポインタを持つため、UI もシリアライズも
**型ごとの switch を 1 回書くだけ**で全変数に対応できる。

## 使い方

### 定義

パラメータを使う `.cpp` の無名名前空間に置く。

```cpp
namespace {
    CVar<float> cvIntensity{
        "r.Vignette.Intensity",        // ドット区切りの一意な名前
        0.8f,                          // コードデフォルト
        "ヴィネットの強さ。0 で無効",     // UI のツールチップ
        CVarRange{ 0.0f, 2.0f } };     // 省略時はドラッグ入力になる
}
```

### 読み出し

```cpp
const float intensity = cvIntensity.Get();
```

`Get()` は単なるメンバ参照なので安価だが、**ホットループでは呼ばずローカルへ退避**すること。

### UI へ出す

機能パネル側は 1 行でよい。

```cpp
CVarUI::DrawTree("r.Vignette");        // 該当する CVar がツリー表示される
CVarUI::ResetTree("r.Vignette");       // まとめてデフォルトへ戻す
```

### 命名規約

`カテゴリ.グループ.名前` のドット区切り。UI はこのドットでツリー化する。

| 接頭辞 | 用途 |
|---|---|
| `r.` | レンダリング |
| `d.` | デバッグ表示 |
| `sys.` | システム |

### 対応型

`bool` / `int` / `float` / `Vector3` / `Vector4`（**ColorEdit4 として編集される**）。

### フラグ

| フラグ | 効果 |
|---|---|
| `CVarFlags::NoSave` | JSON に保存しない（一時的な実験用） |
| `CVarFlags::NoUI` | 自動生成 UI に出さない |

## 制約・注意点

- **必ずファイルスコープで定義する。** 関数内 `static` や動的生成の CVar は、
  `CVarSettingsSection` の登録時点で存在しないため復元されない。
- **名前の重複は登録が拒否される**（片方の変更がもう片方に反映されない事故を防ぐため）。
  警告は `FlushPendingWarnings()` でログに出る。静的初期化中は Logger が未初期化のため蓄積方式。
- **`Vector4` は常に色として扱われる。** 色以外の 4 成分が必要な場合は現状の対象外。
- **自動化されるのは「値をいじる UI」だけ。** グラフ、サムネイル、ボタン、
  プリセットのコンボボックス、レイアウトの作り込みは従来どおり手書き。
- **値を実際に反映するコードは自動化されない。** CVar は値の入れ物・UI・保存だけを担う。

## 移行例: Vignette

`Engine/Src/Graphics/PostEffect/Effect/Vignette/`

- `VignetteParams params_` メンバを廃止し、**CVar を唯一のソース**にした
- `UpdateConstantBuffer()` は CVar の現在値を定数バッファへ書き込む。
  差分検知は持たず `Dispatch()` の先頭で毎フレーム実行する（16 バイトの書き込みのため
  比較コストを掛ける意味がない）
- `GetParams()` は CVar から構築して**値を返す**（キャッシュを持たないため参照を返せない）。
  既存の呼び出し側は `auto params = GetParams();` のため影響なし
- `SetParams()` は CVar へ書き戻す。既存のプリセット機構
  （`PostEffectSerialization.cpp`）は無変更のまま動作する
- `DrawImGui()` のスライダー 3 行が `CVarUI::DrawTree("r.Vignette")` の 1 行になった

### 旧保存経路の削除（二重化の解消）

移行直後は、Vignette のパラメータが 2 つの経路で保存されていた。

1. `CVars.json`（本システム）
2. `PostEffects.json` および名前付きプリセット（`PostEffectSerialization.cpp`）

実測では両者とも同じ値になり実害は出ていなかったが、同じ情報が 2 ファイルにあると
**手で編集したときの優先順位がセクションの登録順に依存する**ため、旧経路を削除した。

- `PostEffectSerialization.cpp` から Vignette のパラメータ保存/読み込みを削除
- `enabled`（有効/無効）は従来どおり `enabledStates` 経由で保存される
- 既存ファイルに残る旧形式の `"vignette"` キーは読み飛ばされる（無害）
- 副作用: **名前付きプリセットを切り替えても Vignette のパラメータは変わらない**。
  切り替え対象に戻したい場合は、プリセット側を CVar 名ベースの保存に作り替えること
  （そうすれば他エフェクトの移行に伴ってエフェクト別コードごと削減できる）

移行時の原則: **CVar に移したら旧経路は必ず畳む。併存させない。**

### CVars.json が作られるタイミング

`EditorSettingsSubsystem` は「前回保存分と差分があるセクションだけ」を書き出す。
そのため全 CVar がコードデフォルトのままなら `CVars.json` は作られない。
いずれかの値が変わった時点（UI 操作、またはプリセット適用による `SetParams`）で生成される。

## ポストエフェクト全 13 種の移行（完了）

| エフェクト | CVar 接頭辞 | 実行時値として CVar 化しなかったもの |
|---|---|---|
| Vignette | `r.Vignette` | — |
| Blur | `r.Blur` | — |
| RadialBlur | `r.RadialBlur` | — |
| Bloom | `r.Bloom` | — |
| ChromaticAberration | `r.ChromaticAberration` | `samples`（未使用フィールド） |
| Random | `r.Random` | `time` |
| RasterScroll | `r.RasterScroll` | `time` / `lineOffset` |
| ColorGrading | `r.ColorGrading` | — |
| Shockwave | `r.Shockwave` | `center` / `time`（発動状態） |
| FadeEffect | `r.Fade` | `fadeAlpha` / `fadeType` / `time`（SceneTransition が制御） |
| Dissolve | `r.Dissolve` | — |
| Outline | `r.Outline` | `nearPlane` / `farPlane`（カメラから毎フレーム設定） |
| LensFlare | `r.LensFlare` | `sunUv` / `sunValid` / 解像度（毎フレーム設定） |
| ToneMapping | `r.AutoExposure` | 順応輝度・自動EV などの計測結果 |

### 判断基準: 何を CVar 化しないか

**外部やエンジン内部が毎フレーム設定する値は CVar 化しない。** これらを保存すると、
起動時に古い状態が復元されて事故になる（FadeEffect の `fadeAlpha` を保存すると
黒画面で起動する、など）。実行時値はクラスのメンバとして残し、
`UpdateConstantBuffer()` で「CVar の調整値 + メンバの実行時値」を組み立てて定数バッファへ書く。

### 削除したもの

- `PostEffectSerialization.cpp` のエフェクト別コード（保存 106 行 + 読み込み分）→
  残ったのは有効/無効状態のみで、**約 350 行から約 70 行に縮小**
- 各エフェクトの `GetParams` / `SetParams`（CVar が唯一のソースになったため）
- `ChromaticAberration::ApplyPreset` / `ColorGrading::ApplyPreset`（呼び出し元ゼロのデッドコード）
- `FadeEffect` の未使用 setter 6 個、`Dissolve` の未使用 setter 3 個
- `ToneMapping` の自動露出チューニング値アクセサ 10 個
- `PostEffectSettingsSection` の `fadeAlpha` 特別扱い（キー自体が無くなったため）

また、有効/無効の対象エフェクト名が保存側・読み込み側に二重に並んでいたのを
`kToggleableEffects` 配列 1 箇所へ統合し、`PostEffectNames` の定数を使うようにした。

## 有効/無効状態の CVar 化（完了）

エフェクトの有効/無効も `r.<Effect>.Enabled` として CVar 化し、
**ポストエフェクト関連の保存を完全に CVars.json へ一本化した**（CVar 101 個）。

仕組みは `PostEffectBase` の仮想フック 1 つ。

```cpp
// PostEffectBase
virtual CVar<bool>* GetEnabledCVar() const { return nullptr; }

bool IsEnabled() const {
    if (const CVar<bool>* cvar = GetEnabledCVar()) { return cvar->Get(); }
    return enabled_;   // CVar を持たないエフェクト（FullScreen / ToneMapping）
}
```

各エフェクトはファイルスコープの `cvEnabled` を返すだけでよい。基底が能動的に問い合わせる
形にしたので、初期化順序に依存しない。

これに伴い削除したもの:
- `PostEffectSettingsSection.h/.cpp`（セクションごと不要になった）
- `PostEffectSerialization.cpp`（プリセット実装は CVar ベースへ作り替えて `PostEffectPresetManager.cpp` へ統合）
- `PostEffectManager::RegisterEffect` の `enabled` 引数
  （CVar の既定値と二重管理になっていた。既定値は CVar 側が持つ）

### プリセット機能

`PostEffectPresetManager::CaptureToJson / ApplyFromJson` はポストエフェクトの CVar の
スナップショットを取る実装に変わった。自動保存（CVars.json）が「触った項目だけ」を
保存するのに対し、プリセットは**完全な状態の記録**なのでデフォルト値も含めて保存する
（`CVarSerialization::Save` の `skipDefaults` で切り替え）。

共通処理は `Engine/Src/Utility/CVar/CVarSerialization.h/.cpp` に切り出し、
`CVarSettingsSection` とプリセットの両方から使う。

### NoSave が必要だったケース

`r.Fade.Enabled` は `SceneTransition` が遷移のたびに切り替える実行時状態だった。
そのまま保存すると「前回の遷移途中の状態」で起動してしまうため `CVarFlags::NoSave` を付けた。
**実行時に制御される値は、パラメータだけでなく有効/無効フラグにも存在する**ので、
CVar 化のときは「誰がこの値を書き換えるか」を必ず確認すること。

## エンジン設定の移行（旧セクションの全廃）

手書きの `IEditorSettingsSection` を CVar へ置き換え、以下を**セクションごと削除**した。

| 旧セクション | CVar 接頭辞 | 個数 | 同期方式 |
|---|---|---|---|
| RayTracing | `r.RTShadow.*` | 11 | `Dispatch()` 先頭で `settings_` へプル |
| RenderingTechniques | `r.SSAO.*` `r.SSAOBlur.*` `r.SSAOTemporal.*` `r.TAA.*` `r.CAS.*` `r.WaterCaustics.*` | 15 | 各 `Execute()` 先頭でプル |
| Atmosphere | `r.Atmosphere.*` | 24 | 変更通番が動いたときだけ `SetParameters()` |
| VolumetricCloud | `r.Cloud.*` | 32 | `Update()` 先頭で毎フレームプル |
| AtmosphereLights | `r.AtmosphereLights.*` | 7 | 実体（Light）から毎フレーム**ミラー** |
| DebugCamera | `d.SceneCamera.*` | 20 | 実体（コントローラ・カメラ）から毎フレーム**ミラー** |

### 同期方式の選び方

CVar は「値の入れ物」でしかないため、既存クラスが自前の設定構造体を持っている場合は
**どちらを唯一のソースにするか**を決める必要がある。

1. **CVar が唯一のソース**（Vignette 等）— メンバを消して `Get()` を直接読む。最も単純
2. **CVar → 実体へプル** — 内部で設定構造体を何十箇所も参照していて消しづらい場合。
   構造体はキャッシュとして残し、更新の入口で CVar から取り込む。
   **LUT のような重い再計算を伴う場合は毎フレーム流し込んではいけない**
   （`AtmosphereManager` は `CVarRegistry::GetGlobalRevision()` が動いたときだけ
   `SetParameters()` を呼ぶ。毎フレーム呼ぶと LUT を作り直し続ける）
3. **実体 → CVar へミラー** — 値の実体がシーン寿命のオブジェクト側にあり、
   CVar 側から所有できない場合（後述の AtmosphereLights）

### 有効/無効フックの横展開

`RenderingTechniqueBase` にも `PostEffectBase` と同じ `GetEnabledCVar()` フックを追加した。
これにより「対象名を手で並べた配列に追記し忘れると保存されない」既知の罠
（`RenderingTechniqueSettingsSection` の `kTechniqueNames`）が構造的に消えた。

### 移行時に必ず確認する 3 点

Atmosphere の移行で、既定値を**目視で**書いて km 単位（`planetRadius = 6360.0f`）にしてしまい、
実際は m 単位（`6360000.0f`）だったため、そのまま起動すれば空が完全に壊れる状態になっていた。

1. **既定値は宣言と 1 個ずつ突き合わせる**（目視で「それらしい値」を書かない）
2. **その値をキャッシュしている場所を探す**（`PostEffectManager` の
   `effectPtrCache_` は `SetEffectEnabled` 経由でしか再構築されず、
   CVars.json からの復元が迂回して「タブ上は有効なのに画面に出ない」バグになった。
   `GetGlobalRevision()` の監視で修正）
3. **誰がその値を書き換えるか grep する**（実行時に制御される値は `NoSave`）

### AtmosphereLights: 実体がシーン側にある場合のミラー方式

太陽・月の向きや強度は `LightManager` が持つ `Light`（**シーン寿命**）が実体で、
エディタ・ギズモ・シーンコードのどこからでも書き換えられる。
CVar 側を唯一のソースにはできないため、`EnvironmentFeature` が両方向を担当する。

```
PostSceneInitialize : CVar ──▶ Light   復元（1回だけ）
Update(PostLogic)   : Light ──▶ CVar   ミラー（毎フレーム）
Finalize            : Light ──▶ CVar   最終ミラー（ライトのクリア前）
```

- 復元で**太陽は `IsModified()` が真の項目だけ**を上書きする。
  自動保存は「触った項目だけ」を書き出すため、全項目を無条件に流し込むと
  保存していない項目のコード既定値でシーン側の設定を潰してしまう
- **月は全項目を無条件に**流し込む。月ライトはこの復元処理自身が生成する
  （`MoonEnabled` が真なら第2ディレクショナルライトを作る）ため、
  シーン固有の初期状態を持たないから
- ミラーは毎フレーム走るが、`CVar::Set` は値が実際に変わったときだけ
  変更通番を進めるので、比較コストだけで済む
- 太陽のサーフェス照度・色は Lighting エディタ側の責務なのでミラーしない

### DebugCamera: ミラー方式の 2 例目

エディタ視点カメラ（`CameraNames::Scene` + `OrbitFlyController`）も同じ構図で、
実体はシーン寿命・マウス操作とカメラ UI の両方から書き換わる。
同期は `Engine/Src/Camera/Debug/DebugCameraCVars.h/.cpp` の 2 関数に閉じ、
`BaseScene` が `SetupCamera` / `Update` / `Finalize` から呼ぶ。

- **AtmosphereLights と違い、復元は全項目を無条件に流し込む。**
  コントローラとカメラは生成直後（＝構造体のコード既定値そのまま）なので、
  CVar の既定値を `OrbitFlyController::Settings` / `OrbitState` / `CameraParameters` と
  一致させておけば、未保存の項目には同じ値が入るだけで害がない
- **`aspectRatio` は CVar 化しない。** ウィンドウサイズから毎フレーム導出される実行時値
  （`0.0f` = 自動計算）で、保存しても意味がない
- 軌道状態（target / distance / pitch / yaw）は実行時状態だが、
  **「前回の視点から再開する」ことが仕様**なので保存する（UE / Unity のビューポートカメラと同じ）。
  初期視点に戻したいときは `CVars.json` から `d.SceneCamera.*` を消す
- 復元は「設定 → 軌道状態」の順に行う。`SetState()` が移動範囲のクランプを行うため、
  クランプ値を含む設定が先に入っていないと違う位置に落ちる

## 今後

- `ConsoleUI` との接続（`r.Bloom.Intensity 0.5` で即反映、`r.Bloom.*` で一覧）
- Water（`WaterSettingsSection`）の移行 — アプリ側（`Application/Src/Scenes/WaterTestScene/`）所有
