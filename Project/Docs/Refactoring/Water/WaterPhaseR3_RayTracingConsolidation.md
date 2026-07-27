# Water リファクタリング Phase R3: RT 水面パスの共通化（実施記録）

- 実施日: 2026-07-27
- ブランチ: `fix/optimization`
- 親文書: [WaterCodeReview_2026-07-27.md](WaterCodeReview_2026-07-27.md)
- 前フェーズ: [R0](WaterPhaseR0_DeadCodeRemoval.md) / [R1](WaterPhaseR1_SingleSourceOfTruth.md) / [R2](WaterPhaseR2_WaterSurfaceShader.md)
- 結果: **-869 行 / +461 行（正味 -408 行）**、Development x64 ビルド成功（エラー・警告 0）

---

## 1. 基底クラスへの引き上げ

`WaterRayTracingPassBase` は存在したが公開面が共通化されておらず、屈折・反射・コースティクスの
3 マネージャが同じ定義を 3 回ずつ持っていた。以下を基底へ移した。

| 対象 | 変更前 | 変更後 |
|---|---|---|
| `FFTOcean{Refraction,Reflection,Caustics}Input` | 完全同型が 3 つ | `WaterRayTracingPassBase::FFTOceanInput` 1 つ |
| `enum class DispatchStatus` | 同一の 7 要素が 3 つ | 基底に 1 つ |
| `struct DispatchDiagnostics` | ほぼ同一が 3 つ | 基底に 1 つ（和集合） |
| `ToString(DispatchStatus)` | switch 7 分岐が 3 コピー | 基底の `static` 関数 |
| ガード失敗 → status 変換 switch | **20 行が 3 コピー** | `ReportDispatchGuardFailure()` |
| `IsInitialized` / `Set・GetSurfaceModelProvider` / `GetLastDiagnostics` | 転送のみの実装が 3 つ | 基底の公開メンバ |
| `EnsureOutputTexture` / `EnsureConstantBuffer` | 基底呼び出し 1 行が 3 つ | `BeginDispatch()` に内包 |
| 所有者名・出力デバッグ名の文字列リテラル | 各所に散在 | `InitializeBase()` で 1 度だけ渡す |

### 新しいディスパッチの型

45 行あったディスパッチ前処理が 8 行になった。

```cpp
WaterSurfaceData resolvedSurfaceData{};
const WaterSurfaceData& dispatchSurfaceData =
    ResolveSurfaceDataForDispatch(surfaceData, resolvedSurfaceData);

const uint32_t viewIndex = static_cast<uint32_t>(viewId);
BeginDiagnostics(viewIndex, width, height, dispatchSurfaceData, sceneDepthSRV, sceneColorSRV);

DispatchResources resources;
if (!BeginDispatch(cmdList, width, height, viewIndex, resources)) {
    return;   // 診断情報の記録と警告ログは BeginDispatch 内で完結
}
```

### 意図的に共通化しなかったもの

- **`ViewID`**: 屈折・コースティクスは GameView + ReflectionView、反射は GameView のみで
  意味が違う。診断側は `viewIndex`（`uint32_t`）で保持する形にした。
- **`Get{Refraction,Reflection,Caustics}SRVHandle` 等**: 基底の `GetOutput*` へ一本化する案もあったが、
  呼び出し側の可読性が落ちる（`GetRefractionSRVHandle` のほうが何を取っているか明確）。
  3 行の転送関数として残した。

### 名前の修正

- `DispatchDiagnostics::worldPositionSrv` → `sceneDepthSrv`
  （WorldPosition ターゲット廃止後は深度 SRV を入れており、名前が実態と食い違っていた）
- `viewId`（派生ごとの enum）→ `viewIndex`（`uint32_t`）

---

## 2. `RayTracingSubsystem::DispatchWater*` の共通前処理

3 つの関数が、以下を丸ごとコピペしていた。

- null チェック 4 種（manager / gBuffer / sceneManager / dx・cmdList）
- SceneColorSnapshot → SceneColor のフォールバック取得
- `GetGameViewCamera3D()` / `viewProjection` / `cameraPosition` / `width`・`height`
- FFT 入力の組み立て

これを `BuildWaterDispatchContext()` に集約した。

```cpp
WaterDispatchContext dispatchContext;
if (!BuildWaterDispatchContext(
    context, dx, cmdList, surfaceData, "water refraction", /*requireSceneColor*/ true, dispatchContext)) {
    return;
}
```

`requireSceneColor` で 1 箇所だけあった差（コースティクスは SceneColor へ再投影しないので不要）を
明示的なパラメータにした。ガードの厳しさが 3 本でバラバラだった問題も同時に解消している。

`RayTracingSubsystem.cpp` は **291 行削減**。

---

## 3. 挙動を変えた 2 点

### 3-1. 水面不在時に屈折・反射のレイを飛ばさないようにした（性能改善）

`RTWaterCausticsPass` だけが `regionValid == 0`（有効な水域なし）でディスパッチを止めており、
屈折・反射は**水面が非表示・不在でもフル解像度で `DispatchRays` していた**。同じガードを追加した。

```cpp
if (!context.waterRefractionSurfaceData
    || context.waterRefractionSurfaceData->regionValid == 0) {
    return;
}
```

> **RenderGraph との整合**: ガードで早期 return すると Blackboard へ出力が登録されないが、
> RenderGraph は未解決リソースのバリアをスキップする設計（`RenderGraph.cpp` の
> 「Compile 時に未解決だったリソースは…実行直前に再解決」）なので問題ない。
> RTWaterCausticsPass が以前からこの前提で動いている。
> 水面描画側の SRV は Blackboard ではなく `WaterSurfaceRuntimeController::SyncFrameResources` が
> マネージャから直接取るため、こちらも影響を受けない。

### 3-2. 毎フレーム無条件だった診断ログを `debugLogEnabled` 配下へ

| 場所 | 変更前 | 変更後 |
|---|---|---|
| `WaterRefractionRayTracingManager::Dispatch` | `Infof` × 3（うち 1 本は**引数 26 個**） | 1 本に統合、ガード内 |
| `WaterCausticsRayTracingManager::Dispatch` | `Infof` × 1（完了ログ） | ガード内 |
| `RTWaterRefractionPass::Execute` | `Infof` × 3 | 1 本に統合、ガード内 |
| `RTWaterCausticsPass::Execute` | `Infof` × 1 | ガード内 |
| `RayTracingSubsystem::DispatchWaterRefraction` | `Infof` × 2（うち 1 本は引数 20 個） | 削除 |
| `WaterRayTracingPassBase::ResolveSurfaceDataForDispatch` | `Infof` × 1（3 マネージャ分 = 毎フレーム 3 本） | 削除（結果は `GetLastDiagnostics` で参照可） |

**毎フレーム 11 本 → 0 本**（UI の「RTログを有効にする」時のみ 2 本）。
文字列整形のコストが常時かかっていた点も解消される。

---

## 4. 検証

| 項目 | 結果 |
|---|---|
| C++ ビルド（Development x64） | **成功**（エラー 0 / Water 関連の警告 0） |
| 旧 API の残存検索（`PrepareDispatchResources` / `*ProviderBase` / `FFTOcean*Input` / `worldPositionSrv` / `diagnostics.viewId`） | 0 件 |
| 無条件 `Infof` の残存 | 0 件（残るのは初期化時 1 回のみのものと、ガード内） |

### 未確認 — 目視確認が必要

R3 は C++ の構造変更が中心で、シェーダーには一切触れていない。ただし §3-1 は**実際に挙動が変わる**ため、
以下を確認してほしい:

1. **水面を非表示にしたとき**に屈折・反射のディスパッチが止まり、かつ画面が壊れないこと
2. 水面を再表示したときに屈折・反射が正常に復帰すること
3. 通常の水面描画（Gerstner / FFT 両モード）が R2 時点と変わらないこと
4. RT 屈折 / RT コースティクスのデバッグログが UI トグルで出る／出ないこと

---

## 5. R0〜R3 の累計

| フェーズ | 内容 | 差分 |
|---|---|---|
| R0 | 死コード削除 | -701 / +198 |
| R1 | 単一情報源化 | -279 / +63（＋共有ヘッダ 4 本） |
| R2 | `Water.PS.hlsl` 整理 | Water.PS 1,094→971 行（＋Debug 242 行）、命令 -16.6% |
| R3 | RT パス共通化 | -869 / +461 |

親レビューで挙げた項目のうち、**R4（水面リソース結線を Engine へ移管）**と
**R5（設定の単一情報源化）**、**R6（ドキュメント整合）**が残っている。

R4 は `WaterPlaneObject` の 12 個の setter 廃止と `SyncFrameResources` の 120 行の移管を伴う
設計変更なので、R0〜R3 の目視確認を済ませてから着手するのが安全。
