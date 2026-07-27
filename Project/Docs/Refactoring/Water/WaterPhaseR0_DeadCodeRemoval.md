# Water リファクタリング Phase R0: 死コード削除（実施記録）

- 実施日: 2026-07-27
- ブランチ: `fix/optimization`
- 親文書: [WaterCodeReview_2026-07-27.md](WaterCodeReview_2026-07-27.md)
- 結果: **-701 行 / +198 行**（AtmosphereEditor の既存差分を除く）、Development x64 クリーンビルド成功

---

## 1. 削除したもの

### 1-1. 平面反射（ReflectionView）系 — 到達不能

`WaterTestScene::BuildRenderViewRequests()` が `{}` を返す（＝ReflectionView を 1 つも発行しない）ため、
以下は呼び出し元ゼロの状態だった。

| 対象 | 措置 |
|---|---|
| `Application/Src/Scenes/WaterTestScene/WaterReflectionPass.{h,cpp}`（214 行） | ファイルごと削除（vcxproj / filters からも登録解除） |
| `WaterSceneController::ApplyWaterRenderViewResult` | 削除 |
| `WaterSurfaceRuntimeController::ApplyWaterRenderViewResult` | 削除 |
| `WaterPlaneObject::ApplyWaterReflectionResult` | 削除 |
| `WaterPlaneObject::SetClipPlane` | 削除 |
| `WaterFrameConstants::clipPlane[4]` / `clipEnabled` | 削除（C++ と HLSL 3 本） |
| `WaterConstantBufferSet` の 3 本目の CB（`reflectionFrameCB_`） | 削除（毎フレーム確保・マップされていたが永久に未使用だった） |
| `GetFrameCBGpuAddress(bool)` / `UpdateFrameConstants(..., bool)` の分岐 | 引数ごと削除 |
| `Water.VS.hlsl` / `FFTWater.VS.hlsl` の `SV_ClipDistance0` 出力と `clipDist` 計算 | 削除（`gClipEnabled` が常に 0 のため常に `1.0` を返していた） |

### 1-2. `WaterSurfaceSnapshot` — 書き込み専用

Gerstner / FFT の両 simulator が毎フレーム構築していたが、読む側が存在しなかった。

- `WaterSurfaceData.h` の型定義を削除
- `WaterSurfaceSimulator::CaptureSurface()` の第 2 引数を削除（実装 2 本・宣言 3 本）
- `WaterSurfaceRuntimeController::waterSurfaceSnapshot_` メンバを削除

### 1-3. FFT UV 写像の配管 — 5 層まとめて

カスケード化（`SampleFFTOceanCascade*` が `worldXZ / kFFTCascadePatch[c]` の写像を内部に持つ）により、
外から渡す UV 写像は**どの RT シェーダーからも参照されなくなっていた**。

```
WaterSurfaceRuntimeController.cpp  fftUVScale/Offset の導出（メッシュ transform から）  ← 削除
  → WaterSurfaceData::fftUVScale / fftUVOffset / fftUVMappingValid              ← 削除
    → FFTOcean{Refraction,Reflection,Caustics}Input::uvScale / uvOffset          ← 削除
      → Water{Refraction,Reflection,Caustics}Constants::fftOceanUVScale/Offset   ← 削除
        → HLSL cbuffer gFFTOceanUVScale / gFFTOceanUVOffset（3 本）              ← 削除
```

`RayTracingSubsystem` の 3 つの `DispatchWater*` にあった「写像の有無で分岐するフォールバック」
（各 14 行 × 3）も道連れで削除。`SampleFFTOceanBilinear()`（呼び出し元ゼロ・23 行）も削除。

### 1-4. HLSL の到達不能・誤誘導コード

| 対象 | 措置 |
|---|---|
| `EvaluateWaterOffsetFFTOcean` / `EvaluateWaterNormalFFTOcean` | 削除（ゼロと真上を返すスタブだった） |
| `EvaluateWaterOffset` / `EvaluateWaterNormal` の `simulationType` 分岐 | 削除し、`EvaluateWater*Gerstner` へ改名。呼び出し側 3 本を追従 |
| `ComputeSunDownwellingTransmittance`（33 行） | 削除（2026-07-27 に呼び出しを撤去済みだった） |
| `kWaterReflectionDistortStrength` | 削除（完全未使用） |

> **`EvaluateWaterOffset` の分岐を消した理由**: FFT 経路で呼ぶと「波の無い平坦な水面」が返る罠だった。
> 実際の FFT 評価は各シェーダーの `IsFFTOceanSurfaceActive()` → `SampleFFTOceanCascade*` が担っており、
> 抽象化の体裁だけが残って中身が無い状態だった。名前を `*Gerstner` にして意図を明示した。

### 1-5. その他

- `Application/Src/GameObjects/Water/WaterConstantBuffer.h`（include 転送 4 行のみ）を削除
- `WaterPlaneObject::UpdateUVScroll()`（「旧呼び出し経路との互換のため」と自認していた）を削除

---

## 2. 追加した安全装置

`WaterFrameConstants` は 20 バイト縮み、float3 の 16 バイト境界を保つために
`skyAmbientEnabled` を `skyAmbientScale` の直後へ移動した（下記レイアウト）。
このクラスのずれは過去に RT シャドウで実害を出しているため、コメントではなく
`static_assert` で固定した。

```cpp
// WaterSurfaceTypes.h
static_assert(sizeof(WaterFrameConstants) == 96, ...);
static_assert(offsetof(WaterFrameConstants, absorptionCoeff) % 16 == 0, ...);
static_assert(offsetof(WaterFrameConstants, scatteringCoeff) % 16 == 0, ...);
static_assert(offsetof(WaterFrameConstants, cameraNearZ) == 80, ...);
```

`WaterCausticsConstants` も UV 写像の削除で中央が 16 バイト詰まるため、
`float3 gAbsorptionCoeff` が 16B 境界をまたがないことと `gInvViewProj` の整列を検証する
`static_assert` を追加した。

### 定数バッファのサイズ変化

| cbuffer | 変更前 | 変更後 |
|---|---|---|
| `WaterFrameConstants` | 112 B | **96 B** |
| `WaterRefractionConstants` | 208 B | **192 B** |
| `WaterReflectionConstants` | 208 B | **192 B** |
| `WaterCausticsConstants` | 192 B | **176 B** |

---

## 3. 実態に合わせたコメント修正

削除対象を指していた記述のみを直した（本格的なドキュメント整合は Phase R6）。

- `Water.PS.hlsl` の `gReflectionTexture`：「Planar Reflection RTT」→ RTWaterReflectionPass の DXR 出力
- `WaterPlaneObject.h` の `SetReflectionTexture`：「毎フレーム WaterReflectionPass から渡す」→ RTWaterReflectionPass
- `WaterSceneSetup.cpp`：「Planar Reflection（ReflectionView）が空を映し込む」「カメラ追従でタイルをカメラ直下へ置く」
  （どちらも撤去済みの挙動）を現状の記述へ

---

## 4. 検証

| 項目 | 結果 |
|---|---|
| C++ ビルド（Development x64・クリーン） | **成功**（exit 0、エラー・警告ゼロ）→ `CoreEngine.exe` 生成 |
| `static_assert`（cbuffer サイズ / 16B 境界 × 7 本） | 全て通過 |
| HLSL: `RTWaterRefraction` / `RTWaterReflection` / `RTWaterCaustics` | dxc `lib_6_3` でコンパイル成功 |
| HLSL: `Water.VS` / `FFTWater.VS` | dxc `vs_6_0` でコンパイル成功 |
| HLSL: `Water.PS` | dxc `ps_6_0` でコンパイル成功 |
| 残存参照の全文検索 | 削除した 17 シンボルすべて 0 件（コメント内の履歴記述を除く） |

> **HLSL は実行時コンパイル**（`ShaderCompiler` が DXC で読む）なので、C++ ビルドが通っても
> シェーダーは検査されない。dxc で個別に検証すること。include ルートは
> `find Engine/Assets/Shaders -type d` を全て `-I` に渡せばよい。

### まだ実機確認していないこと

ビルドとシェーダーコンパイルは通ったが、**描画結果の目視確認は未実施**。
R0 は全て到達不能コードの削除なので見た目は変わらない想定だが、次を確認しておくと確実:

- Gerstner / FFT 両モードで水面が従来どおり描画されるか
- 水面デバッグ表示（可視化モード 1〜22）が従来どおり動くか
- RT 屈折 / RT 反射 / RT コースティクスの 3 経路

---

## 5. 副作用: DirectXTex のビルド環境

MSBuild の `Clean` は DirectXTex の生成済みシェーダー（`externals/DirectXTex/Shaders/Compiled/*.inc`）を
削除する（`ATGDeleteShaders` ターゲット）。再ビルド時に `CompileShaders.cmd` が走るが、
`WindowsSdkVerBinPath` が未設定だと fxc.exe を見つけられず **exit 9009 でビルド全体が落ちる**。

復旧手順:

```bash
WindowsSdkVerBinPath="C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\" \
  cmd /c "cd /d C:\CoreEngine\Project\externals\DirectXTex\Shaders && .\CompileShaders.cmd"
```

`.\` を付けないと cmd が cwd を探索せず「認識されていません」になる点に注意。
これは今回の変更とは無関係な既存の環境問題。

---

## 6. 次のステップ

親レビューの **Phase R1（単一情報源化）** へ。R0 で消しきれなかった
「コメントで守られている不変条件」を仕組みへ移すフェーズ:

1. FFT カスケード定数と `ComputeFFTWaveGroupEnvelope` の共有 hlsli 化（現在 4 箇所コピー）
2. RT 屈折アルファのエンコード/デコード規約の共有化（現在 2 箇所）
3. Gerstner 波評価の共有化（`RTWaterSurfaceCommon.hlsli` と `WaterCaustics.PS.hlsl` の 2 実装）
