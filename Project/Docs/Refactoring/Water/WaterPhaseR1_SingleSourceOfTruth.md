# Water リファクタリング Phase R1: 単一情報源化（実施記録）

- 実施日: 2026-07-27
- ブランチ: `fix/optimization`
- 親文書: [WaterCodeReview_2026-07-27.md](WaterCodeReview_2026-07-27.md) / 前フェーズ: [WaterPhaseR0_DeadCodeRemoval.md](WaterPhaseR0_DeadCodeRemoval.md)
- 目的: 「一致必須」というコメントだけで守られていた不変条件を、**コンパイラが検証する形**へ移す
- 結果: Development x64 ビルド成功、水面シェーダ 7 本すべてコンパイル成功、**DXIL 等価性を検証済み**

---

## 1. 何が問題だったか

R0 で死コードを消した後も、以下が「コメントによる約束」で保たれていた。
片方だけ書き換えると、RT が評価する波面とラスタ描画の波面がズレ、
屈折の depth mismatch や水中判定の帯として現れる（過去に実際に起きた事故）。

| 不変条件 | 重複箇所 |
|---|---|
| FFT カスケードのパッチ長・格子回転・波群エンベロープ | **4 箇所**（FFTWater.VS / Water.PS / RTWaterSurfaceCommon / FFTOceanManager.cpp） |
| Gerstner 波の数式（steepness クランプ含む） | **2 実装**（RTWaterSurfaceCommon.hlsli / WaterCaustics.PS.hlsl） |
| RT 屈折アルファのエンコード規約 | **2 箇所**（RTWaterRefraction.hlsl / Water.PS.hlsl） |
| 頂点変位に使うカスケード数 | FFTWater.VS の定数 と RTWaterCaustics のハードコード `2` |

---

## 2. 追加した共有ヘッダ

新設: `Engine/Assets/Shaders/Water/Common/`

### 2-1. `FFTOceanCascadeValues.hlsli` — C++ / HLSL 共通の数値定義

**プリプロセッサマクロだけで構成**しているため、HLSL からも C++ からも `#include` できる。
これがカスケード数値の唯一の情報源。

```c
#define FFT_OCEAN_CASCADE_COUNT 3
#define FFT_OCEAN_CASCADE_PATCH_LENGTHS { 521.0f, 127.0f, 31.0f }
#define FFT_OCEAN_CASCADE_ROT_COS { 1.0f, 0.89879405f, 0.65605903f }
#define FFT_OCEAN_CASCADE_ROT_SIN { 0.0f, 0.43837115f, -0.75471006f }
#define FFT_OCEAN_GEOMETRY_CASCADE_COUNT 2
#define FFT_OCEAN_WAVE_GROUP_STRENGTH 0.12f
```

C++ 側（`FFTOceanManager.cpp`）:

```cpp
#include "../../../Assets/Shaders/Water/Common/FFTOceanCascadeValues.hlsli"

static_assert(FFTOceanManager::kCascadeCount == FFT_OCEAN_CASCADE_COUNT, "...");
constexpr float kCascadePatchLength[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_PATCH_LENGTHS;
constexpr float kCascadeRotCos[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_COS;
constexpr float kCascadeRotSin[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_SIN;
```

> **なぜ Engine/Src から Assets を include するのか**
> シェーダーは実行時に *配備先* の Assets ツリーから読まれるため、共有ファイルは
> Assets 配下に無ければならない（`xcopy /E` で配備される）。層としては逆向きだが、
> C++ と HLSL の数値一致をコンパイル時に強制できる唯一の方法なので、
> 「数値マクロだけ」という制約付きで許容している。

### 2-2. `FFTOceanCascade.hlsli` — HLSL 側の型付き定数と写像

数値マクロを `static const` へ展開し、ワールドXZ ⇄ 回転格子系の写像を提供する。

- `RotateToFFTCascadeGrid` / `RotateFromFFTCascadeGrid` / `ComputeFFTCascadeUV`
- `ComputeFFTWaveGroupEnvelope`

以前は Water.PS と FFTWater.VS が回転行列を**手書きでインライン展開**していた。
これらを共有関数へ置き換えた（式は完全に同一）。

### 2-3. `GerstnerWave.hlsli` — 波 1 本分の数式

cbuffer レイアウトは用途ごとに違う（DXR は `simulationType`、コースティクスは水域矩形を持つ）ため、
**cbuffer 宣言とループは各シェーダーに残し、「ズレると壊れる数式」だけを集約**した。

- `struct GerstnerWave`（32 バイト。旧 `WaveParam` / `WaterWaveParam` を統合）
- `ResolveGerstnerWaveTerms`（k・ω・自己交差を避けた実効 steepness・位相）
- `EvaluateGerstnerWaveOffset` / `AccumulateGerstnerWaveDerivatives` / `BuildGerstnerNormal`

### 2-4. `WaterRefractionEncoding.hlsli` — RT 屈折アルファの規約

`kRTSuccessRangeMin` / `kRTMaxOpticalPathMeters` / `kRTColorInvalidOffset` と
`EncodeHitAlpha`（書き手）／`IsRTPathValid`・`IsRTColorValid`・`DecodeRTOpticalPath`（読み手）を集約。

---

## 3. 消費側の変更

| ファイル | 変更 |
|---|---|
| `RTWaterSurfaceCommon.hlsli` | GerstnerWave / FFTOceanCascade を include。ローカルの波数式（約 65 行）とカスケード定数・回転写像（約 38 行）を削除 |
| `WaterCaustics.PS.hlsl` | GerstnerWave を include。重複していた波数式（約 65 行）と `WaterWaveParam` 定義を削除 |
| `FFTWater.VS.hlsl` | FFTOceanCascade を include。定数・エンベロープ（約 30 行）を削除し、回転を共有関数へ |
| `Water.PS.hlsl` | FFTOceanCascade / WaterRefractionEncoding を include。定数・エンベロープ・アルファ復号（約 45 行）を削除し、手書き回転を共有関数へ |
| `RTWaterRefraction.hlsl` | WaterRefractionEncoding を include。エンコード規約（約 45 行）を削除 |
| `RTWaterCaustics.hlsl` | ハードコード `2` を `kFFTGeometryCascadeCount` へ |
| `FFTOceanManager.cpp` / `.h` | 共有マクロから展開＋ `static_assert` |

---

## 4. 検証

### 4-1. ビルド・コンパイル

| 項目 | 結果 |
|---|---|
| C++ ビルド（Development x64） | **成功**（exit 0、エラー 0）→ `CoreEngine.exe` 生成 |
| `static_assert`（kCascadeCount ↔ マクロ） | 通過（＝共有ヘッダの include が実際に効いている証明） |
| HLSL 7 本（RT 3 / VS 2 / PS 2）を dxc で個別検証 | 全て成功（`-Od -Zpr`、実行時と同条件） |
| 共有ヘッダの配備 | `outputs/Development/Engine/Assets/Shaders/Water/Common/` に 4 ファイル配備済み |
| 重複定義の残存検索 | `Water/Common/` 以外に 0 件 |

### 4-2. DXIL 等価性（挙動が変わっていないことの証明）

`WaterCaustics.PS.hlsl` は R0 で未変更なので、**HEAD 版（R1 前）と現行版を直接コンパイルして比較**できた。

```
演算数:                    before=6761  after=6761
演算種別のヒストグラム:      完全一致（fmul/fadd/fdiv/FMax/Sin/Cos/Dot2/Rsqrt …）
浮動小数定数の出現回数:      完全一致
```

差分として残るのは SSA レジスタ番号・構造体名（`WaterWaveParam`→`GerstnerWave`）・
cbuffer ロードの巻き上げ順序・可換な乗算のオペランド順のみで、いずれも計算結果を変えない。

> **注意点**: 最初の実装では `EvaluateGerstnerWaveOffset` で
> `(steepness*amp*cos) * dir.x` と括り出していたが、これは元の
> `steepness*amp*dir.x*cos` と乗算順序が変わる。`-Od` では再結合されないため
> 最下位ビットがズレる余地があり、**同じ波面を別々に評価した結果が食い違う**
> 原因になりうる（この種の微小差は過去に継ぎ目の原因になっている）。
> 元の順序へ戻した上で上記の等価性検証を行っている。

### 4-3. まだ実機確認していないこと

R0 と同様、**描画結果の目視確認は未実施**。DXIL 等価性まで確認できているので
見た目は変わらない想定だが、次を見ておくと確実:

- Gerstner / FFT 両モードの水面
- コースティクス（スクリーンスペース／RT の両バックエンド）
- RT 屈折・RT 反射

---

## 5. 効果

- FFT カスケード数値の情報源: **4 箇所 → 1 箇所**（C++ 側もコンパイル時に束縛）
- Gerstner 波の数式: **2 実装 → 1 実装**
- RT 屈折アルファ規約: **2 箇所 → 1 箇所**
- 「一致必須」というコメントに依存した不変条件が water ドメインから解消

---

## 6. 次のステップ

親レビューの **Phase R2（`Water.PS.hlsl` の整理）**。見た目に影響しうるので目視確認とセットで進める:

1. `ResolveSurfaceNormal` の重複呼び出し（同一ピクセルで最大 4 回）を 1 回化
2. `gReflectionEnabled` 時に捨てられている `WaterForwardMain` の PBR/IBL をスキップ
3. 4 系統ある水柱厚さの決定を 1 つの関数へ集約し、到達不能な系統を削除
4. デバッグ表示 22 モード（205 行）の分離
5. 緩和済みマジックナンバーの整理
