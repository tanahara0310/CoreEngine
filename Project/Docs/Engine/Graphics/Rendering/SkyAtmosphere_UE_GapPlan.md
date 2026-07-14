# Sky Atmosphere UEギャップ解消 設計書

CoreEngine の大気散乱システム（Bruneton/Hillaire 方式、実装済み）を、アンリアルエンジンの
SkyAtmosphere コンポーネントの機能・品質へ近づけるための「足りないものリスト」と実装計画。

**この文書単体で実装を完遂できること**を目標に書かれている。実装 AI（または人間）は、
「実装フェーズ計画」に従い、**フェーズごとにビルド → 起動 → 検証**を行ってから次へ進むこと。
フェーズ間に依存はあるが（特に Phase 1→2→3 は輝度単位の整理として連続している）、
各フェーズ単体でも価値が出るように分割してある。

- 作成日: 2026-07-14
- 対象ブランチ: `fix/sky-atomsphere`（大気散乱・Volumetric Cloud・ゴッドレイ・レンズフレア実装済みが前提）
- 最終ゴール: **UE の SkyAtmosphere と同等の「時刻に整合したシーン全体のライティング」と、
  大気圏外からの惑星ビュー**を実現すること

---

## 目次

1. [現状と UE のギャップ一覧](#1-現状と-ue-のギャップ一覧)
2. [前提となる既存実装（必読ファイル）](#2-前提となる既存実装必読ファイル)
3. [Phase 1: 太陽直接光の大気透過率連動](#3-phase-1-太陽直接光の大気透過率連動)
4. [Phase 2: 露出制御と輝度単位の整理](#4-phase-2-露出制御と輝度単位の整理)
5. [Phase 3: 大気連動アンビエント（Sky Light 相当）](#5-phase-3-大気連動アンビエントsky-light-相当)
6. [Phase 4: 散乱積分の品質改善](#6-phase-4-散乱積分の品質改善)
7. [Phase 5: 大気圏外カメラ・惑星ビュー](#7-phase-5-大気圏外カメラ惑星ビュー)
8. [Phase 6: Aerial Perspective 拡張](#8-phase-6-aerial-perspective-拡張)
9. [スコープ外・将来拡張](#9-スコープ外将来拡張)
10. [共通の検証ワークフロー](#10-共通の検証ワークフロー)
11. [落とし穴チェックリスト](#11-落とし穴チェックリスト)
12. [新規パラメータ既定値一覧](#12-新規パラメータ既定値一覧)

---

## 1. 現状と UE のギャップ一覧

すでに実装済みのもの: LUT 4種（Transmittance / MultiScattering / SkyView / CameraVolume）、
太陽ディスク＋周縁減光＋グレア、地球影（`PlanetSunVisibility`）、オゾン吸収、
Aerial Perspective（froxel、不透明のみ）、変化検知による LUT 再生成、環境エディタ統合。

| # | ギャップ | UE での対応物 | 効果 | 工数 | フェーズ |
|---|---|---|---|---|---|
| G1 | 太陽直接光が大気透過率に連動しない（日没後も地表が昼の明るさ） | Atmosphere Sun Light の "Transmittance on light color" | ★★★ | 小 | Phase 1 |
| G2 | 露出制御が無い（固定スケール直結。夕暮れの反太陽側が明るく見える主因） | Auto Exposure / EV100 物理単位 | ★★★ | 中 | Phase 2 |
| G3 | アンビエント・IBL が時刻に追従しない（固定の既定アンビエント） | Real-Time Capture の Sky Light | ★★★ | 中 | Phase 3 |
| G4 | 多重散乱のスケール調整ノブが無い | MultiScatteringFactor | ★☆☆ | 極小 | Phase 4 |
| G5 | 積分がエネルギー保存でない・固定40ステップ（地平線際の精度不足） | 解析的ステップ積分＋距離比例サンプル数 | ★★☆ | 小 | Phase 4 |
| G6 | 大気圏外カメラ非対応（半径クランプ・Y軸天頂固定・惑星地表なし） | 宇宙からの惑星レンダリング（ピクセル単位レイマーチ） | ★★☆ | 大 | Phase 5 |
| G7 | AP が半透明に効かない・128km 固定 | 半透明への AP 適用・View Distance Scale | ★★☆ | 中 | Phase 6 |
| G8 | 月（第2大気ライト）・星空が無い | Second Atmosphere Light | ★☆☆ | 中 | 将来 |
| G9 | 大気散乱への影（Atmosphere Shadow）が無い | Cast Shadows on Atmosphere | ★☆☆ | 大 | 将来（ゴッドレイで代替中） |

**「夕暮れの反太陽側が現実より明るい」問題は G1＋G2＋G4＋G5 の複合**。
G2（露出）が知覚的に最大の要因で、G5（積分精度）が地平線際の物理的な誤差要因。

---

## 2. 前提となる既存実装（必読ファイル）

| ファイル | 関係 |
|---|---|
| `Engine/Src/Graphics/Atmosphere/AtmosphereManager.h/.cpp` | 中核。太陽情報の取得（`Update()` 内 `GetAtmosphereSunLight()`）、CB アップロード（`UploadConstants()`）、カメラ半径クランプ（`Update()` の `distanceFromPlanetCenter_`）、ダーティフラグ管理 |
| `Engine/Assets/Shaders/Atmosphere/Common/AtmosphereCommon.hlsli` | `AtmosphereConstants`（C++ 側 `AtmosphereShaderConstants` と一致必須）、`IntegrateScatteredLuminance`、`SampleTransmittanceToSun`、`PlanetSunVisibility`、LUT パラメータ化 |
| `Engine/Assets/Shaders/Atmosphere/SkyViewLUT.CS.hlsl` | 空の積分（固定40ステップ）。Phase 4/5 の主対象 |
| `Engine/Assets/Shaders/Atmosphere/SkyAtmosphere.PS.hlsl` | 本番描画。太陽ディスク・解析的地面フィル。Phase 5 の主対象 |
| `Engine/Assets/Shaders/Atmosphere/MultiScatteringLUT.CS.hlsl` | 多重散乱 LUT。Phase 4 で積分式を差し替え |
| `Engine/Src/Graphics/Light/LightData.h` | `DirectionalLightData`（**48バイト固定・static_assert あり・空きフィールド無し**） |
| `Engine/Src/Graphics/Light/LightManager.h/.cpp` | `GetAtmosphereSunLight()`、`UpdateAll()` → `bufferManager_.UpdateBuffers()` が GPU アップロード経路 |
| `Engine/Src/Graphics/PostEffect/Effect/ToneMapping/*` + `Engine/Assets/Shaders/PostEffect/Effect/ToneMapping/ToneMapping.CS.hlsl` | ACES トーンマッパ。Phase 2 で露出を追加 |
| `Engine/Assets/Shaders/PostEffect/DeferredLighting.PS.hlsl` | サーフェスライティング。Phase 3 でアンビエント差し替え |
| `Docs/Engine/Graphics/Rendering/VolumetricCloud_Design.md` §10 | ビルド・実行・検証の共通手順（本書はこれを参照） |

**シェーダーはランタイム DXC コンパイル**のため .hlsl 編集はリビルド不要。C++ 変更時のみビルド。

---

## 3. Phase 1: 太陽直接光の大気透過率連動

**✅ 実装済み（2026-07-14）** — `AtmosphereManager::ComputeSunTransmittanceCPU()` /
`LightManager::SetAtmosphereSunTransmittance()` / `GetEffectiveLightColorRGB()`。
検証済み: 天頂で透過率 (0.938, 0.865, 0.761)・高度 5.5° で (0.546, 0.265, 0.075) と地表のオレンジ照明・
地平線下 -7° で (0,0,0) と直接光消灯・トグル OFF で従来動作へ復帰。

### 目的

`isAtmosphereSun` のディレクショナルライトの実効色・強度を、毎フレーム
`Transmittance(地表, 太陽方向) × PlanetSunVisibility` で変調する。
昼は白いまま、夕方は自動的にオレンジ→赤、日没後はゼロになる。

ディレクショナルライトを太陽として使う方式自体は UE と同じであり変更しない。
足りないのは「大気 → ライト」の逆方向フィードバックだけ。

### 設計方針（重要）

- **authored なライトデータ（`directionalLights_` 配列）は書き換えない。**
  書き換えると翌フレームに `AtmosphereManager` が減衰済みの色を読み、
  フィードバックループで光が消えていく。変調は **GPU アップロード時のコピーに対してのみ**行う。
- **大気側 CB（`sunColor_` / `sunIntensity_`）には変調前の値を渡し続ける。**
  空・太陽ディスク・雲は LUT 経由で既に透過率が掛かっている。二重適用禁止。

### 実装手順

1. **CPU 側透過率計算** — `AtmosphereManager` に追加:
   ```cpp
   // AtmosphereCommon.hlsli の ComputeMediumDensity / ComputeExtinction /
   // PlanetSunVisibility を C++ に忠実に移植する（単位は km / 1_km）
   Vector3 ComputeSunTransmittanceCPU() const;   // 40ステップのレイマーチ
   Vector3 GetSunTransmittance() const { return sunTransmittance_; }
   ```
   - 評価点: 地表 `(0, planetRadiusKm + 0.002, 0)`（地表 +2m。カメラ高度でなく地表基準。
     シーンのオブジェクトを照らす光なので地表で評価するのが正しい）
   - 方向: `toSun = -sunDirection_`
   - 惑星ヒット判定は使わず `PlanetSunVisibility`（太陽視半径ぶんの smoothstep）を乗算して
     地平線通過を滑らかにする（HLSL 側と同じ理由）
   - `Update()` 内、太陽情報取得後に毎フレーム計算（40ステップ × 1回。コストはマイクロ秒未満）
2. **LightManager への伝搬**:
   ```cpp
   // LightManager に追加
   void SetAtmosphereSunTransmittance(const Vector3& t);  // 既定 {1,1,1}
   ```
   `AtmosphereManager::Update()` の末尾から `lightManager->SetAtmosphereSunTransmittance(...)` を呼ぶ。
   大気非アクティブなシーンでは `{1,1,1}` のまま（フレーム有効化パターンと同様、
   `EngineSystem` のリセット箇所で毎フレーム `{1,1,1}` に戻すのが安全）。
3. **アップロード時の変調** — `LightManager::UpdateAll()`（または `bufferManager_.UpdateBuffers` 直前）で、
   `isAtmosphereSun` のライトだけ GPU 転送用コピーの `color.rgb *= transmittance` を適用する。
   `DirectionalLightData` は 48 バイト固定（static_assert）で空きフィールドが無いため、
   透過率は **LightManager のメンバ変数**として持つこと。構造体には追加しない。
4. **CPU 直読みの追随** — `GetAtmosphereSunLight()` を直接読んで色・強度を使う箇所
   （水面コースティクス等。`grep GetAtmosphereSunLight` で全列挙すること）には
   `LightManager::GetEffectiveSunColor()`（変調済みの色を返すヘルパー）を追加して切り替える。
   方向は変調しないので direction 参照はそのままでよい。
5. **ImGui** — 大気エディタ（Environment ツリー）に:
   - `Transmittance on Light` トグル（既定 ON）
   - 現在の透過率 RGB の読み取り専用表示（デバッグ用）

### 検証

`AtmosphereTestScene` で太陽高度を動かしながらスクリーンショット（§10 の手順）:

- [x] 太陽天頂: 地表・オブジェクトの色がほぼ従来どおり（実測 (0.938, 0.865, 0.761)）
- [x] 太陽高度 5.5°: 地表がオレンジ色に照らされ、空の色と整合する（実測 (0.546, 0.265, 0.075)）
- [x] 太陽が地平線下 -7°: 直接光がゼロ（実測 (0,0,0)。地面が暗転、残照のみ）
- [ ] 水面シーン: コースティクス・反射ハイライトも夕方に赤くなる（コードは実効色へ切替済み・目視未確認）
- [x] トグル OFF で従来の見た目に完全に戻る（日没後も地面が明るい旧動作を再現できることを確認）
- [ ] キューブマップ空のシーン（大気非アクティブ）でライトが変調されない（設計上保証・目視未確認）

---

## 4. Phase 2: 露出制御と輝度単位の整理

**✅ 2a 実装済み（2026-07-14）** — `ToneMapping::SetExposureEV()`（`ScreenParams` の pad 領域へ
`exposureEV` を追加、ACES 前に `exp2(EV)` 乗算）。UI は Engine Settings → Post Effects →
ToneMapping の「露出補正 [EV]」スライダー＋リセットボタン。
検証済み: EV=0 で従来と完全一致・夕暮れ（太陽高度 0.2°）で EV -3 にすると太陽周辺だけが輝き
雲がシルエットになる現実的な薄暮になる。

**✅ 2b 実装済み（2026-07-14）** — 自動露出。`LuminanceReduction.CS.hlsl`（64×64 グリッド 4096 点の
対数輝度を 1 グループの groupshared 縮約で 1 要素バッファへ）→ 3 スロットのリードバックリングで
CPU が 2 フレーム遅れで読み取り → 指数的時間順応 → `autoEV = clamp(log2(key/順応輝度), min, max)`。
手動 EV は加算オフセットに変わる。既定 OFF（オプトイン）。UAV は Root Descriptor バインドで
ディスクリプタヒープ不使用。検証済み: 太陽 30° で自動EV -2.60（白飛びしていた昼空が青空に）、
太陽 -8.9° で自動EV +4.00（上限。夜が「目が順応した暗さ」になり残照が見える）。
太陽位置の変更だけで露出が計算追従することを確認。

### 目的

現状は `sunIntensity=20`（空）、`intensity=1.75`（直接光）、`kGroundLightScale=5`（地面フィル）と
アドホックなスケールが混在し、トーンマッパに固定スケールで直結している。
このため「太陽側が眩しいから反対側が暗く見える」という現実の知覚（＝露出）が再現されない。
**夕暮れの反太陽側が明るすぎる問題の最大の要因はここ。**

### 実装手順（2a: 手動露出 — まずここまで）

1. `ToneMapping.CS.hlsl` の `ScreenParams` CB に露出を追加
   （`float2 pad` の空きに入る。C++ 側 `ToneMapping.cpp` の CB 構造体も一致させる）:
   ```hlsl
   float exposureEV;   // 露出補正 [EV]。0 = 現状維持
   float pad;
   ```
   ```hlsl
   color.rgb *= exp2(exposureEV);   // ACESFilm() の前に適用
   ```
2. ImGui（Engine Settings のポストエフェクト欄）に `Exposure (EV)` スライダー（-6〜+6、既定 0）。
3. **既定値校正**: EV=0 で昼のシーンが従来と同じ見た目になることを確認（回帰防止）。
   夕暮れシーンで EV を -2〜-3 に下げると反太陽側が現実的な暗さになることを確認する。

### 実装手順（2b: 自動露出 — オプション。2a の効果を見てから判断）

1. SceneColor の平均対数輝度を CS リダクションで計算（1/4 解像度に落としてから
   ログ平均 → 1×1 まで縮約。ヒストグラム方式は不要、ログ平均で十分）。
2. 時間適応: `adaptedLum += (targetLum - adaptedLum) * (1 - exp(-dt * speed))`（speed ≈ 1.5）。
3. `exposureEV = log2(key / adaptedLum)`（key ≈ 0.18）を minEV/maxEV（既定 -4〜+4）でクランプ。
4. 手動 EV は自動露出への補正オフセットとして残す。

### 検証

- [x] EV=0 で全シーンの見た目が変わらない（夕暮れシーンでリセット前後のスクリーンショット一致を確認）
- [x] 夕暮れ＋EV-3 で空が現実的な薄暮になる（太陽周辺のみ輝き、雲シルエット、地表暗転を確認）
- [x] （2b）太陽位置の変更に露出が滑らかに追従する（30°→自動EV-2.60 / -8.9°→+4.00 を確認。
      値は数秒で収束し振動なし。「太陽を画面に入れる/外す」での追従は視線変更の自動化が困難なため未検証だが
      計測は視線に写る輝度そのものなので同じ経路で動作する）

---

## 5. Phase 3: 大気連動アンビエント（Sky Light 相当）

**✅ 3a（拡散SH）実装済み（2026-07-14）** — `SkyIrradianceSH.CS.hlsl`（全球1024方向から Sky-View LUT を
サンプリングし、放射照度畳み込み済み SH9 係数を 9 要素 StructuredBuffer へ。Sky-View 再生成時のみ実行）。
`AtmosphereManager::GenerateSkyIrradianceSH()` / DeferredLighting.PS の `EvaluateSkyIrradiance()`
（t18 + b6 `gSkyAmbient`）。非IBL・大気アクティブ時にハーフランバートアンビエントを置き換える。
既定 ON・`skyAmbientScale` 既定 0.3。UI は Environment → Sky Atmosphere → 環境光。
検証済み: 昼=青みがかったアンビエント / 夕方=暖色で減光 / 夜=ほぼゼロ（完全な夜） /
OFF で従来アンビエントへ復帰。**3b（スペキュラIBL キューブマップ）は未実装**（将来拡張）。

### 目的

UE の Sky Light (Real-Time Capture) 相当。現状の固定 PBR アンビエントを、
Sky-View LUT から毎フレーム生成する球面調和（SH）アンビエントに置き換え、
昼は青みがかった環境光、夕方はオレンジ、夜はほぼゼロと時刻に追従させる。

### 実装手順

1. **SH 射影 CS** — `Engine/Assets/Shaders/Atmosphere/SkyIrradianceSH.CS.hlsl` 新規作成:
   - 1 スレッドグループのみで実行（64 スレッド程度で方向を分担 → groupshared で縮約）
   - 上半球 ＋ 下半球（地面アルベド × 天頂付近の空の平均、の近似でよい）を
     数百方向サンプリングし、2次 SH（9 係数 × float3）へ射影
   - 出力: 9 テクセルの `RWStructuredBuffer<float4>`（または 9×1 テクスチャ）
   - **落とし穴**: `RWStructuredBuffer` はシェーダーリフレクションの既知バグ
     （疑似 CBuffer として b0 衝突する）に注意。既存の修正済みパターンに従うこと
2. **実行タイミング** — `AtmosphereLUTPass` 内、SkyView LUT 生成の直後
   （`skyViewDirty_` のときのみ。コストは無視できる規模）。
3. **DeferredLighting 側** — SH バッファを SRV バインドし、既定アンビエントを差し替え:
   ```hlsl
   float3 ambient = EvaluateSH9(shCoeffs, normal) * skyAmbientScale;
   ```
   - 大気アクティブフラグを Lighting 側 CB に追加し、非アクティブなシーンでは
     従来の固定アンビエントへフォールバック（キューブマップ空シーン無影響の要件）
   - `skyAmbientScale` は空の輝度スケール（sunIntensity=20 系）とサーフェス直接光の
     単位差を吸収する変換係数。Phase 2 完了後に校正し、それまでは美術値
4. **ImGui** — `Sky Ambient` トグル＋`skyAmbientScale` スライダー。
5. **（3b・オプション）スペキュラ IBL** — 32³ キューブマップへ SkyView LUT を焼き、
   ミップへプリフィルタして環境反射に使う。工数が大きいので diffuse SH の効果を見てから。

### 検証

- [x] 昼: アンビエントが空由来の青みがかった色になる（従来の暖色ハーフランバートとの差を確認）
- [x] 夕方: アンビエントが暖色で減光する
- [x] 日没後: アンビエントがほぼゼロ（Phase 1+2 と合わせてシーン全体が完全な夜になる）
- [x] トグル OFF で従来アンビエントへ復帰する
- [ ] キューブマップ空シーンは従来アンビエントのまま（大気非アクティブ時 enabled=0 で設計上保証・目視未確認）
- [ ] 雲量を変えても破綻しない（雲は Sky-View LUT に含まれないため空の SH と雲の見た目に
      多少の乖離があるのは許容。気になる場合は将来 3b で解決）

---

## 6. Phase 4: 散乱積分の品質改善

**✅ 実装済み（2026-07-14）** — エネルギー保存ステップ積分（AtmosphereCommon.hlsli の両積分器＋
MultiScatteringLUT.CS.hlsl）、Sky-View の経路長比例ステップ数（16〜64、150km 基準）、
`multiScatteringFactor`（AtmosphereConstants 208 バイトへ拡張・既定 1.0・大気エディタ「多重散乱」スライダー）。

**関連する不具合診断（2026-07-14）**: 「自動露出でも地平線際の反太陽側が暗くならない
（太陽が沈み切ると正しい）」の主因は大気側ではなく**自動露出に2つの欠陥**があったため。

1. **固定キー正規化**: `autoEV = log2(0.18/順応輝度)` はどんな暗さのシーンも中間グレーへ
   持ち上げるため、薄暮の空が昼のような明るさになる。沈み切った後に正しく見えたのは
   必要 EV が上限クランプ（+4）を超えて正規化が効かなくなっていただけ。
   → **Krawczyk 2005 の自動キー**（`key = 1.03 − 2/(2 + log10(L+1))`。暗いシーンほど
   暗い出力へ写す）を導入（「明暗の絶対感を保持」トグル・既定 ON）。
2. **対数平均（幾何平均）測光**: 幾何平均はゼロ近傍ピクセルに過敏で、「真っ暗な地面＋
   薄暮の空」の構図で平均が桁違いに小さくなり、空だけがさらに数 EV 持ち上がっていた。
   → **線形輝度の算術平均**（カメラの平均測光相当）へ変更。高輝度は 64 でクランプして
   太陽ディスク数サンプルによる平均の支配を防ぐ。

修正後の実測（太陽高度 1.6°）: 太陽側ビュー autoEV -1.44（オレンジの夕焼け）、
反太陽側ビュー autoEV -1.17・キー 0.054（くすんだ暗い赤灰の空。修正前は同構図で
+2.5EV 前後＝約4EV明るい「昼のような空」だった）。地平線下 -3.6° は +4 クランプで
暗い薄暮のまま（従来どおり）。昼 30° は autoEV -2.87 で自然。

### 目的

地平線際の積分精度を上げ、多重散乱の調整ノブを追加する。
夕暮れの反太陽側の「物理的な」明るさ過剰に効く。

### 実装手順

1. **エネルギー保存型ステップ積分**（Hillaire 2020 §5.3。UE と同じ式）。
   `IntegrateScatteredLuminance` / `IntegrateScatteredLuminanceToDistance`
   （AtmosphereCommon.hlsli）と `IntegrateDirection`（MultiScatteringLUT.CS.hlsl）の
   ループ本体を以下へ差し替える:
   ```hlsl
   // 差し替え前: opticalDepth += extinction*dt; L += exp(-opticalDepth) * S * dt;
   float3 sigmaT = max(ComputeExtinction(heightKm, atm), 1e-7f);
   float3 stepTransmittance = exp(-sigmaT * dt);
   float3 S = transmittanceToSun * scattering + psiMs * scatteringNoPhase; // 放射源項
   float3 Sint = (S - S * stepTransmittance) / sigmaT;  // ステップ内解析積分
   luminance += throughput * Sint;
   throughput *= stepTransmittance;   // throughput = カメラまでの透過率（float3、ループ外で 1 に初期化）
   ```
   `IntegrateScatteredLuminanceToDistance` の `transmittanceOut` は最終的な `throughput` を返す。
2. **距離比例サンプル数** — SkyViewLUT.CS.hlsl の固定 `kStepCount = 40` を:
   ```hlsl
   int stepCount = (int)lerp(16.0f, 64.0f, saturate(tMax / 150.0f)); // tMax [km]
   ```
   地平線すれすれ（経路 1000km 超）で 64、天頂（〜80km）で 16 台になる。
3. **MultiScatteringFactor** — `AtmosphereConstants` 末尾に 16 バイト追加
   （**C++ 側 `AtmosphereShaderConstants` と両方**。サイズ検証があれば更新）:
   ```hlsl
   float multiScatteringFactor;  // 既定 1.0
   float3 constantsPad;
   ```
   全積分箇所で `psiMs * atm.multiScatteringFactor` として適用。
4. **ImGui** — `Multi Scattering` スライダー（0〜2、既定 1）。
   **変更時に LUT 再生成が走るよう `skyViewDirty_` 判定（および MS LUT の再生成条件）に
   パラメータ変更検知を追加すること**（既存はカメラ半径と太陽方向しか見ていない）。

### 検証

- [x] 天頂・昼の見た目がほぼ変わらない（昼 30° で青空・雲・地面とも正常。積分式変更の回帰なし）
- [x] 夕暮れの地平線グラデーションが滑らかになり、反太陽側の明るさを寄与スケールで直接調整できる
- [x] MultiScatteringFactor=0 で単一散乱のみになる（夕暮れ 0.4° で地平線に集中する高コントラストの空、
      =2 で全天がピンクに持ち上がることを確認。スライダー変更で LUT 再生成が正しく走る）
- [x] 雲・レンズフレアに回帰なし（夕暮れ・昼のスクリーンショットで正常描画を確認）。
      ゴッドレイは未確認（同 LUT 参照のため次回動作時に要確認）

---

## 7. Phase 5: 大気圏外カメラ・惑星ビュー

### 目的

UE のようにカメラを大気圏外へ出し、青い大気の縁（リム）を持つ惑星として描画できるようにする。
中核の数学は最初から惑星中心座標の球面積分なので LUT 群の作り直しは不要。
ブロッカーは「クランプ」「Y軸天頂固定」「惑星地表の描画が無い」の3点。

### 現状のブロッカー（正確な位置）

| 箇所 | 内容 |
|---|---|
| `AtmosphereManager.cpp` `Update()` の `maxRadius` | カメラ半径を大気圏上端 −1m にクランプ |
| `SkyViewLUT.CS.hlsl` / `SkyAtmosphere.PS.hlsl` の `clamp(cameraRadiusKm, ...)` | 同上（シェーダー側） |
| `Update()` のコメント「Y座標のみを高度として扱う」 | 天頂＝ワールド +Y 固定。惑星を周回できない |
| `SkyAtmosphere.PS.hlsl` の地面フィル | y=一定 の平面チェッカー。球面の惑星地表が無い |

### 実装手順

**5-1: クランプ撤廃と大気圏外レイ処理**

1. CPU 側 `maxRadius` クランプを撤廃（`minRadius` の地表下クランプは残す）。
   `cameraRadiusKm` が大気圏上端を超えられるようにする。
2. `IntegrateScatteredLuminance` 系は `RaySphereIntersectNearest` が大気圏外始点でも
   正しい交点を返すため、**レイ始点を大気圏入口まで前進させる処理を追加**:
   ```hlsl
   float tEntry = RaySphereIntersectNearest(rayOrigin, rayDir, atm.atmosphereTopRadiusKm);
   if (length(rayOrigin) > atm.atmosphereTopRadiusKm) {
       if (tEntry < 0.0f) return 0;             // 大気を通らない → 宇宙（黒）
       rayOrigin += rayDir * (tEntry + 0.001f); // 入口まで進める
   }
   ```

**5-2: ピクセル単位レイマーチパス（大気圏外・高高度用）**

1. `SkyAtmosphere.PS.hlsl` に分岐を追加: `cameraRadiusKm > atmosphereTopRadiusKm - ε` のとき
   Sky-View LUT を使わず、視線ごとに `IntegrateScatteredLuminance`（32〜64 ステップ）を直接呼ぶ。
   （Sky-View LUT のパラメータ化は大気圏内カメラ前提のため。UE も同じ切り替えを行う）
2. このとき **Sky-View LUT の生成をスキップ**する（`AtmosphereLUTPass` にガード追加）。
   カメラ半径の変化検知（0.5m epsilon）が高速な上昇で毎フレーム発火するため、
   スキップしないと無駄な再生成が走り続ける。
3. **惑星地表の描画**: レイが惑星にヒットしたら（`tGround >= 0`）、積分をそこで打ち切り、
   地表のランバート反射を加算する:
   ```hlsl
   float3 hitPos = rayOrigin + rayDir * tGround;
   float3 normal = normalize(hitPos);
   float3 sunTrans = SampleTransmittanceToSun(..., hitPos, toSun, atm);
   luminance += throughput * (atm.groundAlbedo / PI)
              * sunTrans * saturate(dot(normal, toSun));
   ```
   （throughput は 5-4 のエネルギー保存積分で得たカメラまでの透過率）
4. 既存の平面チェッカー地面フィルは**高度 10km 以上でフェードアウト**させる
   （平面近似が破綻する高度。lerp で 3 の球面地表描画へ遷移させる）。

**5-3: 真の球面カメラマッピング（周回ビュー）**

1. 惑星中心をワールド固定点 `(0, groundLevelY - planetRadius, 0)` と定義し、
   `AtmosphereManager` はスカラー半径の代わりに **惑星中心基準のカメラ位置ベクトル
   `cameraPlanetPosKm` (float3)** を CB へ渡す（`cameraRadiusKm` は互換のため残してよい）。
2. `SkyAtmosphere.PS.hlsl` の天頂系を置き換え:
   ```hlsl
   float3 zenith = normalize(cameraPlanetPosKm);
   float viewZenithCos = dot(viewDir, zenith);
   // 太陽相対方位角は zenith に直交する接平面へ viewDir / toSun を射影して算出
   ```
   Sky-View LUT 生成側（カメラを +Y 軸上に置く規約）は変更不要 —
   LUT は (天頂角, 太陽相対方位角) でパラメータ化されており座標系非依存のため、
   **PS 側の UV 算出だけ**を実際の天頂系で行えばよい。
3. `CameraVolumeLUT.CS.hlsl` / `AerialPerspective.CS.hlsl` のサンプル点の惑星中心座標化も
   同じ置き換えを行う（現在は Y のみ高度扱いのはず。要確認）。
4. **精度の注意**: km 単位 float32 で半径 6360km → 仮数部精度 ≈ 0.5m。空の描画には十分だが、
   メートル単位のワールド座標と混ぜないこと（変換は CPU で行い、シェーダーには km だけ渡す）。

### 検証

- [ ] カメラを地表から上昇させ続けると、空が薄くなり → 大気圏上端通過がシームレス
      （切り替え高度で輝度ジャンプが無い）→ 宇宙は黒
- [ ] 宇宙から惑星が「青いリムを持つ球」として見える（地平線が円弧になる）
- [ ] 惑星の昼夜境界（ターミネーター）が見える。夜側は黒、境界は赤みがかる
- [ ] 5-3 まで実装した場合: カメラを水平に大きく移動しても空の見た目が破綻しない
- [ ] 通常の地上シーン（AtmosphereTestScene / Water）に一切回帰が無い

---

## 8. Phase 6: Aerial Perspective 拡張

### 目的

UE の Aerial Perspective View Distance Scale 相当の距離パラメータ化と、半透明への適用。

### 実装手順

1. **距離スケールのパラメータ化** — `CAMERA_VOLUME_KM_PER_SLICE = 4.0f`（AtmosphereCommon.hlsli の
   定数）を `AtmosphereConstants` のフィールド `apKmPerSlice`（既定 4.0）へ移動。
   `CameraVolumeDistanceToW` と `CameraVolumeLUT.CS.hlsl` の距離算出を CB 参照に変更。
   ImGui スライダー（1〜16 km/slice。最大距離 32〜512km に相当）を追加。
2. **半透明への適用** — 新規 `Engine/Assets/Shaders/Atmosphere/AtmosphereApply.hlsli`:
   ```hlsl
   // CameraVolume LUT (Texture3D) と AtmosphereConstants を要求する
   float3 ApplyAerialPerspective(float3 color, float3 worldPos, float2 screenUv, ...)
   {
       float distKm = distance(worldPos, cameraWorldPos) * 0.001f;
       float w = CameraVolumeDistanceToW(distKm);
       float4 ap = gCameraVolumeLUT.SampleLevel(s, float3(screenUv, w), 0);
       return color * ap.a + ap.rgb * sunColor * sunIntensity; // 合成CSと同じ式に揃えること
   }
   ```
   水面シェーダー・パーティクル等、AP を受けたい半透明パスの PS 末尾で呼ぶ。
   CameraVolume の SRV と大気 CB を該当パスへバインドする配管が必要
   （`AerialPerspectivePass` のリソース取得方法を踏襲）。
3. **128km 超のフォールバック** — 雲実装と同じパターン
   （Sky-View LUT の視線方向サンプルへ `1 - exp(-dist/60km)` でブレンド）を
   `ApplyAerialPerspective` 内の `w >= 1` 領域に適用する。

### 検証

- [ ] 水面の遠方が地形・不透明物と同じように大気で霞む
- [ ] `apKmPerSlice` を変えると霞の距離感が変わり、LUT 再生成が正しく走る
- [ ] AP 非対応シーン・大気非アクティブシーンで半透明の見た目が変わらない

---

## 9. スコープ外・将来拡張

- **G8: 月・第2大気ライト・星空** — `isAtmosphereMoon` フラグの第2ディレクショナルライトを追加し、
  Sky-View 積分を2光源対応にする（UE の Second Atmosphere Light）。星空は太陽・月の寄与が
  小さい方向にハッシュベースの手続き星 or キューブマップを輝度ブレンドで加算。
  Phase 1〜3 完了後でないと夜が明るすぎて意味がないため後回し。
- **G9: Atmosphere Shadow** — シャドウマップ・雲影を Sky-View / AP の積分中にサンプルして
  大気そのものに影を落とす（UE の Cast Shadows on Atmosphere）。スクリーンスペースの
  ゴッドレイ実装が既にあるため優先度低。実装するなら CameraVolume 積分への
  シャドウマップサンプル追加が最小構成。
- **物理単位の完全移行（cd/m² / lux）** — Phase 2 は EV 補正に留める。完全な測光単位系への
  移行は全ライト・全マテリアルの再校正を伴うため、必要になるまで行わない。

---

## 10. 共通の検証ワークフロー

`VolumetricCloud_Design.md` §10 と同一。要点のみ:

1. C++ 変更時のみ MSBuild でビルド（.hlsl はランタイム DXC コンパイルのため起動だけでよい）
2. WMI デタッチ起動でゲームを立ち上げ、PrintWindow によるウィンドウ限定キャプチャで検証
3. 太陽高度の変更は大気エディタ（Environment ツリー → 太陽設定）を ImGui 経由で操作
4. 各フェーズの「検証」チェックリストのスクリーンショットを撮ってから次フェーズへ

回帰確認の固定セット（全フェーズ共通）:

- [ ] `AtmosphereTestScene` 昼 / 夕 / 夜
- [ ] `WaterTestScene`（反射・コースティクス）
- [ ] 雲ありシーン（Sky-View LUT を参照しているため）
- [ ] キューブマップ空のシーン（大気非アクティブ経路が汚染されていないこと）

---

## 11. 落とし穴チェックリスト

- [ ] **透過率の二重適用禁止**: 空・太陽ディスク・雲は LUT で減衰済み。Phase 1 の変調は
      サーフェスライティング（DeferredLighting 系）だけに効かせる。大気 CB へは元の値を渡す
- [ ] **authored ライトデータを直接書き換えない**（フィードバックループで光が消える）
- [ ] `DirectionalLightData` は 48 バイト固定・HLSL とストライド共有・**空きフィールド無し**。
      新しい状態は LightManager のメンバとして持つ
- [ ] `AtmosphereConstants`（HLSL）と `AtmosphereShaderConstants`（C++）は常に同時に変更する
- [ ] **新パラメータは必ずダーティ判定へ追加**（`skyViewDirty_` 等）。忘れるとスライダーが効かない
      （既存判定は太陽方向とカメラ半径しか見ていない）
- [ ] Transmittance LUT のサンプラーは CLAMP 必須（WRAP だと日没時に透過率が反対端へ巻き込む）
- [ ] `RWStructuredBuffer` 新設時はシェーダーリフレクションの b0 衝突バグに注意（修正済みパターンに従う）
- [ ] トーンマッピングの後にガンマ補正を足さない（バックバッファ SRGB フォーマットで自動変換される）
- [ ] Phase 5 でカメラ半径クランプを外した後も、**地表下（minRadius）クランプは残す**（積分の特異点）
- [ ] ImGui 追加は Environment ツリー＋Inspector ルーティングの既存構成に従い、
      登録解除は owner 付きで行う（エディタUI再編の規約）
- [ ] 大気非アクティブシーンへの無影響は全フェーズの受け入れ条件（フレーム有効化パターンで保証）

---

## 12. 新規パラメータ既定値一覧

| パラメータ | 置き場所 | 既定値 | 範囲 | フェーズ |
|---|---|---|---|---|
| `transmittanceOnLight` | AtmosphereManager（トグル） | true | - | 1 |
| `exposureEV` | ToneMapping CB | 0.0 | -6〜+6 | 2a |
| `autoExposure` / `minEV` / `maxEV` / `adaptSpeed` | ToneMapping | false / -4 / +4 / 1.5 | - | 2b |
| `skyAmbientEnabled` | Lighting CB フラグ | true（大気アクティブ時） | - | 3 |
| `skyAmbientScale` | Lighting CB | 要校正（目安 0.05〜0.1） | 0〜1 | 3 |
| `multiScatteringFactor` | AtmosphereConstants | 1.0 | 0〜2 | 4 |
| SkyView ステップ数 min/max | SkyViewLUT.CS 定数 | 16 / 64 | - | 4 |
| `apKmPerSlice` | AtmosphereConstants | 4.0 | 1〜16 | 6 |
| 地面フィルのフェード高度 | SkyAtmosphere.PS 定数 | 10 km | - | 5 |

---

## 参考文献

- S. Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" (EGSR 2020)
  — 積分式・MultiScattering LUT・エネルギー保存ステップ積分（§5.3）はこれに従う
- E. Bruneton, F. Neyret, "Precomputed Atmospheric Scattering" (EGSR 2008) — Transmittance パラメータ化
- UE5 `SkyAtmosphereRendering.cpp` / SkyAtmosphere コンポーネントのドキュメント
  — Transmittance on light color / MultiScatteringFactor / Aerial Perspective View Distance Scale の挙動
- L. Lagarde, S. Lachambre, C. Jover, "An Artist-Friendly Workflow for Panoramic HDRI" ほか
  Frostbite PBR Course Notes (SIGGRAPH 2014) — 露出・物理ライティング単位の参考
