# 水面泡（Foam / Whitecap）実装計画

作成日: 2026-08-02
関連: [Step7_Foam.md](../../../JobHuntingWorksDoc/Step7_Foam.md)（要件定義）/ FFTカスケード海面 / TAA・CAS実装（ping-pong規約）

## 進捗
- **Phase 0: 完了（2026-08-02）** — ヤコビアン出力を `(Jxx, Jzz, Jxy, detJ)` へ再定義し、
  `ComputeFFTCombinedDetJ`（回転共役＋エンベロープ込みのワールド合成）を
  `FFTOceanCascade.hlsli` へ追加。VS 経由の interpolant（TEXCOORD1 jacobianData）は
  FFTWater.VS / Water.VS / Water.PS / Water.Debug.hlsli から全廃し、
  デバッグモード 16 は PS が t20 を直接サンプルする方式へ変更。
  全 87 シェーダーの dxc オフライン検証＋実機（WaterTestScene）で
  ランタイムコンパイル・描画・デバッグビュー動作をエラー 0 件で確認済み。
  - **検証で得た知見（Phase 1 の入力）**: 無重みの全カスケード合成 detJ は
    最小カスケード（31m・高波数）の勾配が支配して広範囲で detJ ≤ 0 に飽和する
    （勾配は k に比例するため波高配分が小さくても傾き寄与は最大）。
    §7 の cascadeWeight（1.0/1.0/0.5 起点、実測ではさらに小さい値が要りそう）を
    Phase 1 で必ず導入し、foamBias の較正は重み適用後に行うこと。
- **Phase 1: 完了（2026-08-02）** — 瞬時泡マスクの合成を実装。
  - `WaterFrameConstants` へ泡パラメータを追加（96→128B、**4箇所一致を同時更新**:
    `WaterSurfaceTypes.h` / `Water.VS` / `Water.PS` / `FFTWater.VS`。static_assert で検証）:
    `foamEnabled=1 / foamBias=0.85 / foamGain=4.0 / foamOpacity=0.9 / foamCascadeWeights=(1.0, 0.5, 0.2)`
  - `ComputeFFTCombinedDetJ` に cascadeWeights 引数を追加（勾配テンソルへ乗算）。
    デバッグモード 16（FFT Jacobian）も同じ重みで評価するよう統一。
  - `Water.PS`: `ComputeFoamMask`（合成 detJ → しきい値）＋ `ComputeFoamColor`
    （Lambert 白 kFoamAlbedo=(0.90,0.93,0.95) × 太陽直達/π + 天空光SH。
    水中インスキャッタと同じ光源ソースで空との明るさが整合）。
    合成: `reflectanceWeight ×= (1-foamBlend)` → 最終合成後に `lerp(色, foamColor, foamBlend)`
    → サングリッター `×(1-foamBlend)`。
  - デバッグモード 23「FFT 泡マスク」（`WaterDebugViewMode::FFTOceanFoam`）追加。
    enum + `Water.Debug.hlsli` + `WaterSurfaceDebugPanel` の名前配列（手動リスト）の 3 点セット。
  - UI: `WaterSurfaceParameterPanel` 共通設定タブに「泡 / Whitecap」セクション、
    JSON 永続化（Serialize/Deserialize + 復元時 `ApplyFoamParameters`）も実装済み
    （Phase 5 の残りは Step7 ドキュメント更新程度）。
  - 実機検証: 泡マスクは波頭に沿ったスパースな白パッチ（重み効果で飽和解消）、
    通常描画でも自然な白波。全ログエラー 0、fps 影響は誤差レベル（±1fps）。
  - **ビルドの注意**: CB サイズ変更のクリーンビルドで `-t:Rebuild` を使うと
    DirectXTex の .inc が消えて壊れる（3度目の再発）。正手順は
    「`generated\CoreEngine\obj\<Config>` 削除 → `-t:Build -p:BuildProjectReferences=false`」
    （詳細はメモリ build-system-gotchas）。
- **Phase 2: 完了（2026-08-02）** — 泡の蓄積・減衰パスを実装。
  - 新規 CS `FFTOceanFoamAccumulate.CS.hlsl`: `foam = max(prev·exp(-dt/τ), injection)`。
    Dispatch z=カスケードで全スライス一括処理。専用 cbuffer `FFTOceanFoamConstants`
    （共有の FFTOceanSimulationConstants には触れない＝一致箇所を増やさない）。
  - リソース: ping-pong Texture2DArray ×2（R16_FLOAT・3スライス・シミュ解像度）。
    書き込み先は `foamFrameIndex_ & 1` の純粋関数（TAA規約）。read 側は同フレームの
    Water.PS(t21) も読むため NON_PIXEL|PIXEL の複合状態へ遷移。
  - 設定同期: 単一情報源は WaterFrameConstants（foamDecaySeconds を追加、サイズ不変）。
    `WaterRenderFeature::SyncFoamSettings` が毎フレーム `FFTOceanManager::SetFoamSettings`
    へ転送。スペクトル再構築・無効→有効の遷移で蓄積をリセット（resetFoam フラグ）。
  - PS 合成: `foamMask = max(瞬時項(合成detJ), max_ci(蓄積スライス))`。
  - **実装中に２つの飽和問題を実測で解決**:
    1. 線形注入だと波峰が参照格子上を位相速度で掃引しながら毎フレーム注入するため、
       数周期で海面全体が泡の包絡に埋まり真っ白になる → **注入を二乗特性**にして
       弱い圧縮を非線形に抑圧（強く砕けた峰だけが持続泡を残す）。
    2. それでも最小カスケード（31m）は単体 detJ のレンジが極端（勾配∝k で ±数倍）
       ＋波周期約4.5秒の頻繁な再注入で飽和する → **蓄積参加率 kAccumulationShare =
       {1.0, 0.4, 0.0}** を導入。持続泡は「うねりの砕波」の現象で、小カスケードの
       砕波は瞬時項が既に表現しているため蓄積には参加させない。
  - 検証: マスクは黒ベース＋ソフトな減衰グラデーション付き白パッチ、時間発展を確認。
    通常描画で泡がマスクと一致。全ログエラー0・55fps（コスト増なし）。
- **Phase 3: 完了（2026-08-02）** — 見た目の品質向上（Water.PS のみ・C++ 変更なし）。
  - **ノイズ分断**: 手続き value noise 2 オクターブ（ワールド固定・周期約 2m/0.6m、
    テクスチャ資産不要）。`mask - noise·強さ·(1-mask)` で薄い縁ほど強く削れ、
    濃い芯は残る → しきい値等高線の単調な縁取りがフラクタル状に割れる。
  - **2 層構造**: 濃い層（surface, smoothstep 0.40→0.95, Lambert 白）＋
    薄い層（subsurface, smoothstep 0.05→0.55, 泡色×0.55 を弱ブレンド ＝
    水面下の気泡による白濁。水の情報を残す）。フレネル/グリッター抑制は
    実効被覆率 `surface + 0.5·subsurface` で行う。
  - **ラフネス連動**: 泡域は ComputeSunGlintSpecular のラフネスを 0.45 へ lerp
    （輝度は従来通り (1-被覆) 倍も併用）。
  - **波群エンベロープ同期**: 蓄積泡へ envelope²×0.7 を表示時に乗算
    （蓄積 CS は格子空間でワールド位置を知らないため表示側で変調。
    瞬時項は合成 detJ 内で適用済みなので二重適用しない）。
  - 検証: マスクの縁がノイズで分断され、被覆率は風速 12m/s の whitecap として
    物理的に妥当な数%へ低下（Phase 2 時点はやや過剰だった）。波群の通過で
    泡の量が時間変動する。全ログエラー 0・55fps。
  - チューニング定数（kFoam* 系）は Water.PS 冒頭の static const に集約。
- **Phase 4: 完了（2026-08-02）** — 岸際泡（shore foam）。Water.PS のみ・C++ 変更なし。
  - `WaterColumnResult` に解析的鉛直水深 `analyticColumn` を追加
    （far plane / Depth Fade 無効時は kInfiniteWaterColumnMeters ＝ 泡ゼロ側の保守既定）。
  - `ComputeShoreFoamMask`: `1 - smoothstep(0, 0.6m, analyticColumn)` × ノイズ分断
    × 最大強度 0.85（surface しきい値 0.95 未満 ＝ 汀線がベタ白にならず白濁主体）。
    ★入力は解析水深の連続場のみ★ — 「波打ち際の線」10 連発の教訓により、
    RT 成功/失敗・深度分岐などの離散量は一切使わない。
  - 時間変調は追加不要: 解析水深は波の変位を含むため、波の寄せ引きで帯が自然に脈動する。
  - 合成: crest 泡と max で foamMask へ（2 層化・フレネル/グリッター抑制は共通経路）。
  - 検証: 汀線に沿ってノイズで分断された泡帯が波と同期して脈動、深場には出ない、
    1px の硬い線は発生せず。全ログエラー 0・53fps。
- **Phase 4.5: 完了（2026-08-02）** — 泡の見た目の作り直し（dissolve 方式）。
  - **動機（ユーザー指摘）**: 「泡がノイズテクスチャみたいで違和感」「岸側の泡が弱い」。
    真因は泡を「被覆率の明度グラデーション」でそのまま描いていたこと。実際の泡は
    レースの穴・筋・粒という鋭い微細構造を持つ。さらに旧 value noise 2 オクターブ
    （周期 2.2m/0.6m）は近景で雲状ブロブにしかならず、蓄積テクスチャの
    2m/テクセル解像度を隠せていなかった。
  - **dissolve（しきい値カット）方式へ変更**: マスクは滑らかな「被覆率」の場に純化
    （ノイズ減算を撤去）し、表示時に高周波泡パターン `FoamPattern`（fbm 4 オクターブ
    1.5m〜0.1m ＋ ridged 網目）へ `smoothstep(1-mask, 1-mask+0.18, pattern)` で
    カット（`ComputeFoamLace`）。被覆率が上がるとパターンの高い所から順に埋まり、
    縁は常に鋭いレース状になる。
  - **白濁（haze）の再定義**: レースの穴の間 `(1-lace)×mask` に限定し、パターンで
    粒状に変調（均一な白もや＝霧化を解消）。強度 0.5→0.35。
  - **粒感**: 泡内部のアルベドを周期 8cm のノイズで 0.82〜1.0 に変調。
  - **岸泡の 2 段構造**: 汀線エッジ（水深 0.15m 以浅）は被覆率満量の白いシート、
    外側 0.6m は被覆率 0.9→0 のフェードでレース状に割れる。
  - 検証: 汀線はシート＋レースの構造、砕波泡は穴と筋を持つ割れた形状になり
    ブロブ感が解消。全ログエラー 0。パターンはワールド固定なので、動きで
    違和感が出る場合は将来スクロール（風向へ低速移流）を検討。
- Phase 5（Step7 ドキュメント更新）: 未着手

---

## 1. 泡の物理と実装の定石

### 1.1 泡はどこから来るか
- 白波（whitecap）は、波頭が水平方向に圧縮されて砕けた際に空気が混入した層。
  「水面の色」ではなく「散乱体の層」なので、発生条件・寿命・減衰を持つ現象として扱う。
- Tessendorf (2001) 以来の定石: 水平変位のヤコビアン行列式 `detJ = det(I + ∂D/∂x)` が
  小さくなる（波頭の圧縮）〜負になる（波面の折り返し = foldover）箇所が砕波候補。
- 業界実装（Sea of Thieves, Atlas, UE Water 等）の共通構造:
  1. **発生（injection）**: `saturate((foamBias - detJ) * foamGain)` で瞬時の砕波候補を取る
  2. **蓄積・減衰（accumulation）**: 前フレームの泡を減衰させつつ injection と合成
     → 波が通り過ぎた後も泡が数秒残る「白い筋」になる
  3. **描画（shading）**: 泡はほぼ Lambert な白い拡散層。フレネル反射を下げ、
     ラフネスを上げ、水の透過色を覆う。白ベタではなく水面下の情報を少し残す
  4. **分断（breakup）**: ノイズで輪郭の単調さを崩す

### 1.2 蓄積を FFT 格子空間で行う理由
泡は物理的には水の質点に付着して流されるが、深水波の質点軌道はほぼ円運動
（Stokesドリフトは微小）なので、**FFT の参照格子テクセル上で蓄積すれば移流は近似的に不要**。
これが業界標準のやり方で、周期テクスチャなのでワールドタイリングとも自動的に整合する。

---

## 2. 現状調査の結果

### 2.1 既にあるもの
| 資産 | 場所 | 状態 |
|---|---|---|
| ヤコビアン計算 | `FFTOceanFinalize.CS.hlsl:97-104` | `(detJ, breakingCandidate, compression, foldover)` をカスケード毎に出力済み |
| ヤコビアンテクスチャ | `FFTOceanManager.cpp:395`（R16G16B16A16_FLOAT, Texture2DArray×3） | SRV/スライスUAV作成済み、Water パイプラインへバインド済み（t20） |
| choppiness の織り込み | `FFTOceanTimeEvolution.CS.hlsl:60-61` | スペクトル段階で変位に乗算済み → Finalize の勾配は choppiness 込み ✔ |
| カスケード写像の単一情報源 | `FFTOceanCascade.hlsli` / `FFTOceanCascadeValues.hlsli` | 回転格子 UV・波群エンベロープが共通関数化済み |
| デバッグ表示 | `WaterDebugViewMode::FFTOceanJacobian` + `VisualizeJacobian` | 動作するが後述の制約あり |
| 要件定義 | `Docs/JobHuntingWorksDoc/Step7_Foam.md` | 完了条件・作業項目が明文化済み |
| 岸際判定の材料 | `Water.PS.hlsl ResolveWaterColumn`（解析的鉛直水深） | 岸際泡（shore foam）に流用可能 |

### 2.2 消費側の現状
- `FFTWater.VS.hlsl:93` が **カスケード0のみ・頂点解像度で** ヤコビアンをサンプルし、
  PS へは **デバッグ可視化専用** に渡している。シェーディングには一切未使用。
- つまり泡の「入力」は揃っているが「消費」はゼロの状態。

---

## 3. 「今のヤコビアンのままで進めて大丈夫か」の結論

**土台（計算方法・choppiness込み・カスケード毎テクスチャ）はそのままで良い。
ただし出力チャネルの意味を1箇所変えないと、この先で詰む。** 理由は3つ:

### 問題A: カスケード毎の detJ は合成できない（最重要）
実際に描画される水面は 2〜3 カスケードの変位の**和**。合成面のヤコビアンは
`det(I + J₀ + J₁ + J₂)` であり、カスケード毎の `det(I + Jᵢ)` からは復元不可能
（行列式は和に対して分配されない）。さらに各カスケードは格子が回転している
（0°/+26°/−49°）ので、成分をワールド系に回転してから足す必要がある。

- 現在の出力 `(detJ, breakingCandidate, compression, foldover)` は最終値のみで、
  勾配テンソル成分が失われている → **合成不能**。
- **対策**: Finalize の出力を `(Jxx, Jzz, Jxy, detJ)` に変更する。
  - `Jxx = ∂Dx/∂x, Jzz = ∂Dz/∂z, Jxy = ∂Dx/∂z`。
    FFT 変位は同一スカラーポテンシャル由来なので `∂Dx/∂z ≡ ∂Dz/∂x`（対称）となり
    3成分で完全（Finalize 内の `dDx_dz` と `dDz_dx` は理論上同値、平均を取って格納）。
  - `.w` の detJ は「そのカスケード単体の砕波判定」として泡蓄積 CS がそのまま使う。
  - `breakingCandidate / compression / foldover` はサンプリング側で1行で再計算できる
    派生量なので、テクスチャに置く必要がない（デバッグ表示も追随修正）。
- 消費者は `FFTWater.VS`（デバッグ）と `Water.Debug.hlsli` のみなので変更コストは小さい。

### 問題B: 波群エンベロープが反映されていない
`FFTWater.VS:76` で変位に `ComputeFFTWaveGroupEnvelope(worldXZ)`（±12%）が掛かるが、
ヤコビアンはこれを知らない。エンベロープが強い場所ほど実際の圧縮も強いのに泡は一定になる。
- **対策**: 問題Aで勾配成分を持てば、サンプリング時に `J *= envelope` してから
  `det(I + ΣJ)` を取るだけで解決（エンベロープの空間勾配は波長364m〜なので無視可）。
  これはテンソルを持っていないと原理的に不可能 → 問題Aの変更が前提。

### 問題C: 時間発展（蓄積・減衰）がない
瞬時 detJ は波の位相と一緒に動くため、そのまま白を乗せると
「泡が波と一緒に滑る・明滅する」不自然さになる。泡の寿命は Step7 の完了条件でもある。
- **対策**: 泡蓄積テクスチャ＋専用 CS パスを新設（§4 Phase 2）。

> 補足: `FFTWater.VS:93` の「カスケード0のみ・回転なしサンプル」はカスケード0の回転が
> 0° なので現状バグではないが、泡実装後はこの VS 経由の受け渡し自体を廃止し、
> PS で全カスケードを直接サンプルする方式（法線と同じ）に揃える。

---

## 4. 実装フェーズ計画

### Phase 0: ヤコビアン出力の再定義（半日）
**目的**: 問題A/Bを解消できるデータ形式にする。見た目の変化なし。
- `FFTOceanFinalize.CS.hlsl`: 出力を `float4(Jxx, Jzz, Jxy, detJ)` へ変更
  （`Jxy = 0.5*(dDx_dz + dDz_dx)`）。
- `Water.Debug.hlsli` / `Water.PS.hlsl VisualizeJacobian`: 新チャネルから
  compression/foldover を再計算する形へ追随。
- **PS 側に共通関数を新設**（`FFTOceanCascade.hlsli` へ）:
  ```hlsl
  // 全カスケードのヤコビアンをワールド系で合成して detJ を返す
  // J_world = R^T · J_grid · R（回転共役）、envelope 倍率込み
  float ComputeFFTCombinedDetJ(float2 worldXZ, Texture2DArray<float4> jacobianTex, SamplerState s)
  ```
- 検証: `FFTOceanJacobian` デバッグビューで detJ 分布を目視。風速12m/s・choppiness 1.35
  の既定で detJ < 1 の圧縮域が波頭に沿って出ること、値域をログで確認
  （しきい値 foamBias の初期値決めに使う）。

### Phase 1: 静的泡マスクの合成（1日）
**目的**: 蓄積なしの瞬時泡をまず画に出し、しきい値・見た目の方針を固める。
- `Water.PS.hlsl`:
  - `ComputeFFTCombinedDetJ` で合成 detJ → `foamMask = saturate((foamBias - detJ) * foamGain)`
  - 合成方法（Step7「白ベタ禁止・反射透過を壊さない」に対応）:
    ```
    foamColor   = foamAlbedo × (Σ 太陽光·NdotL + 天空光SH)   // Lambert。既存の
                  gDirectionalLights / gWaterSkyIrradianceSH を流用
    finalColor  = lerp(finalWaterComposite, foamColor, foamMask × foamOpacity)
    reflectanceWeight ×= (1 - foamMask)                        // 泡はフレネル反射しない
    サングリッター ×= (1 - foamMask)                           // 鏡面のきらめきも抑制
    ```
  - 深場抑制: 泡は detJ 由来なので「深場中央で常時発生」はそもそも起きないが、
    湖モード等に備え foamGain=0 で完全無効化できるようにする。
- `WaterDebugViewMode::FFTOceanFoam`（新規 =23）: foamMask 単独表示。
- パラメータは `WaterFrameConstants` へ追加（**4箇所一致**: `WaterSurfaceTypes.h` +
  `Water.VS.hlsl` + `Water.PS.hlsl` + `FFTWater.VS.hlsl`。§6参照）。
- 検証: 波頭にのみ白が出る/深場中央が汚れない/反射・透過の破綻なし。

### Phase 2: 泡の蓄積・減衰パス（1〜1.5日）
**目的**: 泡に寿命を与える（Step7 完了条件の核心）。
- **新規リソース**（`FFTOceanResourceFactory` / `FFTOceanManager`）:
  - `foamTexture_[2]`: Texture2DArray（3スライス, R16_FLOAT, シミュ解像度と同じ256）
    ping-pong 2枚。SRV は配列全体、UAV も配列全体（1ディスパッチで全スライス処理）。
- **新規 CS** `FFTOceanFoamAccumulate.CS.hlsl`（numthreads 8,8,1 / Dispatch z=カスケード数）:
  ```hlsl
  injection = saturate((foamBias - jacobian[slice].w) * foamGain);   // カスケード単体 detJ
  foam      = max(prevFoam * exp(-dt / foamDecaySeconds), injection);
  ```
  - `max` 型（Atlas方式）: 発生中は満充填・通過後に指数減衰。加算型より飽和管理が楽。
  - dt はポーズ・スロー対応のため CB で渡す（`FFTOceanSimulationConstants` の
    padding スロットを使用。**TimeEvolution/Finalize の HLSL 側宣言と C++ 構造体の
    3箇所同時更新**）。
  - 設定変更（解像度・風速変更でスペクトル再構築）時は泡テクスチャをクリア。
- **ping-pong 規約**: TAA の知見に従い `frameNumber の偶奇の純粋関数` に一本化
  （フラグ変数のトグルにしない）。
- **パイプライン挿入位置**: `FFTOceanManager::Dispatch` の Finalize ループ後・
  SRV遷移後（jacobianTexture_ を SRV として読むため現在の遷移順と噛み合う）。
- `Water.PS`: 泡テクスチャ（新規 **t21**）を全カスケード分サンプルして
  `foamMask = saturate(Σ foamᵢ × cascadeWeightᵢ)`、瞬時 detJ は「発生直後の輝度ブースト」
  として弱く加算。バインドは `WaterShaderResourceBinder` + `WaterRenderResources` に追加。
- 検証: カメラ静止で波が通過→白い筋が2〜5秒かけて消えること。
  自動露出下の見え方確認（夜は+8EVで白飛びする既知の罠に注意）。

### Phase 3: 見た目の品質（1日）
- **ノイズ分断**: 手続きノイズ（value noise 2オクターブ程度、ワールドXZ基準）で
  `foamMask` をしきい値変調。専用テクスチャ資産は不要な見込み。
- **2層構造**: しきい値を2段にし、
  - 濃い層（surface foam）: 上記の Lambert 白
  - 薄い層（subsurface bubbles）: 水の透過色を `lerp(色, 白っぽい色, 弱め)` で
    わずかに白濁させるだけ（水面下の情報を残す）
- **ラフネス連動**: 泡域は `glintRoughness` を上げ、ハイライトを柔らかく。
- 波群エンベロープを injection 側にも掛け、泡の濃淡が波のセットと同期するようにする
  （PS で `ComputeFFTWaveGroupEnvelope` を再利用）。

### Phase 4: 岸際泡（shore foam）— 任意・後回し可
- `ResolveWaterColumn` の解析的鉛直水深（分岐のない連続場）を入力に、
  `shoreFoam = 1 - smoothstep(0, shoreFoamWidth, analyticColumn)` × ノイズ × 時間変調。
- **注意**: 過去の「波打ち際の線」10連発の教訓により、岸際に新しい2値切替を持ち込まない。
  必ず連続場（解析水深）だけから作り、RT成功/失敗などの離散量を混ぜない。
- crest foam と同じ合成経路（foamMask への加算）に乗せる。

### Phase 5: UI・永続化・仕上げ（半日）
- `WaterSurfaceParameterPanel` に「泡」セクション:
  foamEnabled / foamBias / foamGain / foamDecaySeconds / foamOpacity /
  foamNoiseScale / cascadeWeight[3] / shoreFoamWidth
- `WaterSettingsSection`（WaterTestScene）へシリアライズ追加（登録時即 Deserialize 方式）。
- `WaterSurfaceDebugPanel` の FFT セクションに泡テクスチャの状態表示。
- Step7_Foam.md のステータス・完了条件チェックを更新。

---

## 5. 合成データフロー（Phase 2 完了時点）

```
FFTOceanPass (Compute)
  ├─ TimeEvolution ── choppiness織り込み済みスペクトル
  ├─ IFFT ×2系統
  ├─ Finalize ──→ displacement / normal / jacobian(Jxx,Jzz,Jxy,detJ) [3スライス]
  └─ FoamAccumulate（新規）
        in : jacobian(SRV), foam[prev](SRV), dt
        out: foam[cur](UAV)  ← ping-pong, R16_FLOAT ×3スライス
                 │
Water.PS（ラスタ）
  ├─ ComputeFFTCombinedDetJ: 回転共役でワールド合成 + envelope → 瞬時detJ
  ├─ foamMask = Σ foam[cur]ᵢ·weightᵢ （+ 瞬時ブースト、ノイズ分断）
  └─ 合成: 透過/反射の lerp 後に Lambert白を foamMask で重ね、
           reflectanceWeight・サングリッターを (1-foamMask) 倍
```

RT 3パス（反射/屈折/コースティクス）は**変更不要**:
水面は自分自身を反射しないため泡は RT 反射に現れず、RT 屈折は水面より下の像なので
ラスタ PS で上に重ねる泡と競合しない。コースティクスへの影響（泡が日光を遮る）は
物理的には存在するが微小なので対象外とする。

---

## 6. 依存性と落とし穴（チェックリスト）

| # | 項目 | 内容 |
|---|---|---|
| 1 | **WaterFrameConstants 4箇所一致** | `WaterSurfaceTypes.h`(C++) / `Water.VS.hlsl` / `Water.PS.hlsl` / `FFTWater.VS.hlsl` の b5 レイアウトを同時更新。1箇所でもズレると全パラメータが壊れる |
| 2 | **FFTOceanSimulationConstants 3箇所一致** | dt 追加時: `FFTOceanManager.h`(C++) / `FFTOceanTimeEvolution.CS` / `FFTOceanFinalize.CS`（FoamAccumulate.CS 含め4箇所） |
| 3 | **構造体サイズ変更 → クリーンビルド** | ODR事故（ヒープ破損 c0000374）の前科あり。CB構造体を触ったらクリーンビルド必須 |
| 4 | **ping-pong はフレーム偶奇の純関数** | TAA で確立した規約。トグル変数は再入・リサイズで壊れる |
| 5 | **リソース状態遷移** | foamTexture は「前=SRV/現=UAV」を毎フレーム交互遷移。jacobianTexture は Finalize 後 NON_PIXEL_SHADER_RESOURCE になっている位置に FoamAccumulate を置く |
| 6 | **ShaderReflectionBuilder の RWStructuredBuffer バグ** | 新 CS では RWTexture2DArray のみ使い、UAV 構造化バッファは使わない（既知バグ回避） |
| 7 | **レジスタ割当** | 泡テクスチャは t21（t18=disp, t19=normal, t20=jacobian, t24=SH, t25=skyEnv の空き） |
| 8 | **デバッグビュー enum** | `WaterDebugViewMode` に追加したら `WaterSurfaceDebugPanel` の表示名配列にも追記（手動リスト） |
| 9 | **設定変更時の泡リセット** | スペクトル再構築（`spectrumBufferDirty_`）と解像度変更で泡テクスチャをクリアしないと古い泡が残留する |
| 10 | **夜間検証の罠** | 自動露出+8EVで泡の白が飛ぶ。しきい値調整は昼シーンで行い、夜は露出固定で確認 |
| 11 | **エンベロープの二重適用禁止** | detJ 計算で envelope を掛けたら、injection 側で再度掛けない（どちらか一方を選び コメントで明記） |

---

## 7. パラメータ初期値（調整開始点）

| パラメータ | 初期値 | 根拠 |
|---|---|---|
| foamBias | 0.85 | detJ<0.85 で発生（Tessendorf系実装の常用域 0.7〜1.0。Phase 0 の値域ログで確定） |
| foamGain | 4.0 | しきい値からの立ち上がり勾配 |
| foamDecaySeconds | 3.0 | 白波の残存 2〜5 秒の中央値 |
| foamOpacity | 0.9 | 白ベタ回避のため 1.0 にしない |
| foamAlbedo | (0.9, 0.93, 0.95) | 泡はわずかに青白い |
| cascadeWeight | 1.0 / 1.0 / 0.5 | 最小カスケード(31m)は頂点変位に不参加のため泡も控えめに |

---

## 8. 検証計画

1. **Phase 0**: FFTOceanJacobian ビューで合成 detJ の値域を確認（風速 8/12/18 m/s）。
2. **Phase 1-2**: FFTOceanFoam ビュー + カメラ静止での寿命目視。
   GameView キャプチャは既存ワークフロー（WMIデタッチ起動 + PrintWindow）を使用。
3. **不自然さ判定**（Step7 完了条件）:
   - 深場中央が常時白くない / 泡が波と一緒に「滑らない」/ 消滅がポップしない
4. **性能**: GpuTimestampProfiler の Water カテゴリで FoamAccumulate 追加分を計測
   （256²×3 スライスの CS なので 0.05ms 未満の想定。超えたら異常）。
5. **回帰**: 反射・透過・グリッター・波打ち際（線の再発）・TAA 収束を昼夜で確認。

---

## 9. 工数まとめ

| Phase | 内容 | 目安 |
|---|---|---|
| 0 | ヤコビアン出力再定義 + 合成関数 | 0.5日 |
| 1 | 静的泡マスク合成 | 1日 |
| 2 | 蓄積・減衰パス | 1〜1.5日 |
| 3 | ノイズ分断・2層化・品質 | 1日 |
| 4 | 岸際泡（任意） | 0.5〜1日 |
| 5 | UI・永続化・ドキュメント | 0.5日 |

合計: コア（0〜2）で約3日、フル（0〜5）で約5日。
