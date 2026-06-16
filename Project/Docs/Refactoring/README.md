# レンダリングパイプライン設計メモ

> **対象:** `Engine/Src/Graphics/Render/`、`Engine/Src/EngineSystem/EngineSystem.cpp`、`Engine/Src/EngineSystem/Subsystem/RayTracingSubsystem.cpp`  
> **目的:** 現在のレンダリングパイプライン構造を整理し、Deferred Rendering を基盤にしつつ、最終的に RenderGraph 化するための移行方針をまとめる。

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

4. **SceneView 描画（デバッグ系）**
   - 通常パイプラインとは別経路で実行

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
- SceneView の特例実行

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
GameView / SceneView / 将来の ReflectionView などを統一的に扱う構造になっていない。

---

## 3. 目指すべき設計方針

基盤は **Deferred Rendering** とし、以下を標準化する。

- **Opaque / Masked** は Deferred
- **Transparent / Water / Particle / UI** は Forward Composite
- **Screen Space 技術** は GBuffer 後に差し込む
- **RT 技術** は専用パスとして差し込む
- **ポストエフェクト** は Lighting/Composite 後に集約する

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

---

## 4. どこから直していくか

RenderGraph をいきなり入れるのではなく、**RenderGraph に載せ替え可能な構造へ先に分解する**。

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
- SceneView と GameView を同じ仕組みで扱いやすくする

### Phase 5. `FrameBlackboard` を導入する
**対象:** 新規フレーム共有リソース管理層

パス間のデータ受け渡しを、Manager 直参照ではなく論理リソース名に寄せる。

例:
- `SceneColor`
- `SceneDepth`
- `GBufferA`
- `GBufferB`
- `GBufferC`
- `Velocity`
- `SSAO`
- `ShadowMask`
- `RTShadowMask`

**到達目標:**
- パスごとの入出力を明示しやすくする

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

### マイルストーン 1
- `EngineSystem` からパイプライン実行責務を分離
- `RenderPipeline` を実行器として成立させる

### マイルストーン 2
- RT Shadow を RenderPass 化
- `GeometryPass` を Composite 系パスへ分割

### マイルストーン 3
- `FrameBlackboard` 導入
- パス入出力の論理名管理を開始

### マイルストーン 4
- 最小 RenderGraph 導入
- `GBuffer → SSAO → DeferredLighting → PostEffect → Present` を Graph 化

### マイルストーン 5
- 自動バリア導入
- `SceneColor / GBuffer / BackBuffer` を自動管理

### マイルストーン 6
- Shadow / RT / 複数 View を段階的に統合

---

## 8. 進捗管理チェックリスト

実装の進み具合が一目で分かるように、マイルストーン単位でチェックできるシートを用意する。
着手時は上から順に進め、完了した項目にチェックを入れる。

### 8-1. 全体進捗サマリー

| マイルストーン | 内容 | 状態 |
|---|---|---|
| 1 | `EngineSystem` から実行責務を分離 | ☐ 未着手 |
| 2 | RT Shadow の RenderPass 化 + Composite 系整理 | ☐ 未着手 |
| 3 | `FrameBlackboard` 導入 | ☐ 未着手 |
| 4 | 最小 RenderGraph 導入 | ☐ 未着手 |
| 5 | 自動バリア導入 | ☐ 未着手 |
| 6 | Shadow / RT / 複数 View 統合 | ☐ 未着手 |

### 8-2. マイルストーン別チェックリスト

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
- [ ] フレーム内で共有したい論理リソース名を一覧化する
- [ ] `SceneColor` / `SceneDepth` / `GBuffer*` / `SSAO` / `RTShadowMask` を定義する
- [ ] 各パスがどのリソースを読むか書くか整理する
- [ ] Manager 直参照を Blackboard 経由参照へ段階的に置き換える
- [ ] 既存パスが同じ結果を出すことを確認する

**着手しやすい最初の作業**
- 先にドキュメント上で「論理リソース一覧表」を作り、その後コードへ反映する

#### マイルストーン 4: 最小 RenderGraph 導入
- [ ] `RenderGraph` の最小責務を定義する
- [ ] `RenderGraphPass` / `RenderGraphResource` / `RenderGraphBuilder` を設計する
- [ ] `GBufferPass` を Graph 登録対象にする
- [ ] `SSAOPass` を Graph 登録対象にする
- [ ] `DeferredLightingPass` を Graph 登録対象にする
- [ ] `PostEffectPass` を Graph 登録対象にする
- [ ] `BackBufferPass` を Graph 登録対象にする
- [ ] `GBuffer → SSAO → DeferredLighting → PostEffect → Present` が Graph 上で通ることを確認する

**着手しやすい最初の作業**
- 最初から全パスを移行せず、5パスだけを対象にして最小構成を成立させる

#### マイルストーン 5: 自動バリア導入
- [ ] 自動化対象のリソース状態を `RTV / DSV / SRV / UAV / Present` に絞る
- [ ] `SceneColor` の状態遷移を自動化する
- [ ] `GBuffer*` の状態遷移を自動化する
- [ ] `SceneDepth` の状態遷移を自動化する
- [ ] `BackBuffer` の状態遷移を自動化する
- [ ] 冗長バリアが減っているか確認する
- [ ] 既存手動バリアとの二重管理がないか確認する

**着手しやすい最初の作業**
- まず `SceneColor` 1本だけで状態追跡を試し、問題がなければ GBuffer に広げる

#### マイルストーン 6: Shadow / RT / 複数 View 統合
- [ ] ShadowMap 系を RenderGraph / Blackboard に統合する
- [ ] RT 系パスを Graph 上の正式パスへ寄せる
- [ ] GameView / SceneView の扱いを共通化する
- [ ] 将来の ReflectionView / CaptureView を想定した View 抽象化を行う
- [ ] 複数 View で同じ流れを再利用できるか確認する

**着手しやすい最初の作業**
- まず GameView を基準に整え、SceneView の特例処理を後から寄せる

### 8-3. 直近の着手順

迷ったら次の順で進める。

1. [ ] `EngineSystem::ExecuteRenderPipeline()` の責務を書き出す
2. [ ] `RenderPipeline` にパス実行責務を移す
3. [ ] `RTShadowPass` を追加する
4. [ ] `GeometryPass` の責務を文書化する
5. [ ] `FrameBlackboard` の論理リソース名を決める
6. [ ] 最小 RenderGraph のクラス責務を定義する

### 8-4. 完了判定の目安

各段階の完了は、次を満たしたらチェックする。

- **設計完了**: 対象クラスの責務と依存が文書化されている
- **実装完了**: コード上で対象責務の移設が終わっている
- **検証完了**: 既存描画結果が大きく崩れていない

---

## 9. 結論

現状は **Deferred 化の入口には入っているが、拡張型レンダリング基盤としてはまだ固定手続き寄り** である。

そのため、今後の方針は以下で進めるのがよい。

1. `EngineSystem` から実行責務を剥がす
2. RT / Forward Composite をパスとして整理する
3. パス間リソースを `FrameBlackboard` で明示化する
4. 最小 RenderGraph を導入する
5. 最低限のバリア自動化を入れる

この順で進めることで、Deferred Rendering を基盤にしつつ、
後から SSR、TAA、Water、Volumetric、RT Reflection、SSGI などを差し込みやすい構造に育てやすくなる。
