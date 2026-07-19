# Sky Atmosphere 夜間・月 実装設計書

CoreEngine の大気散乱システム（Bruneton/Hillaire 方式、実装済み）へ、UE の
Second Atmosphere Light 方式に則った**月と夜間**を追加するための設計書。
`SkyAtmosphere_UE_GapPlan.md` の G8（月・星空）を独立させたもの。

**この文書単体で実装を完遂できること**を目標に書かれている。
「実装フェーズ計画」に従い、**フェーズごとにビルド → 起動 → 検証**を行ってから次へ進むこと。

- 作成日: 2026-07-18
- 対象ブランチ: `dev`（大気散乱 UE ギャップ解消 Phase 1〜4, 6 完了・スペキュラIBL実装済みが前提）
- 最終ゴール: 太陽が地平線下に沈んでも、**月の直接光・月光による空の散乱・
  月光アンビエント・月ディスク（・星空）**によりシーンが現実の夜のように見えること

---

## 1. 背景

### 1.1 現状、夜が真っ黒になる理由

太陽高度が地平線下になると:

1. `SampleTransmittanceToSun` の `PlanetSunVisibility`（惑星遮蔽）で太陽直接光が 0
2. Sky-View LUT（空の輝度）が 0 → 空が黒
3. Sky-View LUT から作る SH アンビエント（`SkyIrradianceSH.CS`）と
   スペキュラIBL（`SkyEnvironmentCapture.CS`）も 0

つまり**シーン内の光源が文字通り1つも無くなる**。現実の夜に存在する光源
（月の直接光・月光の大気散乱・星・大気光）が全く無いのが原因。

### 1.2 UE のやり方（お手本）

UE の SkyAtmosphere は**大気ライトを2本まで**サポートする
（Directional Light の "Atmosphere Sun Light" + "Atmosphere Sun Light Index" 0=太陽 / 1=月）。

- 月は「2本目のディレクショナルライト」。専用の月システムではなく、
  太陽と同じ配管（透過率・散乱寄与・ディスク描画）をもう1本分回すだけ
- Sky-View LUT / Aerial Perspective は**両ライトの散乱寄与を同じ LUT へ合算**して格納する。
  そのため UE の Sky-View LUT は**絶対方位（360°）パラメータ化**
  （Hillaire 原論文の太陽相対・半周対称パラメータ化ではない）
- 月ディスクも太陽と同じ解析的ライトディスク描画（テクスチャ月はスカイマテリアル側の拡張）
- **星空は SkyAtmosphere の担当外**。スカイマテリアルでキューブマップ等を合成する
- 月光強度は物理値（満月 ≈ 太陽の約1/40万）ではなく**美術値＋自動露出の Min EV クランプ**で
  「夜らしい暗さと視認性」を作る

---

## 2. 前提となる既存実装（必読ファイル）

| ファイル | 関係 |
|---|---|
| `Engine/Src/Graphics/Atmosphere/AtmosphereManager.h/.cpp` | 中核。太陽情報取得・CB アップロード・LUT ダーティ管理。月情報の取得元もここに足す |
| `Engine/Assets/Shaders/Atmosphere/Common/AtmosphereCommon.hlsli` | `AtmosphereConstants`（C++ `AtmosphereShaderConstants` と一致必須・208B）、LUT パラメータ化、積分関数。**Phase 1 の主対象** |
| `Engine/Assets/Shaders/Atmosphere/SkyViewLUT.CS.hlsl` | 空の積分。Phase 1 で絶対方位化・色前乗算、Phase 3 で2光源化 |
| `Engine/Assets/Shaders/Atmosphere/CameraVolumeLUT.CS.hlsl` | AP froxel。Phase 1 で色前乗算、Phase 3 で2光源化 |
| `Engine/Assets/Shaders/Atmosphere/SkyAtmosphere.PS.hlsl` | 本番描画・太陽ディスク。Phase 4 で月ディスク追加 |
| `Engine/Assets/Shaders/Atmosphere/SkyIrradianceSH.CS.hlsl` | 空アンビエント SH。Sky-View LUT の消費者 |
| `Engine/Assets/Shaders/Atmosphere/SkyEnvironmentCapture.CS.hlsl` | スペキュラIBL キューブマップ焼き込み。Sky-View LUT の消費者 |
| `Engine/Assets/Shaders/Atmosphere/AerialPerspective.CS.hlsl` + `AtmosphereApply.hlsli` | AP 合成（不透明/水面）。CameraVolume・Sky-View の消費者 |
| `Engine/Assets/Shaders/Cloud/CloudRayMarch.CS.hlsl` / `CloudCubemapCapture.CS.hlsl` | 雲。Sky-View LUT をアンビエント・遠方霞に使用 |
| `Engine/Src/Graphics/Light/LightData.h` | `DirectionalLightData`（48B 固定・`isAtmosphereSun` は GPU padding 領域に配置） |
| `Engine/Src/Graphics/Light/LightManager.h/.cpp` | `GetAtmosphereSunLight()`。月版の取得関数を足す |
| `Docs/Engine/Graphics/Rendering/SkyAtmosphere_UE_GapPlan.md` | 既存ギャップ解消の設計・検証記録 |

**シェーダーはランタイム DXC コンパイル**のため .hlsl 編集はリビルド不要。C++ 変更時のみビルド。

### 2.1 Sky-View LUT の消費者一覧（Phase 1 の影響範囲）

`sunColor × sunIntensity` をサンプル時に掛けている箇所（Phase 1 で全て削除する）:

| 消費者 | サンプル箇所 | 色乗算の現状 |
|---|---|---|
| `SkyAtmosphere.PS.hlsl` | 本番の空描画 | 末尾で乗算（L104） |
| `SkyIrradianceSH.CS.hlsl` | SH 射影の全球 1024 方向 | `SampleSkyLuminance` 内で乗算 |
| `SkyEnvironmentCapture.CS.hlsl` | キューブマップ全テクセル | `SampleSkyLuminance` 内で乗算 |
| `CloudRayMarch.CS.hlsl` | 雲アンビエント（L123）・遠方霞（L332） | サンプル後に `gCloud.sunColor×sunIntensity` 乗算 |
| `CloudCubemapCapture.CS.hlsl` | 同上のキューブマップ版（L104・L244） | 同上 |
| `AtmosphereApply.hlsli`（Water.PS） | AP の SkyView フォールバック（L70） | 乗算 |

CameraVolume LUT の消費者（同様に乗算を削除する）:

| 消費者 | 色乗算の現状 |
|---|---|
| `AerialPerspective.CS.hlsl` | L53 で乗算 |
| `AtmosphereApply.hlsli`（Water.PS） | L39 で乗算 |

**変更しない消費者**: `GodRayMarch.CS.hlsl` は Sky-View LUT を読まず自前積分に
`gAtmosphere.sunColor×sunIntensity` を掛けている。LUT の色ドメイン変更の影響を受けないので触らない。
Transmittance / MultiScattering LUT は無色（ライト非依存）のまま維持し、2光源で共用する。

---

## 3. 実装フェーズ計画（概要）

| フェーズ | 内容 | 見た目の変化 | 状態 |
|---|---|---|---|
| Phase 1 | Sky-View / CameraVolume LUT の**絶対方位化＋ライト色前乗算**（太陽1灯のまま） | **無し**（リグレッションが無いことが合格条件） | ✅ 完了（2026-07-18。昼A/B画素差 平均1.0/765） |
| Phase 2 | 月ライトの配管（LightData / LightManager / AtmosphereManager / CB / UI） | 無し（月はまだ描かれない） | ✅ 完了（2026-07-18。月OFF時ベースライン一致） |
| Phase 3 | LUT・AP の2光源積分＋月の Transmittance on Light＋月光アンビエント | **夜が月光で青白く明るくなる** | ✅ 完了（2026-07-18。月光の青い夜空・雲・透過率赤方偏移を確認） |
| Phase 4 | 月ディスク描画（解析的・満ち欠け付き） | 月が見える | ✅ 完了（2026-07-18。新月⇔満月が太陽との位置関係に自動追従） |
| Phase 5 | 星空（任意） | 星が見える | ✅ 完了（2026-07-19。手続き的星field・昼は暗さマスクで自動消灯を実機確認） |
| Phase 6 | 夜の見た目調整（露出・雲の月光・水面） | 夜の品質向上 | ✅ ほぼ完了（2026-07-19。水面の実機確認のみ残） |

**Phase 6 の実装メモ（2026-07-19）**:
- 露出: 自動露出は既存実装（Krawczyk キー＋Min/Max EV クランプ）があったため、**上限 EV の既定を 4→8 へ拡大**（月夜の平均輝度 ~0.001 を持ち上げるには約+5EV 必要。UI スライダー範囲も ±12 へ）。既定 OFF は維持し、**大気エディタ「夜（満月）」プリセットが自動露出を有効化**する（ToneMapping へ PostEffectManager 経由でアクセス）
- **解析的地面フィルの定数「常夜灯」床（0.03）を廃止**し、Sky-View LUT（前乗算済み）の高仰角サンプル × 0.03 の「空追従アンビエント」へ変更。旧実装は夜も明るいベージュ帯が地平線に残り、①夜景の不自然な明帯 ②自動露出の平均測光を持ち上げて夜が黒いまま、の2つの問題を起こしていた（ユーザー報告の「地平線の明るい帯」の一因もこれ）
- 雲の月光直接照明: `SunLuminanceAt` を `DirectLightLuminanceAt(ライト方向・色・強度)` に一般化し、太陽＋月（hasMoon 時のみ）を合算。CloudRayMarch.CS / CloudCubemapCapture.CS の両方。雲 CB は 224→**256B**（moonDirection/moonIntensity/moonColor/hasMoon 追加。C++ static_assert 更新済み）。月有効時はライトマーチ約2倍（夜間限定コスト）
- 月の直接光既定値: 0.1→**0.002**（太陽の空:直接光比 20:1.75≈0.0875 を月の空 0.02 へ適用）。人為的に盛ると自動露出下で「空だけ暗く床だけ明るい」不整合になる（実機で確認済み）。強度スライダーはログスケール化
- **シェーダー反映の落とし穴**: ビルド後に .hlsl を編集した場合、ランタイムコンパイルは出力ディレクトリのコピーを読むため反映されない。`generated/CoreEngine/outputs/Debug/Engine/Assets/Shaders/` へ手動コピー＋再起動が必要
- **照明駆動EV（2026-07-19 追加）**: 画面平均測光は構図で露出がポンピングする（地面/空を見るだけで明るさが変わる）ため、測光の既定を「照明駆動（大気連動）」へ変更。`AtmosphereManager::ComputeSceneIlluminationLuminance()` が太陽・月の高度・強度からシーン代表輝度を解析（薄明は実測カーブの log-linear 近似 10^(0.36×高度deg) で減衰・昼30°で旧画面平均の実測 1.4 と一致する校正係数 0.127・星明かり床 2e-4）し、EnvironmentFeature が毎フレーム ToneMapping へプッシュ。カメラの向きに露出が依存しなくなる（UE 実務の「Manual＋時刻ベース露出カーブ」相当）。画面平均測光は UI で選択可能・照明供給が無いシーンでは自動フォールバック。検証: 太陽方位の大回転（構図激変）で自動EV変化 0.07 のみ
- 水面の実機確認（2026-07-19）: WaterTestScene に一時コード（夜の太陽・満月ライト・自動露出ON）を入れて確認し、
  **月光の夜空・星空が水面世界で描画され、水面が月明かりで明るくなる**ことを実機確認した（確認後に一時コードは全て削除済み）。
  月ディスクそのものの水面反射のスクリーンショットはカメラ姿勢の自動操作制約で未取得だが、
  反射ビューは太陽ディスクと同一経路（ReflectionView の SkyBoxQueuePass が SkyAtmosphere.PS を描く）のため構造上同様に映る

**Phase 2-4 の実装メモ（2026-07-18）**:
- `DirectionalLightData.isAtmosphereMoon`（sunPadding の1バイトを転用・48B維持）＋ `LightManager::GetAtmosphereMoonLight()`（フォールバック無し・両フラグ時は太陽扱い）
- 月の Transmittance on Light は Phase 2 で配管済み（`ComputeSunTransmittanceCPU` を `ComputeLightTransmittanceCPU(方向)` へ一般化して太陽・月で共用）
- **`LightManager::Initialize` で directionalLights_ 等を最大数 reserve**: 月ライト追加時の vector 再確保で LightingFeature 等が保持するポインタがダングリングする潜在バグの回避（必須）
- CB は 208→256B（月ブロック追加）。C++/HLSL の static_assert を両方更新済み
- 月ディスクの満ち欠け: ディスク内ローカル座標→月面球法線→太陽方向ランバート（+地球照フロア0.02）。周縁減光なし・グレアは太陽の約1/10
- **満ち欠けは既定OFF＝常に満月（2026-07-18 ユーザー指定）**: `AtmosphereParameters::moonPhaseFromSun`（CB `moonPhaseFromSun`）。ゲームでは太陽と月を独立に動かすため、実位相だと配置次第で意図せず新月（ほぼ見えない月）になる。実位相はUI「満ち欠けを太陽と連動（実位相）」でオプトイン
- **月夜の地平線が明るく暖色になるのは正しい挙動**: 光路の長い低空は散乱が多く（明るい）、青が先に減衰する（暖色化）。昼の「地平線際が白っぽい」のと同じ物理で、月が高いほど強まる。バグではないので「修正」しないこと
- UI: 大気エディタ「月」セクション（有効トグル・高度/方位/強度（空・直接光）/月光色）。月ライトは初回有効化時に facade が生成
- 既定の月光強度（空0.02）は物理準拠で**露出補正なしではほぼ黒**（実測: 夜空ピクセル値 R0G1B1）。夜を見せるには強度を上げるか Phase 6 の露出クランプが必要（想定どおり）

Phase 1 が全体の土台。**Phase 1 だけは「見た目が変わらないこと」が合格条件**なので、
必ず単独で検証してから Phase 2 へ進むこと。

---

## 4. Phase 1: Sky-View LUT の絶対方位化＋ライト色前乗算

### 4.1 目的

現行の Sky-View LUT には月を入れられない前提が2つある:

1. **太陽相対方位パラメータ化**: `SkyViewParamsToUv` の U 軸は「太陽との相対方位角の余弦」
   （対称性で半周分のみ・√で太陽側に解像度集中）。太陽と月は方位が違うため、
   2光源の寄与を合算した瞬間に対称性が壊れて破綻する。
   → UE と同じ**絶対方位（360°・線形）**へ変更する。
2. **色の後乗算**: LUT には無色の輝度が入っており、消費者がサンプル後に
   `sunColor × sunIntensity` を掛けている。2光源では色・強度がライトごとに違うため、
   **積分時にライト色を前乗算**して LUT を「最終放射輝度」ドメインへ変える。

### 4.2 新しいパラメータ化

```hlsl
// U 軸: 視線の絶対方位角 φ = atan2(dir.z, dir.x) ∈ [-π, π] を [0,1] へ線形マッピング。
// ±π の継ぎ目はサブUVエンコードで連続化する:
//   端テクセル（x=0 と x=W-1）の中心が unit 0 と 1（＝どちらも方位 ±π で同一方向）に
//   正確に載るため、CLAMP サンプラーのまま継ぎ目がバイリニアで連続になる。
float FromUnitToSubUvs(float u, float resolution)  { return (u * (resolution - 1.0f) + 0.5f) / resolution; }
float FromSubUvsToUnit(float u, float resolution)  { return (u * resolution - 0.5f) / (resolution - 1.0f); }
```

- V 軸（天頂角。地平線上下分割＋√エンコード）は**変更しない**
- 視線がほぼ真上/真下（水平成分 ≈ 0）のときは φ=0 とする（その行は全列同値に収束するので任意でよい）
- 方位角ヘルパー `SkyViewAzimuth(float3 dir)` を AtmosphereCommon.hlsli に追加し、
  全消費者の「太陽相対方位余弦の計算ブロック」を置き換える

**解像度に関する注意**: 従来は半周 192 テクセル（太陽側に√集中）、変更後は全周 192 テクセル
（約1.9°/テクセル）。太陽周りのミーのにじみが方位方向にわずかに鈍る可能性があるが、
UE も同条件（192 幅・線形方位）で出荷している。目視で気になる場合のみ
`kSkyViewLUTWidth` を 256 へ上げる（C++ とHLSL の両定数を同時に変更）。

### 4.3 色前乗算

- `SkyViewLUT.CS.hlsl`: 出力を `luminance × gAtmosphere.sunColor × gAtmosphere.sunIntensity` にする
- `CameraVolumeLUT.CS.hlsl`: rgb（inscattering）のみ前乗算。**a（平均透過率）には掛けない**
- §2.1 の表にある全消費者からサンプル後の色乗算を削除する
- `SkyAtmosphere.PS.hlsl` の太陽ディスク・グレアは LUT 由来でないため、
  `sunRadiance` の計算時に明示的に `sunColor × sunIntensity` を掛ける
  （解析的地面フィルは従来から独立スケールなので変更不要）
- 雲の2箇所は `gCloud.sunColor` を掛けていたが、値の出所は同じ大気太陽ライトなので
  前乗算済み LUT に置き換えても等価

### 4.4 C++ 側: ダーティ検知の拡張（重要）

LUT に色が焼き込まれるため、**太陽の色・強度変更でも Sky-View 再生成が必要になる**
（従来は方向とカメラ高度のみ検知。色はサンプル時乗算だったので再生成不要だった）。

`AtmosphereManager::Update()` の `sunChanged` 判定に `sunColor_` / `sunIntensity_` の
変化検知を追加し、キャッシュ（`lastSunColor_` / `lastSunIntensity_`）を header に足す。
これを忘れるとエディタで太陽色を動かしても空が変わらないバグになる。

なお Transmittance on Light は authored 値を変調した「GPU 転送コピー」にのみ掛かり、
`sunColor_` は変調前の値のままなのでフィードバックループは起きない（従来どおり）。

### 4.5 雲の固定方位サンプルの扱い

`CloudRayMarch.CS.hlsl` / `CloudCubemapCapture.CS.hlsl` は Sky-View を
「太陽から 90° の固定方位」でサンプルしている（√エンコードの太陽子午線での不安定回避＋
仰角が支配的という理由。既存コメント参照）。絶対方位化後は
`φ = SkyViewAzimuth(toSun) + π/2` として**同じ挙動を維持**する。
（線形方位化で√の不安定性は消えるため、将来は実方位に戻す改善余地があるが Phase 1 ではやらない）

### 4.6 検証（合格条件: 見た目が変わらないこと）

1. C++ ビルド → AtmosphereTestScene 起動
2. 昼（高度 30°）・夕（5°）・日没直後（-5°）でスクショを撮り、変更前と比較
   - 空のグラデーション・太陽ディスク・雲・水面・AP・地面の明るさが一致すること
   - 特に **±π 継ぎ目（太陽の真後ろの方位）に縦筋が出ない**こと
3. エディタで太陽の色・強度を動かし、空が即追従すること（§4.4 の検証）
4. 空アンビエント・スペキュラIBL（モデルの映り込み）が従来と一致すること

---

## 5. Phase 2: 月ライトの配管

### 5.1 ライトデータ

- `DirectionalLightData` に「大気ライトインデックス」を追加する。
  既存 `isAtmosphereSun` と同様 **GPU padding 領域**へ配置し、48B ストライドと
  static_assert を維持する（空きビットが無い場合は `isAtmosphereSun` を
  `atmosphereLightIndex`（0=なし/1=太陽/2=月）へ置き換える形でも良い。
  その場合は既存の全参照箇所を同時に更新すること）
- `LightManager::GetAtmosphereMoonLight()` を追加（`GetAtmosphereSunLight()` と対）

### 5.2 AtmosphereManager / CB 拡張

`AtmosphereShaderConstants` / `AtmosphereConstants` へ追加（**両者一致・static_assert 更新**。
HLSL 側は AtmosphereCommon.hlsli の1箇所なので全シェーダーへ自動伝播する）:

```cpp
Vector3 moonDirection;   float moonIntensity;      // 月光の進行方向 / 強度
Vector3 moonColor;       float moonDiskHalfAngleRad; // 月光色 / 月の視半径 [rad]
float moonDiskLuminanceScale; float hasMoon;       // ディスク輝度 / 月有効フラグ(0/1)
float nightPad[2];
// → 208B + 48B = 256B。static_assert を 256 に更新
```

- `AtmosphereManager::Update()` で `GetAtmosphereMoonLight()` から月情報を取得
- ダーティ検知に月方向・色・強度を追加（Phase 1 §4.4 と同じ構造）
- 月ディスク用パラメータを `AtmosphereParameters` に追加（§10 の既定値）

### 5.3 エディタ UI

大気エディタ（AtmosphereEditorFacade）に「月」セクション: 有効トグル・高度角・方位角・
色・強度・ディスク視半径・ディスク輝度スケール。太陽セクションの実装を流用する。

### 5.4 検証

ビルドが通り、月を有効にしても**まだ何も変わらない**こと（配管のみ）。
ImGui で月パラメータが AtmosphereManager まで届いていることをログ等で確認。

---

## 6. Phase 3: 月の散乱寄与・直接光・アンビエント

### 6.1 LUT の2光源積分

- `SkyViewLUT.CS.hlsl` / `CameraVolumeLUT.CS.hlsl`:
  ```hlsl
  float3 lum = IntegrateScatteredLuminance(..., toSun, ...) * sunColor * sunIntensity;
  if (hasMoon > 0.5f) {
      lum += IntegrateScatteredLuminance(..., toMoon, ...) * moonColor * moonIntensity;
  }
  ```
  Transmittance / MultiScattering LUT はライト非依存なので**そのまま共用**
  （`SampleTransmittanceToSun` / `SampleMultiScattering` に toMoon を渡すだけで正しく動く）
- コスト: 月有効時に LUT 生成コストが約2倍になるが、LUT 生成は変化時のみ＋
  低解像度（192×108 / 32³）なので許容範囲。
  **注意**: 「月が地平線下なら積分スキップ」という早期カットは入れないこと。
  地平線下の光源でも高高度の大気は照らされる（薄明）ため、カットすると月の
  薄明グラデーションが境界でポップする。スキップは `hasMoon=0`（無効/強度0）のみ

### 6.2 月の直接光

- `ComputeSunTransmittanceCPU()` を月方向でも呼び、月ライトの GPU 転送コピーへ
  Transmittance on Light を適用（月の出・月の入りで月光が赤くなる）。
  太陽と同じく authored 値は書き換えない
- DeferredLighting は既存の複数ディレクショナルライト経路で月が自動的にモデルを照らす
  （これが「モデル真っ黒」の直接の解消点）。月の影はディレクショナルシャドウの
  既存仕組みに任せる（対応していなければスコープ外）

### 6.3 アンビエント・IBL

`SkyIrradianceSH.CS` / `SkyEnvironmentCapture.CS` は前乗算済み Sky-View LUT を読むだけなので、
**Phase 1 が済んでいれば変更不要**で月光アンビエント・月光IBLが自動的に効く。

### 6.4 検証

- 太陽 -10°・月 30° で: 空が月光で薄青く光る・モデルに月の直接光と青いアンビエントが乗る・
  水面に月光の空が映る
- 月も -10° にすると全てがほぼ黒に戻る（星・露出は後続フェーズ）
- 昼に月を有効にしても見た目がほぼ変わらない（太陽が3〜4桁明るいため）

---

## 7. Phase 4: 月ディスク描画

`SkyAtmosphere.PS.hlsl` の太陽ディスクと同じ解析的手法で月ディスクを追加:

- 視半径 `moonDiskHalfAngleRad`（既定 0.259°）・縁の fwidth AA・
  `SampleTransmittanceToSun`（月方向）による透過率変調・`PlanetSunVisibility` で地平線下へ消失
- **周縁減光は掛けない**（月は反射体なので太陽と逆に縁が暗くならない。むしろ一様に近い）
- グレアは太陽の係数を大幅に弱める（月光は空の散乱に埋もれない程度の控えめなにじみ）
- 任意拡張（満ち欠け）: ディスク内のローカル座標から月面の球法線を復元し、
  `saturate(dot(moonSurfaceNormal, toSun))` のランバート陰影を掛けるだけで
  太陽・月の位置関係から位相が自動再現される。月アルベドテクスチャはさらにその先の拡張

検証: 月の高度・方位を動かしてディスクが追従・月の入りで赤く沈む・
（位相実装時）太陽と月の相対位置で満ち欠けが変わること。

---

## 8. Phase 5: 星空（任意）

UE 同様、大気の「外側」の背景として合成する。`SkyAtmosphere.PS.hlsl` にて:

- 星ソース: 手続き的星field（方向ベクトルのハッシュで疎な点光source を生成）または
  星空キューブマップ（アセット追加が必要）。手続き的が手軽
- 合成: `luminance += starColor × 視線方向の大気透過率 × 空の暗さマスク`
  - 視線透過率は Sky-View LUT の α に積分時の throughput 平均を格納して取得するのが正道
    （現在 α=1 固定。CameraVolume と同じ方式）
  - 空の暗さマスク: 空の輝度が高いほど星を消す（昼・薄暮で自然に消える）。
    `saturate(1 - luminance輝度 / しきい値)` の美術値で良い
- 星も空ドメインの輝度なので露出・トーンマップは自動で整合する

**実装メモ（2026-07-19 完了）**:
- `AtmosphereCommon.hlsli` に `ComputeStarLuminance(dir, pixelAngleRad, starIntensity)` を実装。
  CB は旧 `nightPad` を `starIntensity`（0=無効、既定 1.0）へ転用し 256B 不変
- **セル分割はキューブ面ごとの2Dグリッド**（`StarFaceIndex`/`StarFaceToDir`、96×96×6面 ≈ 5.5万セル）。
  3Dセル＋normalize 方式は星（ガウス点像）の中心が隣セルの角度領域へ射影されて点像が欠ける／
  消えるため不採用。星のセル内位置を [0.3, 0.7] に制限して境界欠けを防ぐ
- ハッシュは Dave Hoskins hash33。存在確率 0.22・等級分布 `0.03+0.97×pow(h,7)`・
  色温度はオレンジ〜青白の lerp。ピーク輝度 `STAR_LUMINANCE_SCALE=0.10`（夜の自動露出下で
  最輝星が白飛び＝実際の星の見え方。初版 0.03/pow10 は「まばらで暗い」見た目だった）
- **点像はピクセル角（`length(fwidth(viewDir))`）へ半径を広げた分だけピーク輝度を下げて
  エネルギー保存**（解像度・FOV 非依存。低解像度キャプチャで星が明滅しない）
- 視線透過率は Sky-View α ではなく **Transmittance LUT を (r, 視線天頂角余弦) でそのまま
  サンプル**（太陽用パラメータ化が視線にも使える。地平線際の減光・赤化が物理的に出る）
- 暗さマスクは `saturate(1 - 空輝度/0.02)`。ディスク・グレア込みの輝度に掛けるため
  太陽・月ディスクを星が貫通しない。実機確認: 夜=星あり（青白/暖色混在・輝度階調あり）、
  正午=孤立輝点 0 個
- **SkyEnvironmentCapture（スペキュラIBL）には意図的に入れない**: 太陽ディスク除外と同じ方針
  （微小輝点はプリフィルタでファイアフライ化する）。水面の鏡面反射は平面反射が
  SkyAtmosphere.PS を描くため自動で星が映る
- UI: 大気エディタ「大気パラメータ →星空→ 星の強度」スライダー（0〜5）

---

## 9. Phase 6: 夜の見た目調整

1. **自動露出の Min/Max EV クランプ（必須）**: 現状の自動露出は暗いシーンを際限なく
   持ち上げるため、夜が昼のように明るくなる。`ToneMapping` に
   「自動露出の補正範囲 [EV]」（既定 ±4 程度・UI スライダー）を追加し、
   Krawczyk キー計算後の露出値をクランプする
2. **雲の月光対応**: `CloudCommon.hlsli` の CB に月方向・色・強度を追加し、
   `CloudRayMarch.CS` の直接光ループを2光源化（アンビエントは前乗算 LUT 経由で自動）
3. **水面**: 平面反射・スペキュラIBLは自動追従。月の水面反射（ReflectionView の
   SkyBoxQueuePass 解析ディスク）は Phase 4 のディスクが反射カメラでも描かれれば自動
4. ゴッドレイ・レンズフレアの月対応はスコープ外（必要になったら別計画）

---

## 10. 新規パラメータ既定値一覧

| パラメータ | 既定値 | 根拠 |
|---|---|---|
| `moonDiskAngularRadiusDeg` | 0.259° | 実際の月の平均視半径 |
| `moonIntensity`（空） | 0.02（美術値。sunIntensity=20 の 1/1000） | 物理比（1/40万）では見えない。UE 同様の美術値＋露出で夜を作る |
| 月の直接光強度（サーフェス） | 0.1（太陽 1.75 と同単位） | 露出クランプ導入前でも夜のモデルがうっすら見える美術値 |
| `moonColor` | (0.55, 0.65, 0.85) | 知覚的な青白い月光（物理的には約4100Kの暖色だが、プルキンエ効果による青の知覚をゲーム慣習どおり色で表現） |
| `moonDiskLuminanceScale` | 25.0 | moonIntensity=0.02 でディスク輝度 ≈ 0.3 ＝ 昼の空（≈1）の約1/3。実際の満月面輝度（約2500 cd/m²）と同比率で、夜は白い円盤・昼は空に埋もれる |
| 自動露出 Min/Max EV 補正幅 | ±4 EV | 夜を「暗いが見える」に留めるクランプ |

---

## 11. 共通の検証ワークフロー

`Docs/Engine/Graphics/Rendering/VolumetricCloud_Design.md` §10 と
メモリ `game-verification-workflow` に従う（WMI デタッチ起動・ウィンドウ限定キャプチャ・
ImGui は PostMessage 座標=スクショ座標÷1.25）。

- シーン: AtmosphereTestScene（太陽高度スライダー -20°〜90°）・WaterTestScene（水面確認）
- 昼/夕/夜の3高度 × 変更前後の比較スクショを基本とする

---

## 12. 落とし穴チェックリスト

- [ ] `AtmosphereConstants` を変えたら C++ / HLSL 両方＋static_assert を更新したか
      （HLSL は AtmosphereCommon.hlsli の1箇所。Water.PS・雲・ゴッドレイも同 CB を読む）
- [ ] CameraVolume LUT の α（透過率）に色を掛けていないか
- [ ] LUT 前乗算後、太陽/月の**色・強度変更のダーティ検知**を入れたか（§4.4）
- [ ] 雲の固定方位サンプルを「太陽方位+90°」で維持したか（§4.5）
- [ ] Sky-View の ±π 継ぎ目に縦筋が出ていないか（サブUVエンコード漏れ）
- [ ] LUT サンプラーは CLAMP のままか（WRAP 禁止は従来どおり。継ぎ目はサブUVで解決する）
- [ ] Transmittance on Light は GPU 転送コピーのみ変調・大気 CB へは変調前の値か（月も同様）
- [ ] `DirectionalLightData` の 48B ストライドと static_assert を維持したか
- [ ] 「月が地平線下なら積分カット」を入れていないか（薄明が壊れる。§6.1 の注意参照）
- [ ] 自動露出クランプを入れる前に「夜が明るすぎる」を大気側パラメータで調整しようとしていないか
