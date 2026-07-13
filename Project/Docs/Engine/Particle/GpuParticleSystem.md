# GPUパーティクルシステム 設計・実装計画

CPUパーティクル（`ParticleSystem`）とは**別クラスとして実装**し、モジュールのパラメータ定義・プリセット・描画基盤を共有しながら段階的に機能を移植する。全モジュールのパリティ達成後に、生成側の入口（ファサード/ファクトリ）だけを統一する。

## 方針の根拠

- 既存モジュール（`NoiseModule` 等）は「データ構造体」と「CPU更新ロジック」が密結合。切り替え式を最初から狙うと9モジュール一斉HLSL移植のビッグバンになる
- CPU版は `std::vector` を `remove_if` で詰める方式で、`GetParticleCount()` / `IsFinished()` / 統計が粒子配列を直接読む。GPU版は粒子がUAV上に住み、生存数はリードバック（1〜2フレーム遅延）でしか取れないため、同一APIの透過切り替えは抽象化が漏れる
- 乱数がCPU版は `RandomGenerator`、GPU版はハッシュベースで、同一パラメータでもビット一致しない（見た目の同等性まで）

## Phase 1（最小構成）— 本ドキュメントの実装対象

「生成（Emit）→更新（Update）→描画」がGPU上で完結する最小ループを動かす。

### スコープ

- 放出: エミッター位置から球面ランダム方向、一定レート（毎秒N個）
- 更新: 等速直線運動 + 寿命 + 寿命比例のアルファフェード
- 描画: ビルボード（ViewFacing固定）、既存の `Particle.VS/PS.hlsl` を**そのまま再利用**
- 生存数の取得・ソート・モジュール類は対象外（Phase 2以降）

### 全体データフロー（毎フレーム）

```
[CPU] GpuParticleSystem::Update()
        放出レート×dt を積算 → 今フレームの emitCount を決定
[CPU] GpuParticleSystem::Draw(camera)
        定数バッファ（GpuParticleParams）へ書き込み
        （ビルボード行列・VP行列・emitCount・乱数シード等）
[GPU] GpuParticleRenderer::DrawGpu()  ※GpuParticleパス内
        1. Emit CS   : リングバッファへ emitCount 個の粒子を書き込み
        2. UAVバリア
        3. Update CS : 全スロットを更新し、描画用インスタンスバッファ
                       （ParticleForGPU互換 = WVP/World/Color）を生成
        4. インスタンスバッファを UAV → NON_PIXEL_SHADER_RESOURCE へ遷移
        5. グラフィックスPSO再バインド → DrawInstanced(6, kMaxParticles)
```

### 生存管理: リングバッファ方式（Phase 1）

- 単調増加カウンタ（UAV上の uint 1個）を `InterlockedAdd` で進め、`slot = counter % kMaxParticles` に上書き放出
- フリーリスト不要・初期化パス不要で、最小実装の失敗要因を減らす
- 制約: 放出量が容量を超えると**生存中の粒子も上書き**される（容量設計で回避、Phase 2でフリーリスト化）
- 死亡スロットは Update CS が WVP=0（縮退四角形）を書き、描画コストのみ払って非表示にする
  - `DrawInstanced` は常に `kMaxParticles` インスタンス。Phase 2 で ExecuteIndirect + 生存数カウンタに置換

### バッファ構成（GpuParticleSystem が所有）

| バッファ | ヒープ | 状態 | 内容 |
|---|---|---|---|
| particleBuffer_ | DEFAULT | UAV固定 | `GpuParticle` × kMaxParticles（位置/速度/寿命/色/スケール、64byte） |
| counterBuffer_ | DEFAULT | UAV固定 | uint × 1（放出カーソル） |
| instancingBuffer_ | DEFAULT | UAV ⇔ NON_PIXEL_SRV | `ParticleForGPU` 互換 × kMaxParticles（Update CSが生成、VSが読む） |
| paramsCB_ | UPLOAD | 永続Map | `GpuParticleParams`（下記） |

`kMaxParticles = 8192`（Phase 1 は全スロット描画のため控えめ。ExecuteIndirect導入後に拡大）

### 定数バッファ GpuParticleParams（Emit/Update 共用、b0）

```
Matrix4x4 billboardMatrix;   // ビルボード回転（平行移動なし）
Matrix4x4 viewProjection;
Vector3   emitterPosition;   float deltaTime;
Vector4   startColor;
uint      emitCount;  float startLifetime;  float startSpeed;  float startScale;
uint      maxParticles;  uint frameSeed;  uint reset;  float pad;
```

- `reset=1` の初回フレームは Emit をスキップし、Update CS が全スロットを死亡状態にゼロクリア＋カウンタを0化（DEFAULTヒープの未初期化ゴミ対策）
- 乱数は `frameSeed`（フレームカウンタ）+ スロットIDの Wang Hash + xorshift

### 初期フレーム（reset）シーケンス

```
frame 0: reset=1 → Emit省略、Update CSで全スロット死亡化+カウンタ0
frame 1以降: 通常の Emit → Update
```

### 新規ファイル

| ファイル | 内容 |
|---|---|
| `Engine/Assets/Shaders/Particle/GpuParticle.hlsli` | GpuParticle構造体・CB定義・乱数関数（Emit/Update共有） |
| `Engine/Assets/Shaders/Particle/GpuParticleEmit.CS.hlsl` | 放出CS（numthreads 64） |
| `Engine/Assets/Shaders/Particle/GpuParticleUpdate.CS.hlsl` | 更新+インスタンスデータ生成CS（numthreads 64） |
| `Engine/Src/Particle/Gpu/GpuParticleSystem.h/.cpp` | GameObject派生。バッファ/CB所有、放出レート積算、CB書き込み |
| `Engine/Src/Graphics/Render/Particle/GpuParticleRenderer.h/.cpp` | `ParticleRenderer` 派生。Compute PSO×2 + ディスパッチ + 描画 |

### 既存ファイルへの変更

- `RenderPassType.h`: `GpuParticle` を UI の後（kBuiltInCount 直前）に追加 + 文字列変換
  - 既存パスの enum 値を変えないため末尾追加とする
- `RenderManager.cpp`: パス優先度 650（Particle 600 と Sprite 700 の間）、ディスパッチ分岐追加
- `GraphicsComponentFactory.cpp`: `GpuParticleRenderer` の生成・登録
- `ParticleTestScene`: CPU版の隣（x=+3）に `GpuParticleSystem` を配置して比較確認
- `CoreEngine.vcxproj`: 新規 .cpp/.h の手動登録（.filters はプリビルドの SyncFilters が自動生成）

### 設計上の要点・落とし穴

- **シェーダーは `-Zpr`（行優先）でコンパイルされる** → CS内で行ベクトル規約（第3行=平行移動、`mul(v, M)`）のまま行列を構築してよい。CPU側 `Matrix4x4` とメモリレイアウト一致
- **描画シェーダー再利用**: Update CS の出力を `Particle.hlsli` の `ParticleForGPU` と同一レイアウトにすることで、`Particle.VS/PS.hlsl`・ルートシグネチャ・PSO（全ブレンドモード）を `ParticleRenderer` からそのまま継承
- **パス途中のCompute**: `SetPipelineState(computePso)` はグラフィックスPSOを潰すため、ディスパッチ後に `BeginPass()` を再実行してから `DrawInstanced` する（Compute/Graphics のルートシグネチャは別バインドポイントなので互いに無影響）
- **ShaderReflectionBuilder の既知バグ（修正済み）**: `RWStructuredBuffer<T>` の要素型が疑似CBufferとして b0 衝突する問題は 2026-07-09 修正済み。本実装は「RWStructuredBuffer + 実CBV(b0) 同居のCS」なので、RootSignature 構築失敗が出たら `ReflectConstantBuffers` を最初に疑う
- **CSのRootSignature**: `SkinningComputeDispatcher` と同じ構成（リフレクションベース、CBV=RootDescriptor、UAV=DescriptorTable）
- **リソース状態**: particle/counter は UAV 固定（Emit→Update 間は UAVバリアのみ）。instancing のみ毎フレーム UAV ⇔ NON_PIXEL_SHADER_RESOURCE を往復（`SkinCluster.outputBufferState` と同じ状態トラッキング方式）
- **アップロードCBは1面のみ**: エンジン既存実装（CPU版インスタンシングバッファ等）と同じ前提（フレーム完了待ちで上書き競合しない）に従う

## Phase 2: モジュール移植（実装済み・2026-07-13）

CPU版の9モジュールを `GpuParticleSystem` が**パラメータコンテナとして所有**し、
毎フレーム `Draw()` でデータをCB（`GpuParticleParams`・576バイト）へ詰め、CSがCPU版と同じ数式で適用する。
ImGui は既存モジュールの `ShowImGui()` をそのまま再利用（CollapsingHeaderで列挙）。

| モジュール | 移植状況 |
|---|---|
| Main | 全項目（各初期値＋ランダム性、gravityModifier、duration/looping/playOnAwake、maxParticles→リング実効容量） |
| Emission | rateOverTime、バースト（ループ毎に1回、burstTime発火）。CPU側 `Update()` で放出数を計算 |
| Shape | 全11形状（Point/Box/Sphere/Circle/Cone/Hemisphere/Ring/Line/Cylinder/Edge/CircleHalf）＋circlePlane＋emitFromSurface＋randomPositionRange |
| Velocity | 方向決定（固定方向/ランダム方向＋揺らぎ幅）。速さはMainのstartSpeed |
| Force | 重力×gravityModifier、風、抵抗、加速度フィールド（AABB内判定） |
| Color | initialColor→endColor のグラデーション |
| Size | endSize/endSize3D、カーブ4種（Linear/EaseIn/EaseOut/EaseInOut/Constant）、min/maxクランプ |
| Rotation | 2D/3D回転速度＋ランダム性＋回転方向モード＋OverLifetime倍率＋角度正規化。**未移植: limitRotationRange / alignToVelocity** |
| Noise | Perlinノイズ（周波数/強度/スクロール/軸別影響量/寿命減衰）。**CPU版の順列テーブルではなくハッシュ勾配方式**（見た目同等、パターンは不一致） |

- パーティクル構造体は 96 バイトに拡張（rotation / rotationSpeed / initialScale / initialColor を追加）。
  現在色・現在サイズは保存せず lifeRatio から毎フレーム導出（決定的なため）
- 乱数はCPU版 `RandomGenerator` と一致しない（Wang Hash + xorshift）。分布として同等
- レンダラー（GpuParticleRenderer）は変更なし（CB/UAVのルートパラメータ構成が同じため）

## Phase 3: フリーリスト + ExecuteIndirect + リードバック（実装済み・2026-07-13）

リングバッファ（生存粒子の上書きあり・全スロット描画）を廃止し、GPU完結の生存管理へ移行。
`kMaxParticles` は **65536** に拡大（描画コストは生存数にのみ比例）。

### カウンタバッファ（uint×4）

| index | 内容 | 寿命 |
|---|---|---|
| 0 | freeListTop（フリーリストのスタック深さ） | 永続 |
| 1 | aliveCount（生存数。Emitで+1 / 死亡で-1） | 永続 |
| 2 | drawCount（今フレームの描画インスタンス数＝コンパクション書き込みカーソル） | 毎フレーム0クリア |
| 3 | 予約 | - |

### 毎フレームのGPUシーケンス（GpuParticleRenderer::DispatchCompute）

```
0. 初回のみ: 間接引数 {6,0,0,0}・カウンタ {kMax,0,0,0}・フリーリスト {0..kMax-1} を
   アップロードバッファから CopyBufferRegion で初期化（下記「CB競合」参照）
1. drawCount(counter[2]) を CopyBufferRegion で0クリア（アップロードバッファの0領域から4B）
2. Emit CS   : aliveCount を先にインクリメントし実効容量超過なら取り消してスキップ
               → freeListTop をデクリメントしてスロットをpop（空なら取り消し）→ 粒子初期化
3. UAVバリア
4. Update CS : 更新。今フレーム死亡したスロットは freeList へ push + aliveCount 減算。
               生存粒子は drawCount を InterlockedAdd してインスタンスバッファへ**コンパクション書き込み**
               （死亡スロットの縮退四角形出力は廃止）
5. counter → COPY_SOURCE: InstanceCount(=drawCount) を間接引数バッファ(+4B)へコピー、
   counter 16B をリードバックバッファへコピー（統計用、CPUは1フレーム遅れで読む）
6. 間接引数 → INDIRECT_ARGUMENT、インスタンスバッファ → NON_PIXEL_SHADER_RESOURCE
7. 描画: ExecuteIndirect（コマンドシグネチャは DRAW 1個・ルートシグネチャなし）
```

- **落とし穴（CB競合）: 「CSのresetフラグで初回初期化」は機能しない。** CBはUPLOAD1面をCPUが毎フレーム
  上書きするため、フレームパイプライン中にGPUが読む前に一度きりの reset=1 が次フレームの 0 で潰される
  （実測: freeTop が初期化されず放出が全棄却された）。一度きりのGPU初期化は必ず CopyBufferRegion で行うこと。
  粒子バッファ本体はコミットリソースのOSゼロ初期化（lifeTime=0=死亡）に依存する
- 間接引数バッファは初回のみ全16Bコピーし、以後は InstanceCount の4Bのみ更新
- フリーリストのpopは「デクリメント→prev==0（またはアンダーフローで巨大値）なら書き戻してスキップ」方式
- 実効容量（MainModule.maxParticles）の制限は Emit CS の aliveCount チェックで厳密に守られる（CPU版と同等）
- 生存数/フリー数は ImGui のインスペクターに表示（リードバックは1フレーム遅延）
- 検証（2026-07-13）: レート20000/秒×寿命2秒で alive=39666・draw=39666・freeTop=25870（=65536-39666）と
  完全整合、約4万粒子の描画をエラー0で確認

## Phase 4: プリセット対応 + CPU/GPU入口統一（実装済み・2026-07-13）

### 共通インターフェース `IParticleSystem`（`Engine/Src/Particle/IParticleSystem.h`）

CPU版 `ParticleSystem` と GPU版 `GpuParticleSystem` の両方が実装する純粋仮想インターフェース。
Initialize / Play / Stop / SetTexture / エミッター位置 / BillboardType / BlendMode / 9モジュールのアクセッサを含む。
両クラスは `GameObject` と `IParticleSystem` の多重継承（`SetBlendMode` 等は両基底の仮想を単一実装が同時にオーバーライド）。

### 統一入口

```cpp
// シーンからはバックエンドを選ぶだけ。Initialize まで自動で行われる
particleSystem_    = CreateParticleSystem(ParticleBackend::CPU, "TestParticle");
gpuParticleSystem_ = CreateParticleSystem(ParticleBackend::GPU, "TestGpuParticle");
```

`BaseScene::CreateParticleSystem(ParticleBackend, name)` が DirectXCommon / ResourceFactory を
engine_ から取得し、`CreateObject<>` → `Initialize` まで行って `IParticleSystem*` を返す。

### プリセットの両対応

`ParticlePresetManager` の引数を `ParticleSystem*` → `IParticleSystem*` に変更。
**同じJSONプリセットをCPU/GPUで相互に保存・読み込みできる**
（検証済み: GPU側の設定を保存→CPU側へ読み込みで色・全モジュール設定が一致）。
GPU版のインスペクターにも「プリセット」セクションを追加（CPU版と同じUI）。

### ビルボードタイプのGPU対応

GPU版は ViewFacing 固定だったが、`SetBillboardType` で None / ViewFacing / YAxisOnly / ScreenAligned
に対応（CPU版 `CreateBillboardMatrix` と同じ数式をCPU側で計算してCBへ渡す。プリセット互換に必要）。

## Phase 5 以降（未実装）

1. 深度ソート（半透明品質、Bitonic Sort。加算ブレンドでは順序不問のため優先度低）
2. モデルパーティクル（ModelParticleRenderer 相当。`D3D12_DRAW_INDEXED_ARGUMENTS` の
   コマンドシグネチャ＋モデルVB/IBバインドで ExecuteIndirect の構成は流用可能）
3. GPU側の「全消去」機能（gReset 復活。CB競合対策として複数フレーム連続フラグ等が必要）

## 検証手順（Phase 1）

1. `MSBuild.exe CoreEngine.vcxproj /p:Configuration=Debug /p:Platform=x64`
2. 初期シーンを `ParticleTestScene` にして WMI デタッチ起動（作業ディレクトリ = `C:\CoreEngine\Project`）
3. 25〜30秒待機後、ウィンドウ限定スクリーンショットで CPU版（原点）と GPU版（x=+3）が並んで出ることを確認
4. `Cache/logs/` にエラー0件（特に RootSignature / DeviceRemoved）
