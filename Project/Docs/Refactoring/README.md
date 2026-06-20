# レンダリングパイプライン設計メモ

> **対象:** `Engine/Src/Graphics/Render/`、`Engine/Src/EngineSystem/EngineSystem.cpp`、`Engine/Src/EngineSystem/Subsystem/RayTracingSubsystem.cpp`  
> **目的:** 現在のレンダリングパイプライン構造を整理し、Deferred Rendering を基盤にしつつ、最終的に RenderGraph 化するための移行方針をまとめる。  
> **このファイルの役割:** 各ステップ詳細ドキュメントを束ねるまとめファイル。全体方針、共通原則、進捗確認、着手順の索引として扱う。

---

## 0. ドキュメント構成

このディレクトリのドキュメントは、全体まとめとステップ別詳細に分けて管理する。

### 0-1. まとめファイル
- `README.md` : 全体方針、共通ルール、進捗サマリー、各ステップへの入口

### 0-2. ステップ別詳細ファイル
- [Step01_RenderPipelineExecution.md](./Step01_RenderPipelineExecution.md) - `EngineSystem` から `RenderPipeline` へ実行責務を移す段階
- [Step02_RTShadowAndComposite.md](./Step02_RTShadowAndComposite.md) - RT Shadow の Pass 化と Composite 系責務整理
- [Step03_FrameBlackboard.md](./Step03_FrameBlackboard.md) - 論理リソース名ベースのフレーム共有層導入
- [Step04_RenderGraphMinimum.md](./Step04_RenderGraphMinimum.md) - 最小 RenderGraph の導入
- [Step05_AutoBarrier.md](./Step05_AutoBarrier.md) - 主要リソースの自動バリア導入
- [Step06_MultiViewIntegration.md](./Step06_MultiViewIntegration.md) - Shadow / RT / 複数 View 統合
- [Step07_LegacyPipelineRemoval.md](./Step07_LegacyPipelineRemoval.md) - 旧実行経路削除と RenderGraph 完全移行
- [Step08_FullPassGraphMigration.md](./Step08_FullPassGraphMigration.md) - Graph 外 GPU 経路整理と全パス RenderGraph 化方針

### 0-3. 読み方
- 最初にこの `README.md` で全体像を確認する
- 実装時は対象ステップの詳細ファイルを主に参照する
- 進捗更新は、まずステップ別詳細の状態を更新し、その後この README のサマリーに反映する

---

## 1. 現在のレンダリングフロー

現在のメインフローは `EngineSystem::ExecuteRenderPipeline()` が実質的に制御している。
`RenderPipeline` は保持しているが、実行順の主導権は `EngineSystem` 側にある。

### 現行フロー

1. **RenderContext 構築**
   - `DirectXCommon`
   - `RenderManager`
   - `RenderTargetManager`
   - `GBufferManager`
   - `ShadowMapManager`
   - `RenderingTechniqueManager`
   - `RayTracingShadowManager`
   - `DepthStencilManager`

2. **DXR 加速構造更新**
   - BLAS 遅延構築
   - シーン内 `ModelGameObject` から TLAS 構築

3. **ShadowMapPass**
   - 主方向ライトの Light View Projection を計算
   - モデル / スキニングモデルのシャドウマップ生成

4. **ReflectionView 描画（必要時のみ）**
   - 水面反射などの補助ビューを Engine 側で実行

5. **GBufferPass**
   - 不透明 `Model / SkinnedModel` を GBuffer に出力

6. **SSAOPass**
   - GBuffer から SSAO を生成
   - 必要に応じてブラー

7. **RT Shadow Dispatch**
   - `WorldPosition / Normal / MotionVector` を参照
   - ライトごとに Trace → Temporal → Denoise

8. **DeferredLightingPass**
   - GBuffer、SSAO、ShadowMap、RT Shadow、IBL を使ってライティング
   - `Offscreen0` にシーンカラー出力

9. **GeometryPass**
   - 実態は Forward Composite
   - 透過オブジェクト、SkyBox、Particle、UI、Debug などを `Offscreen0` に上乗せ

10. **PostEffectPass**
	- ポストエフェクトチェーンを適用

11. **BackBufferPass**
	- 最終結果をバックバッファへ描画し Present

---

## 2. 現在の構造の問題点

### 2-1. `EngineSystem` に責務が集中している
- 実行順制御
- パス入力の接続
- Forward/Deferred の切り替え制御
- RT Shadow の特例実行
- 補助 View の実行調停

`RenderPipeline` より `EngineSystem` が実質的なパイプライン実行器になっている。

### 2-2. パス依存が暗黙的
各パスが `RenderContext` や各種 Manager を経由して必要データを取得しているため、
「何を読んで何を書いているか」がコード上で見えづらい。

### 2-3. `PassOutput` が単一出力前提で弱い
GBuffer のような複数出力、Depth、Velocity、ShadowMask などを自然に表現できない。
そのため、GBuffer は `GBufferManager` 直参照で扱っている。

### 2-4. `RenderManager` の責務が多い
- DrawQueue 管理
- カメラ振り分け
- Shadow 描画
- GBuffer 描画
- Forward 描画
- IBL 適用

描画対象の抽出、描画リスト構築、実際の描画が密結合している。

### 2-5. `GeometryPass` の名前と役割が一致していない
現在の `GeometryPass` は不透明ジオメトリの本流ではなく、
DeferredLighting 後の Forward Composite を担当している。

### 2-6. RT 系が RenderPass に統合されていない
`DispatchRTShadow()` がパイプラインの一部ではなく特例呼び出しになっている。
将来 RT Reflection や RT GI を追加すると整理が難しくなる。

### 2-7. View 概念が弱い
GameView / ReflectionView / 将来の CaptureView などを統一的に扱う構造になっていない。

### 2-8. アプリ層とエンジン層の責務境界が曖昧
WaterTestScene のように、アプリケーション層が一時的に以下を直接扱えてしまう余地がある。

- RenderPass の実行タイミング制御
- RenderTarget の切り替え・復元
- SceneColor / SceneDepth / ReflectionRT の注入
- CommandList 操作
- ResourceBarrier / ResourceState の整合管理

この状態では、機能追加のたびにアプリ側へエンジン責務が漏れやすく、
一時対応のつもりで入れた処理が恒久化しやすい。

### 2-9. 新規 RenderPass の差し込みコストが高い
新しい描画機能を追加する際に、RenderPipeline へ Pass を追加するだけでは済まず、
EngineSystem・Scene・RenderManager・個別オブジェクト側へ特例処理が広がりやすい。

特に以下のような機能は本来 Pass / View / Resource 契約だけで差し込めるべきである。

- Water Reflection
- SSR / RT Reflection
- CaptureView
- Volumetric / Fog
- 将来の TAA / MotionVector 利用パス

---

## 3. 目指すべき設計方針

基盤は **Deferred Rendering** とし、以下を標準化する。

- **Opaque / Masked** は Deferred
- **Transparent / Water / Particle / UI** は Forward Composite
- **Screen Space 技術** は GBuffer 後に差し込む
- **RT 技術** は専用パスとして差し込む
- **ポストエフェクト** は Lighting/Composite 後に集約する

### 3-1. 責務境界の明文化

アプリケーション層とエンジン層の責務を以下で固定する。

#### アプリケーション層が行ってよいこと
- 描画機能の利用要求
- Material / Feature フラグ設定
- オブジェクト単位の描画属性設定
- 必要な View の利用要求
- Scene 固有のゲームロジック更新

#### エンジン層が専属で行うこと
- RenderPass の実行順制御
- RenderTarget / DepthStencil の切り替え
- CommandList 操作
- ResourceBarrier / ResourceState 管理
- SceneColor / SceneDepth / Reflection / Shadow などのフレーム内リソース配線
- パス間の入力接続

#### 明示的な禁止事項
- アプリケーション層は RenderTarget を直接 bind / restore しない
- アプリケーション層は CommandList を直接操作しない
- アプリケーション層は ResourceBarrier を直接発行しない
- アプリケーション層は SceneColor / SceneDepth を個別オブジェクトへ直接注入しない
- GameObject / Scene は RenderPass の実行順を制御しない

必要な描画機能は **Pass / View / Feature 登録を通じて要求する** 形に寄せる。

### 3-2. View 抽象の方針

将来的に複数の描画視点を同じ仕組みで扱うため、View を正式概念として持つ。

- `GameView` : 通常ゲーム描画
- `ReflectionView` : 水面反射や鏡面反射用の補助ビュー
- `CaptureView` : 将来のプレビュー / キューブマップ生成 / オフラインキャプチャ用

SceneView は移行途中の検証経路として扱っていたが、現行方針では廃止済みとする。

重要なのは、**Reflection は Scene が一時的に反射パスを実行して作るものではなく、ReflectionView をエンジンが処理した結果として得る** 形にすること。

### 3-3. パス追加時の基本原則

新規 RenderPass は、少なくとも以下を宣言できる状態を目標にする。

- どの View で動作するか
- どの論理リソースを Read するか
- どの論理リソースを Write するか
- 前提となる描画フェーズは何か
- どの Feature を有効条件にするか

これにより、新規パス追加時の変更点を Scene や GameObject に広げず、RenderPipeline / FrameBlackboard / RenderGraph 側へ閉じ込めやすくする。

### 理想フロー

1. Scene 抽出
2. View ごとの描画リスト構築
3. Shadow Phase
4. GBuffer Phase
5. Screen Space Phase
   - SSAO
   - Decal
   - SSR 前処理
   - Hi-Z など
6. Lighting Phase
   - Deferred Lighting
   - IBL
   - ShadowMap / RT Shadow 統合
   - 将来の SSGI / RT Reflection 差し込み
7. Forward Composite Phase
   - Transparent
   - Water
   - Particle
   - Sky
8. PostProcess Phase
   - TAA
   - Bloom
   - ToneMapping
   - ColorGrading
9. UI / Debug / Present

### 3-4. Forward Composite の順序指針

少なくとも標準順序の基準を持つ。

1. Sky
2. Water
3. Transparent
4. Particle
5. UI
6. Debug / Gizmo

実際の描画順は機能要件に応じて再調整してよいが、個別機能ごとに Scene 側から暫定順序を差し込まないことを優先する。

---

## 4. どこから直していくか

RenderGraph をいきなり入れるのではなく、**RenderGraph に載せ替え可能な構造へ先に分解する**。

### 着手前の前提ルール

- 新機能追加のために Scene 側から RenderPass を直接実行しない
- 新機能追加のために Scene 側から RenderTarget を直接切り替えない
- 一時的な SRV / RTV / DSV の配線は Engine 側のフレーム共有リソース管理へ寄せる
- 「まずアプリ側で無理やり動かす」は原則避け、必要なら薄い暫定 Pass をエンジン側へ追加する

### Phase 1. `EngineSystem` から実行責務を剥がす
**対象:** `EngineSystem.cpp`、`RenderPipeline.h/.cpp`

- `ExecuteRenderPipeline()` にある順序制御を `RenderPipeline` 側へ寄せる
- `Setup / Execute / Cleanup` の呼び出し責務を `RenderPipeline` に統一する
- `EngineSystem` は RenderContext 構築と実行依頼に寄せる

**到達目標:**
- `EngineSystem` がパス順序を直接知らない状態にする

### Phase 2. RT Shadow を RenderPass 化する
**対象:** `RayTracingSubsystem.cpp`、新規 `RTShadowPass`

- `DispatchRTShadow()` を特例処理から外す
- `GBufferPass` の後、`DeferredLightingPass` の前に入る通常パスへ変える
- 可能なら将来を見据えて `ASBuildPass` と `RTShadowPass` を分離する

**到達目標:**
- RT 技術をパイプラインへ自然に差し込めるようにする

### Phase 3. `GeometryPass` を役割ごとに分割する
**対象:** `GeometryPass.*`、`RenderManager.*`

- `TransparentPass`
- `SkyPass`
- `ParticlePass`
- `UIPass`

必要なら最初は内部実装を共通化してもよいが、パスの責務は分離する。

**到達目標:**
- Deferred 基盤の後段合成構造を明確にする

### Phase 4. DrawQueue から DrawList 構造へ寄せる
**対象:** `RenderManager.*`、将来の `RenderScene / RenderView / DrawListBuilder`

- 描画対象抽出
- View ごとの分類
- Opaque / Transparent / ShadowCaster / UI などのリスト化

**到達目標:**
- GameView と補助 View を同じ仕組みで扱いやすくする

### Phase 5. `FrameBlackboard` を導入する
**対象:** 新規フレーム共有リソース管理層

パス間のデータ受け渡しを、Manager 直参照ではなく論理リソース名に寄せる。

例:
- `SceneColor`
- `SceneDepth`
- `ReflectionColor`
- `ReflectionDepth`
- `GBufferA`
- `GBufferB`
- `GBufferC`
- `Velocity`
- `SSAO`
- `ShadowMask`
- `RTShadowMask`

**到達目標:**
- パスごとの入出力を明示しやすくする

この段階で、Water / Reflection / Screen Space 系の機能が
`SceneColor` や `SceneDepth` を Scene 側から直接受け渡ししなくても利用できる構造を目指す。

---

## 5. 最終目標: RenderGraph 化

十分に責務分離できたら、次に RenderGraph を導入する。

### RenderGraph に持たせたい最低限の機能
- パス追加
- パス間依存解決
- リソース Read / Write 宣言
- 自動実行順決定
- 最低限のリソースバリア自動化

### 最小構成
- `RenderGraph`
- `RenderGraphPass`
- `RenderGraphResource`
- `RenderGraphBuilder`
- `RenderGraphContext`
- `FrameBlackboard`

### パスが宣言する情報の例
- `Read(SceneDepth)`
- `Read(GBufferNormal)`
- `Write(SSAO)`
- `Write(SceneColor)`

必要に応じて以下も扱えるようにする。

- `Read(SceneColor)`
- `Read(ReflectionColor)`
- `Write(ReflectionColor)`
- `Preserve(SceneColor)`

`Preserve` の概念を持てると、Forward Composite のように既存カラーへ上乗せするパスを明示しやすい。

### 先に Graph 化しやすいパス
1. `GBufferPass`
2. `SSAOPass`
3. `DeferredLightingPass`
4. `PostEffectPass`
5. `BackBufferPass`

この5つは依存関係が比較的素直で、最初の移行対象に向いている。

---

## 6. 自動化したいバリア管理

RenderGraph 化で最初に自動化したいのは、D3D12 の全ケースではなく最低限の主要状態遷移。

### 対象にする状態
- `RENDER_TARGET`
- `DEPTH_WRITE`
- `PIXEL_SHADER_RESOURCE / NON_PIXEL_SHADER_RESOURCE`
- `UNORDERED_ACCESS`
- `PRESENT`

加えて、Depth を SRV 参照するパスでは **read-only DSV と SRV の併用を標準経路として扱う** 方針を持つ。
これにより、水面や Screen Space 系のように深度を参照しつつ深度テストも行うパスを特例ではなく通常機能として扱いやすくする。

### 最初の対象リソース
- `SceneColor`
- `GBuffer*`
- `SceneDepth`
- `SSAO`
- `BackBuffer`

### 目的
- 冗長バリアを減らす
- パス追加時の人的ミスを減らす
- 将来の Compute / RT パス統合を容易にする

---

## 7. 推奨マイルストーン

各マイルストーンの詳細は、対応するステップ別ドキュメントを参照する。

### マイルストーン 1
- `EngineSystem` からパイプライン実行責務を分離
- `RenderPipeline` を実行器として成立させる
- 詳細: [Step01_RenderPipelineExecution.md](./Step01_RenderPipelineExecution.md)

### マイルストーン 2
- RT Shadow を RenderPass 化

### マイルストーン 6
- GameView / ReflectionView / 将来の CaptureView を RenderGraph ベースの実行モデルへ統合
- Water Reflection の Scene 特例 RTT 実行を除去し、Engine 側 ReflectionView 結果として扱う
- `ReflectionColor` を Blackboard 論理リソースとして正式接続
- Step07 は旧互換経路と手動バリア削除へ集中できる状態になった
- `GeometryPass` を Composite 系パスへ分割
- 詳細: [Step02_RTShadowAndComposite.md](./Step02_RTShadowAndComposite.md)

### マイルストーン 3
- `FrameBlackboard` 導入
- パス入出力の論理名管理を開始
- 詳細: [Step03_FrameBlackboard.md](./Step03_FrameBlackboard.md)

### マイルストーン 4
- 最小 RenderGraph 導入
- `GBuffer → SSAO → DeferredLighting → PostEffect → Present` を Graph 化
- 詳細: [Step04_RenderGraphMinimum.md](./Step04_RenderGraphMinimum.md)

### マイルストーン 5
- 自動バリア導入
- `SceneColor / GBuffer / BackBuffer` を自動管理
- 全パス自動遷移へ向けた状態追跡基盤を整備
- 詳細: [Step05_AutoBarrier.md](./Step05_AutoBarrier.md)

### マイルストーン 6
- Shadow / RT / 複数 View を段階的に統合
- 特殊経路まで自動バリア対象を拡張
- 詳細: [Step06_MultiViewIntegration.md](./Step06_MultiViewIntegration.md)

### マイルストーン 7
- 旧手動実行経路を削除
- RenderGraph を唯一の正式実行経路へ移行
- 手動バリアを廃止し、自動遷移へ一本化
- 詳細: [Step07_LegacyPipelineRemoval.md](./Step07_LegacyPipelineRemoval.md)

---

## 8. 進捗管理チェックリスト

実装の進み具合が一目で分かるように、マイルストーン単位でチェックできるシートを用意する。
着手時は上から順に進め、完了した項目にチェックを入れる。

### 8-1. 全体進捗サマリー

| マイルストーン | 内容 | 状態 |
|---|---|---|
| 1 | `EngineSystem` から実行責務を分離 | ☑ 完了 |
| 2 | RT Shadow の RenderPass 化 + Composite 系整理 | ☑ 完了 |
| 3 | `FrameBlackboard` 導入 | ☑ 完了 |
| 4 | 最小 RenderGraph 導入 | ☑ 完了 |
| 5 | 自動バリア導入 | ☑ 完了 |
| 6 | Shadow / RT / 複数 View 統合 | ◐ 進行中 |
| 7 | 旧実行経路削除と RenderGraph 完全移行 | ☐ 未着手 |

### 8-1-1. ステップ別ファイル対応表

| ステップ | 詳細ファイル | 主な作業範囲 |
|---|---|---|
| 1 | `Step01_RenderPipelineExecution.md` | `EngineSystem` / `RenderPipeline` |
| 2 | `Step02_RTShadowAndComposite.md` | `RayTracingSubsystem` / `GeometryPass` / Composite 整理 |
| 3 | `Step03_FrameBlackboard.md` | `FrameBlackboard` / 論理リソース整理 |
| 4 | `Step04_RenderGraphMinimum.md` | 最小 RenderGraph / 主要5パス |
| 5 | `Step05_AutoBarrier.md` | 自動状態遷移 / read-only DSV |
| 6 | `Step06_MultiViewIntegration.md` | Shadow / RT / GameView / ReflectionView / CaptureView |
| 7 | `Step07_LegacyPipelineRemoval.md` | 旧経路削除 / RenderGraph 完全移行 |

### 8-2. マイルストーン別チェックリスト

この節は全体進捗の俯瞰用とし、各項目の詳細な作業メモ・完了条件・依存関係はステップ別ファイル側で管理する。

#### マイルストーン 1: `EngineSystem` から実行責務を分離
- [ ] `EngineSystem::ExecuteRenderPipeline()` の責務を棚卸しする
- [ ] RenderContext 構築処理とパス実行処理を分離する
- [ ] `RenderPipeline` に `Setup / Execute / Cleanup` の呼び出し責務を寄せる
- [ ] `EngineSystem` 側からパス順序の知識を減らす
- [ ] 現行の描画結果を崩していないか確認する

**着手しやすい最初の作業**
- `ExecuteRenderPipeline()` の中身を「前準備」「パス実行」「後処理」にコメント単位で整理する
- その後、`executePass` 相当の処理を `RenderPipeline` に移す

#### マイルストーン 2: RT Shadow の RenderPass 化 + Composite 系整理
- [ ] `DispatchRTShadow()` の入力依存を洗い出す
- [ ] `RTShadowPass` を新規作成する
- [ ] `GBufferPass` の後、`DeferredLightingPass` の前に `RTShadowPass` を入れる
- [ ] `EngineSystem` から RT Shadow の特例呼び出しを削除する
- [ ] `GeometryPass` の責務を整理する
- [ ] `TransparentPass` / `SkyPass` / `ParticlePass` / `UIPass` への分割方針を確定する

**着手しやすい最初の作業**
- まず `RTShadowPass` を薄いラッパーとして作り、内部で既存 `RayTracingSubsystem::DispatchRTShadow()` を呼ぶ形から始める

#### マイルストーン 3: `FrameBlackboard` 導入
- [x] フレーム内で共有したい論理リソース名を一覧化する
- [x] `SceneColor` / `SceneDepth` / `GBuffer*` / `SSAO` / `RTShadowMask` を定義する
- [x] 各パスがどのリソースを読むか書くか整理する
- [x] Manager 直参照を Blackboard 経由参照へ段階的に置き換える
- [x] 既存パスが同じ結果を出すことを確認する

**着手しやすい最初の作業**
- 先にドキュメント上で「論理リソース一覧表」を作り、その後コードへ反映する

#### マイルストーン 4: 最小 RenderGraph 導入
- [x] `RenderGraph` の最小責務を定義する
- [x] `RenderGraphPass` / `RenderGraphResource` / `RenderGraphBuilder` を設計する
- [x] `GBufferPass` を Graph 登録対象にする
- [x] `SSAOPass` を Graph 登録対象にする
- [x] `DeferredLightingPass` を Graph 登録対象にする
- [x] `PostEffectPass` を Graph 登録対象にする
- [x] `BackBufferPass` を Graph 登録対象にする
- [x] `GBuffer → SSAO → DeferredLighting → PostEffect → Present` が Graph 上で通ることを確認する

**着手しやすい最初の作業**
- 最初から全パスを移行せず、5パスだけを対象にして最小構成を成立させる

#### マイルストーン 5: 自動バリア導入
- [x] 自動化対象のリソース状態を `RTV / DSV / SRV / UAV / Present` に絞る
- [x] `SceneColor` の状態遷移を自動化する
- [x] `GBuffer*` の状態遷移を自動化する
- [x] `SceneDepth` の状態遷移を自動化する
- [x] `BackBuffer` の状態遷移を自動化する
- [x] 冗長バリアが減っているか確認する
- [x] 既存手動バリアとの二重管理がないか確認する

**補足方針**
- Step 5 は一時対策ではなく、最終的に全パスの状態遷移を自動化するための基盤段階とする
- 特殊経路への展開は Step 6、残存手動バリアの廃止は Step 7 で行う

**着手しやすい最初の作業**
- まず `SceneColor` 1本だけで状態追跡を試し、問題がなければ GBuffer に広げる

#### マイルストーン 6: Shadow / RT / 複数 View 統合
- [x] ShadowMap 系を RenderGraph / Blackboard に統合する
- [x] RT 系パスを Graph 上の正式パスへ寄せる
- [x] GameView と補助 View の扱いを共通化する
- [x] 将来の ReflectionView / CaptureView を想定した View 抽象化を行う
- [ ] 複数 View で同じ流れを再利用できるか確認する
- [ ] `ReflectionColor` / `ReflectionDepth` の Blackboard 統合を行う
- [ ] WaterReflectionPass の Scene 特例を View 実行要求へ寄せる
- [ ] 特殊経路でも自動バリアと状態追跡を共通化する

**着手しやすい最初の作業**
- まず GameView を基準に整え、補助 View の差分を Graph 側へ寄せる

**進行メモ**
- `RenderContext::viewSettings` と `RenderViewType` を導入し、View ごとの有効パスと出力先を切り替えられるようにした
- `OffscreenRenderTarget` と `OffScreenRenderTargetManager` の状態参照を共有化し、補助 View 導入後の `SceneColor` バリア衝突を抑える修正を入れた
- `ReflectionView` の実動化、`ReflectionColor/Depth` の Blackboard 統合、`WaterReflectionPass` の Scene 特例縮小まで完了した
- ReflectionView / CaptureView の実動構成と Water Reflection の統合は継続中

#### マイルストーン 7: 旧実行経路削除と RenderGraph 完全移行
- [ ] `EngineSystem` に残る旧手動パス実行ロジックを除去する
- [ ] `RenderPipeline::ExecutePass()` への直接依存箇所を洗い出す
- [ ] 補助 View の残存旧経路を廃止する
- [ ] `PassOutput` の必要性を再評価し、不要な受け渡しを削除する
- [ ] Graph 外の暫定 Blackboard / Manager 直参照を縮小する
- [ ] RenderGraph を唯一の正式実行経路として成立させる
- [ ] 残存する手動バリアと重複状態追跡コードを削除する

**着手しやすい最初の作業**
- まず GameView と補助 View の実行経路差分を一覧化し、残っている旧互換コードを棚卸しする

### 8-3. 直近の着手順

迷ったら次の順で進める。

1. [ ] `EngineSystem::ExecuteRenderPipeline()` の責務を書き出す
2. [ ] `RenderPipeline` にパス実行責務を移す
3. [ ] `RTShadowPass` を追加する
4. [ ] `GeometryPass` の責務を文書化する
5. [x] `FrameBlackboard` の論理リソース名を決める
6. [x] 最小 RenderGraph のクラス責務を定義する
7. [x] 主要リソースの自動バリアを導入する
8. [~] Shadow / RT / 複数 View の自動遷移を統合する
9. [ ] 旧手動実行経路と手動バリアを削除する

### 8-4. 完了判定の目安

各段階の完了は、次を満たしたらチェックする。

- **設計完了**: 対象クラスの責務と依存が文書化されている
- **実装完了**: コード上で対象責務の移設が終わっている
- **検証完了**: 既存描画結果が大きく崩れていない

### 8-5. 新規 Pass 追加時の確認項目

新しい描画機能を入れる際は、少なくとも以下を確認する。

- [ ] Scene / GameObject 側から CommandList を触っていないか
- [ ] Scene / GameObject 側から RenderTarget を切り替えていないか
- [ ] Read / Write する論理リソースが文書化されているか
- [ ] 対象 View が明確か
- [ ] 既存 Pass への依存順が明確か
- [ ] Barrier / State 遷移の責務が Engine 側へ閉じているか
- [ ] Debug Layer / GPU-Based Validation で検証できる状態か

---

## 9. 結論

現状は **Deferred 化の入口には入っているが、拡張型レンダリング基盤としてはまだ固定手続き寄り** である。

そのため、今後の方針は以下で進めるのがよい。

1. `EngineSystem` から実行責務を剥がす
2. RT / Forward Composite をパスとして整理する
3. パス間リソースを `FrameBlackboard` で明示化する
4. 最小 RenderGraph を導入する
5. 最低限のバリア自動化を入れる
6. 最後に旧処理を削除して RenderGraph を唯一の正式経路にする

この順で進めることで、Deferred Rendering を基盤にしつつ、
後から SSR、TAA、Water、Volumetric、RT Reflection、SSGI などを差し込みやすい構造に育てやすくなる。
