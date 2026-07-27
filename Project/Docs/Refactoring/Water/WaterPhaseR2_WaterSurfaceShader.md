# Water リファクタリング Phase R2: `Water.PS.hlsl` の整理（実施記録）

- 実施日: 2026-07-27
- ブランチ: `fix/optimization`
- 親文書: [WaterCodeReview_2026-07-27.md](WaterCodeReview_2026-07-27.md)
- 前フェーズ: [R0 死コード削除](WaterPhaseR0_DeadCodeRemoval.md) / [R1 単一情報源化](WaterPhaseR1_SingleSourceOfTruth.md)

---

## 0. レビュー時の判断ミスの訂正

親レビュー §5-2 で「`gReflectionEnabled` 時に PBR 出力は**必ず**上書きされるので丸ごと無駄」と書いたが、
**これは誤り**だった。実際のコードは:

```hlsl
float3 skyReflectColor = reflectColor;          // ← PBR 出力
if (gSkyEnvReflectionEnabled != 0) {
    skyReflectColor = gSkyEnvironmentMap.SampleLevel(...).rgb;
}
reflectColor = rtHit ? rtReflection.rgb : skyReflectColor;
```

空環境マップが無効なフレームでは、**RT 反射レイがミスしたピクセルのフォールバックとして
PBR 出力が実際に使われる**。無条件にスキップすると、そのピクセルが黒くなる。

そのため実装は「PBR の rgb が確実に読まれない条件」に限定した:

```hlsl
const bool forwardColorUnused = (gReflectionEnabled != 0) && (gSkyEnvReflectionEnabled != 0);
```

この条件は cbuffer だけで決まる**均一分岐**なので、ワープ内発散も起こさない。
大気散乱シーン（＝既定の構成）では常に true になり、PBR とIBLが丸ごと省かれる。

---

## 1. 実施内容

### 1-1. 水面法線の評価を 4 回 → 1 回

`ResolveSurfaceNormal()` は FFT 経路で **3 カスケード分のテクスチャサンプル**を伴う重い関数だが、
同一ピクセルで最大 4 箇所から呼ばれていた（フォワード PBR / 水柱厚さ / geomNormal / デバッグモード7）。

`main()` の冒頭で 1 度だけ解決し、以降は使い回す。
`ResolveSurfaceNormal` は入力に対する純関数なので、結果は完全に同一。

### 1-2. 使われないフォワード PBR のスキップ

上記 §0 の条件で `CalculateAllLighting`（全ライトの Cook-Torrance）と `ApplyIBL`
（キューブマップサンプル）を丸ごと省く。アルファ（`gMaterial.color.a`）だけは常に必要なので保持する。

### 1-3. 水柱厚さの決定を関数へ抽出

`main()` に直書きされていた約 90 行を `ResolveWaterColumn()` へ切り出し、
戻り値を `WaterColumnResult` 構造体にまとめた。

**ロジックは一切変更していない。** 代わりに、4 つの供給源の関係をドキュメント化した:

| 記号 | 供給源 | 効く条件 |
|---|---|---|
| (A) | スクリーン空間近似（深度差 × 屈折換算） | 背景ジオメトリあり & RT がミス |
| (B) | 無限水柱 `1e4 m` | 背景が far plane（外洋・水平線） |
| (C) | RT 実測光路長 | 屈折レイがヒット（(A)/(B) より優先） |
| (D) | 解析的な鉛直水深（分岐なしの連続場） | 浅瀬 1m までは 100%、4m へ向けて (A)/(C) へ移行 |

> レビューでは「(A) は実質どこでも使われない可能性が高い」と書いたが、
> **改めて追うと (A) は到達可能**（深部かつ RT ミス時）だった。削除はせず、
> 到達条件を明記するに留めている。

### 1-4. デバッグ表示 22 モードを別ファイルへ分離

`main()` を占めていた 205 行を `Water.Debug.hlsli` へ移設。
参照する中間量は `WaterDebugContext` 構造体で**明示的に**渡す。

HLSL ではテクスチャ・サンプラを引数で持ち回れないため、それらだけは
グローバル参照が残る。依存する識別子の一覧を同ファイル冒頭に契約として明記し、
include 位置（`main()` 直前）を指定した。

移設時の差異は「早期 return への書き換え」のみで、分岐の意味は変えていない
（例: モード 1 の「背景が水面より手前なら赤」を後置上書きから先頭 return へ）。

### 1-5. カスケード化で死んでいた定数の削除

`kFresnelNormalMipBias`（宣言のみで参照ゼロ）を削除。
カスケード化により「最大パッチのスライスを単独サンプル」方式へ移行しており、
ミップバイアスによる高周波ぼかしは使われていない。定数だけが残り、
`ResolveFresnelNormal` のコメントと食い違っていた。

また `SampleGlossyReflection` は**現在デバッグモード 19 専用**になっているため、
その旨を関数コメントへ明記した（本体の合成は DXR 反射を screenUV でそのまま引く）。

### 1-6. 見た目に関わる定数は変更していない

`kFresnelNormalFlatten` / `kWaterReflectionMicroRoughness` / `kWaterReflectionBlurTexels` /
`kReflectionCompressKnee` / `kAnalyticColumnFullMeters` などの「緩和済みマジックナンバー」は、
**値を一切変更していない**。これらの調整は目視確認を伴うチューニング判断であり、
リファクタリングとは分けるべきと判断した（親レビュー §4-2 に課題として残す）。

---

## 2. 効果測定

`-Od -Zpr`（実行時と同条件）で DXIL を出力し、R2 の最適化を無効化した変種と比較した。

### 2-1. 法線の重複排除（1-1）

| | 命令数 | テクスチャ Sample | 算術演算 |
|---|---|---|---|
| 法線を重複評価（従来） | 5,028 | 39 | — |
| **R2 適用後** | **4,191** | **30** | — |
| 削減 | **−16.6%** | **−9（−23%）** | — |

削減された 9 サンプルは「3 カスケード × 除去した 3 呼び出し」と一致する。

### 2-2. フォワード PBR のスキップ（1-2）

こちらは**実行時の分岐**なので静的命令数には現れない（両方の経路がシェーダーに残る）。
大気散乱シーンでは分岐が常に成立し、全ライトの Cook-Torrance と IBL キューブマップ
サンプルが実行されなくなる。

### 2-3. ファイル構成

| | 行数 |
|---|---|
| `Water.PS.hlsl`（R2 前） | 1,094 |
| `Water.PS.hlsl`（R2 後） | 971 |
| `Water.Debug.hlsli`（新規） | 242 |

---

## 3. 検証

| 項目 | 結果 |
|---|---|
| C++ ビルド（Development x64） | 成功（エラー 0） |
| HLSL 7 本を dxc で個別検証（`-Od -Zpr`） | 全て成功 |
| `ResolveSurfaceNormal(input)` の呼び出し箇所 | 4 → **1** |
| `kFresnelNormalMipBias` の残存 | 0 |

### 3-1. 挙動等価性について

R0・R1 と違い、**R2 は DXIL レベルの完全一致では検証できない**:

- 1-1（法線 1 回化）は純関数の共通部分式除去なので数学的に等価
- 1-3・1-4 は制御フローの入れ替えを伴わない抽出
- **1-2（PBR スキップ）だけは「結果が使われない経路の計算を省く」変更**であり、
  等価性は「`gSkyEnvReflectionEnabled != 0` なら `skyReflectColor` が必ず
  キューブマップ色で上書きされる」というコード読解に依拠している

### 3-2. 未確認 — 目視確認が必要

**このフェーズは実機での目視確認を強く推奨する。** 特に:

1. **空環境マップが無効な構成**（大気散乱を切った場合／キューブマップ未生成のフレーム）で
   水面が黒くならないこと ← 1-2 の条件判定が正しいかの直接確認
2. Gerstner / FFT 両モードの水面の見た目が R1 時点と変わらないこと
3. デバッグ可視化 22 モードすべてが従来どおり表示されること（特に 1 / 18 / 19 / 20〜22）
4. 反射有効／無効の切り替え

---

## 4. 次のステップ

親レビューの **Phase R3（RT 水面パス 3 本の共通化）**。C++ 側の作業で約 500 行削減見込み:

1. `FFTOcean*Input` 3 つを 1 つに統合
2. `DispatchStatus` / `DispatchDiagnostics` / `ViewID` / `ToString` / ガード変換 switch を基底へ
3. `RayTracingSubsystem::DispatchWater*` の共通前処理を抽出
4. `RTWaterRefractionPass` / `RTWaterReflectionPass` へ `regionValid` ガードを追加（水面不在時の DispatchRays を止める）
5. 無条件 `Infof` を `debugLogEnabled` ガード配下へ

なお親レビュー §4-2 の「緩和済みマジックナンバーの妥当性検証」は、
リファクタリングではなくチューニング作業として別途扱う。
