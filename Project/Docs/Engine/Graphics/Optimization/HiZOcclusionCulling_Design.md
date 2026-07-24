# Hi-Z オクルージョンカリング 設計書

作成日: 2026-07-23
親ドキュメント: [RenderingOptimization_Roadmap.md](RenderingOptimization_Roadmap.md)

## 1. 目的

カメラ内（フラスタム内）にあっても手前のオブジェクトに遮蔽されて見えていないオブジェクトの
描画をスキップし、G-Buffer パスの三角形数を削減する。

## 2. 方式選定

### 採用: 同フレーム深度 Hi-Z テスト + CPU リードバック適用（2フレーム遅延）

| 候補 | 内容 | 判断 |
|---|---|---|
| **リードバック方式（採用）** | GPU で AABB vs Hi-Z 判定 → 結果を CPU に読み戻し、2フレーム後の描画スキップに使う | 描画経路（`DrawIndexedInstanced`）を変えずに済み最小侵襲。可視判定は保守的にして遅延起因のポップを抑制 |
| ExecuteIndirect GPU 駆動 | GPU 上で可視インスタンスを圧縮し間接描画 | 遅延ゼロだが描画発行経路の全面改修が必要。メッシュレットカリング（A-3）導入時に移行 |
| D3D12 オクルージョンクエリ | HW クエリ | ドローコール数分のクエリ発行と1フレーム遅延。バッチとの相性が悪く不採用 |

### データフロー（フレーム N を基準）

```
フレーム N (CPU描画構築):
  Model::Draw
    ├─ HiZOcclusionSystem::IsVisible(id) を参照 ← フレーム N-2 の判定結果
    │    occluded なら InstanceBatchManager::Submit をスキップ
    └─ HiZOcclusionSystem::SubmitBounds(id, worldAABB) ← 今フレームの判定対象に登録
                                                        （スキップした物も毎フレーム登録し続ける）
フレーム N (GPU):
  GBufferPass (SceneDepth 完成)
    → HiZBuildPass: SceneDepth → Hi-Z ピラミッド構築 (max リダクション CS)
    → HiZCullPass:  AABB バッファ vs Hi-Z 判定 CS → 可視フラグ UAV
    → CopyResource: 可視フラグ → Readback バッファ[frameIndex]
フレーム N+2 (CPU、同じ frameIndex が回ってきた時):
  フェンス完了済みの Readback[frameIndex] を Map → 可視フラグ表を更新
```

- フレームバッファリングは 2（`EngineConfig.h` frameCount=2）。バックバッファ index が
  同じ値に戻った時点でそのフレームの GPU 完了は `CommandManager::WaitForFrame` により保証される
  （`InstanceBatchManager::frameResources_` と同じリング方式）ため、追加のフェンス待ちは不要。
- **判定自体は「そのフレームの深度 × そのフレームの行列」で行うため正確**。古くなるのは
  「結果を適用するのが2フレーム後」という点のみ。

## 3. 深度規約と判定式

- 本エンジンは**標準 Z**（クリア値 1.0、near=0 / far=1。reversed-Z ではない）。
  `DepthStencilManager.cpp:96` で `ClearDepthStencilView(..., 1.0f, ...)` を確認済み。
- したがって Hi-Z ピラミッドは **max リダクション**（タイル内の最遠深度を保持）。
- 可視判定: ワールド AABB の 8 頂点を ViewProjection でクリップ空間へ投影し
  - いずれかの頂点が near 平面より手前（w <= 0）→ **可視**（保守的）
  - スクリーン矩形を求め、**矩形が 4 テクセル以下に収まるミップ**を選択し、
    矩形が覆うテクセル範囲（通常 5x5 以下、上限 8x8）をループして max 深度 `hizMax` を取る
  - `minZ <= hizMax + ε` なら**可視**
  - ※ 初版は「1 テクセル以下に収まる粗いミップ + 2x2 サンプル」だったが、footprint が
    実矩形の外側（空・遠景の深度 1.0 付近）まで拾い**カリング率が 0% になる**ため、
    細ミップ + 正確な範囲ループへ変更（2026-07-23）
- ε（深度バイアス）と矩形の 1 テクセル拡張で保守側に倒し、2フレーム遅延による
  ディスオクルージョン時のポップインを緩和する。

## 4. リソース設計

| リソース | 形式 | 個数 | 備考 |
|---|---|---|---|
| Hi-Z ピラミッド | `R32_FLOAT` Texture2D フルミップチェーン | 1 | mip0 = 深度解像度の 1/2（512〜1024 級）。per-mip SRV/UAV ハンドル配列を保持（FFT 法線ミップ生成と同型） |
| AABB バッファ | `StructuredBuffer<BoundsData>` Upload | frameCount | BoundsData = { float3 min; float3 max; }（ワールド空間）。永続 Map |
| 可視フラグ | `RWStructuredBuffer<uint>` Default | 1 | 1 要素 = 1 判定対象。判定 CS が 0/1 を書く |
| Readback | Readback バッファ | frameCount | 可視フラグのコピー先。`FFTOceanReadbackHelper` のパターンを流用 |
| カリング定数 | CBV | frameCount | viewProj / Hi-Z 解像度 / ミップ数 / 判定対象数 / ε |

上限: 判定対象は初期実装で 4096 スロット（`kMaxOcclusionQueries`）。超過分は常に可視扱い。

## 5. シェーダー

### `Engine/Assets/Shaders/Culling/HiZBuild.CS.hlsl`
- パス1: SceneDepth SRV → mip0 へ 2x2 max ダウンサンプル
- パス2以降: mip(n-1) SRV → mip(n) UAV へ 2x2 max（奇数サイズは端テクセルをクランプ吸収）
- ドライバ側はミップごとに Subresource 指定の遷移バリア + UAV バリア
  （実例: `FFTOceanManager::DispatchNormalMipGenPass`, FFTOceanManager.cpp:809-859）

### `Engine/Assets/Shaders/Culling/HiZOcclusionCull.CS.hlsl`
- 1 スレッド = 1 判定対象。§3 の判定式を実装し可視フラグ UAV へ 0/1 を書く。

## 6. エンジン統合

### 新規クラス
- `Engine/Src/Graphics/Render/Culling/HiZOcclusionSystem.h/.cpp`
  - `uint32_t RegisterTarget()` / `void UnregisterTarget(uint32_t id)` — スロット管理
  - `bool IsVisible(uint32_t id) const` — 直近リードバック結果（既定: 可視）
  - `void SubmitBounds(uint32_t id, const BoundingBox& worldAABB)` — 今フレームの判定対象登録
  - `void SetApplyEnabled(bool)` — **メイン GameView の描画構築中のみ true**
  - `BuildAndCull(cmdList, ...)` — Hi-Z 構築 + 判定 CS + Readback コピー（パスから呼ぶ）
  - `ApplyReadback(frameIndex)` — フレーム先頭で可視フラグ表を更新
- `Engine/Src/Graphics/Render/Pass/HiZOcclusionPass.h/.cpp`
  - RenderGraph パス。`DeclareResources` で `Read(SceneDepth)`。
  - 配置: `RenderPassPhase::PreLighting` の先頭（G-Buffer 完成直後、SSAO より前）
  - **GameView のみ実行**（ReflectionView / CaptureView では何もしない）

### 既存コードへのフック
| 場所 | 変更 |
|---|---|
| `Model::Draw` (Model.cpp:176-238) | Submit 前に可視判定参照＋AABB 登録（適用フラグが立っている時のみ） |
| `Model` メンバ | `occlusionId_`（初回 Draw 時に Register、デストラクタで Unregister） |
| `EngineSystem` | システム生成・パス登録・フレーム先頭で `ApplyReadback`・メインビュー実行前後で `SetApplyEnabled` 切替 |
| `EngineStats` / `EngineStatsWindow` | `occlusionCulledCount` / `occlusionTestedCount` 追加・表示、有効/無効トグル |
| vcxproj / filters | 新規ファイル手動登録（SyncFilters はファイル追加しない仕様のため） |

### ビューの扱い（重要な不変条件）
- **可視判定の適用・AABB 収集はメイン GameView のみ**。反射ビュー・キャプチャビューは
  カメラが異なるため、メインカメラ基準の判定を適用すると誤カリングになる。
- スキニングモデルは初期実装では対象外（即時描画経路のため。必要なら後続で追加）。
- 透過・水面など forward 経路も初期実装では対象外（G-Buffer 不透明のみ）。

## 7. 統計・デバッグ

- `EngineStats`: 判定対象数 / カリングされたモデル数 / スキップ三角形数（概算）
- Engine Settings ウィンドウにグローバル有効/無効トグル（問題発生時の切り分け用）
- 検証手順: 統計値の確認 → カメラを遮蔽物の裏→表に動かしてポップイン有無を目視
  （夜間シーンの検証は自動露出 +8EV で白飛びする既知の落とし穴に注意）

## 8. 実装フェーズ

| Phase | 内容 | 状態 |
|---|---|---|
| 1 | HiZBuild CS + ピラミッドリソース + パス統合 | **実装済み（2026-07-23）** |
| 2 | 判定 CS + AABB 収集 + Readback リング | **実装済み（2026-07-23）** |
| 3 | Model::Draw 適用 + トグル + 統計表示 | **実装済み（2026-07-23）** |
| 4a | サブメッシュ粒度化 + 画面占有カリング | **実装済み（2026-07-23）** |
| 4b | （将来）メッシュレット（クラスタ）粒度化 | 未着手（A-3 と統合） |

### Phase 4a: サブメッシュ粒度化 + 画面占有カリング（2026-07-23）
- **サブメッシュ単位判定**: ロード時に LOD0 インデックス範囲からサブメッシュ別ローカル AABB を算出
  （`ModelResource::subMeshLocalBounds_` 別配列 — `SubMeshData` 本体に持たせると ODR 事故になるため）。
  `Model::Draw` はサブメッシュごとに Register/Submit/判定し、遮蔽中のサブメッシュだけ `continue` で
  スキップする。モデル全体では見えていても完全に隠れている部分を個別に落とせる。
  簡略化 LOD は同一頂点の部分集合のため LOD0 の AABB で保守的に覆える。
- **画面占有カリング**: 投影矩形が縦横とも `gMinRectTexels`（既定 1 テクセル ≒ スクリーン 2px）未満の
  対象は、可視でも寄与が無いため遮蔽扱いにする（判定 CS 内、閾値 0 で無効化可能）。
  細長い物は片側が閾値を超えるため描画される。
- 統計の「判定対象数」「遮蔽スキップ数」は**サブメッシュ単位**のカウントになった。

### 実装ファイル（2026-07-23）
- `Engine/Assets/Shaders/Culling/HiZBuild.CS.hlsl` — max リダクション（端テクセルが余り領域を吸収）
- `Engine/Assets/Shaders/Culling/HiZOcclusionCull.CS.hlsl` — AABB 8頂点投影 + ミップ選択 2x2 Load 判定
- `Engine/Src/Graphics/Render/Culling/HiZOcclusionSystem.h/.cpp` — シングルトン本体
  （スロット管理・フレームリング3・Readback 適用・ピラミッド構築/判定ディスパッチ）
- `Engine/Src/Graphics/Render/Pass/HiZOcclusionPass.h/.cpp` — PreLighting(priority 5)、GameView のみ
- フック: `Model::Draw`（Model.cpp、isGBufferPass かつ IsCollectEnabled 時のみ）・
  `Model::~Model` で UnregisterTarget・`EngineSystem::ExecuteRenderPipeline`
  （BeginFrame + メインビュー前後の SetCollectEnabled）・
  `EngineSystem::BuildDefaultRenderPipeline`（パス登録）
- 統計/トグル: `EngineStats`（occlusionTestedCount / occlusionCulledCount）、
  `EngineStatsWindow` レンダリングタブに「Hi-Z オクルージョンカリング」セクション（有効チェックボックス付き）

### 実装上の決定事項
- 判定粒度はモデル単位（`ModelResource::GetLocalBoundingBox` をワールド変換）。
  サブメッシュ単位 AABB はロード時データが無いため Phase 4 送り。
- 結果が `kResultStaleFrames`(8) を超えて更新されないスロットは可視へ戻す
  （フラスタムカリングで判定対象から外れたモデルの stale 結果対策）。
- スキニングモデル・forward（透過/水面）経路は対象外。シャドウは DrawShadow 別経路で影響なし。
- Hi-Z mip0 は深度の 1/2 解像度、R32_FLOAT フルミップチェーン。フレーム間は全ミップ
  UNORDERED_ACCESS で均一化し、構築中のみミップ単位に SRV 遷移（FFT 法線ミップ生成と同型）。

## 9. 既知の制約・落とし穴

- 2フレーム遅延のため、高速なカメラ移動でのディスオクルージョン時に最大2フレームの
  ポップインが起こり得る（保守的判定 ε とテクセル拡張で緩和）。
- 判定対象はモデル単位 AABB。地形のような巨大単一モデルは常に可視となり効果が出ない
  → サブメッシュ粒度化（Phase 4）またはクアッドツリー地形（A-2）で対応。
- `ShaderReflectionBuilder` の RWStructuredBuffer 疑似 CBuffer バグ（b0 衝突）に注意。
  判定 CS のリソースレイアウトはリフレクション結果を必ず確認する。
- 構造体サイズを変更するため、ビルドは**クリーンビルド必須**（ODR 事故防止）。
- `HiZOcclusionSystem` はシングルトンのため、**EngineSystem::Finalize でデバイス破棄前に
  `Shutdown()` を明示的に呼ぶ**こと。呼ばないと静的破棄がデバイスより後になり、
  LeakChecker（ReportLiveObjects）に Hi-Z テクスチャ・バッファ・PSO が報告される。
