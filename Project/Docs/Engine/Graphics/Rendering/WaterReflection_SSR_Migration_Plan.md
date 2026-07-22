# 水面反射の鏡像カメラ廃止（SSR移行）とレンダリング最適化計画

作成日: 2026-07-21
対象: WaterTestScene / RenderPipeline 全体

---

## 0. 現状分析（前提の整理）

実測（島4個・Debugビルド・LOD/半解像度反射導入後）:

| 項目 | 値 |
|---|---|
| FPS | 約36（≒27.7ms/frame） |
| メイン GBuffer | 約13ms（近距離LOD0のため。頂点/三角形バウンド） |
| 反射 GBuffer | 約4.7ms（LOD+1バイアス＋半解像度適用後） |
| その他パス | 各1ms前後 |

60fps（16.6ms）に対し約11ms超過。内訳から明らかなように、
**ボトルネックはピクセルシェーディングではなくジオメトリ量×描画ビュー数**。

平面反射（鏡像カメラ）は RenderGraph をもう一周実行する設計のため、
- シーンのジオメトリコストを常に約2倍払う（LODバイアスで軽減済みだが依然シーン複雑度に比例）
- GBuffer/深度/ライティングの補助ビュー用リソースを別途保持
- per-view CBV の二重書き込み問題（ちらつきバグの温床。2026-07-21に実際に発生）
という構造的コストを持つ。**反射コストをシーン複雑度から切り離す**のが本計画の目的。

---

## 1. 鏡像カメラ廃止の代替案

### 1.1 候補比較

| 方式 | コスト | 品質 | シーン複雑度依存 | 実装コスト |
|---|---|---|---|---|
| A. SSR＋天空環境マップフォールバック | 0.5〜1.5ms（固定） | 画面内の物体は正確。画面外は映らない | **なし** | 中（推奨） |
| B. DXR水面反射（RTWaterRefractionと対称） | 1〜3ms | 画面外・裏面も正確 | TLAS依存（低） | 中〜大 |
| C. 低頻度更新キューブマップのみ | ほぼ0 | 近景物体の反射が消える | なし | 小 |
| 現行: 鏡像カメラ平面反射 | 5〜9ms | 完全な鏡像 | **フル依存** | — |

**推奨: A（SSR）を主経路、ミス時フォールバックに既存の天空環境マップ（gSkyEnv）。**
将来の品質オプションとして B をトグル追加（SSRミス画素のみRTレイを撃つハイブリッドも可）。

推奨理由（本エンジン固有の事情）:
1. `Water.PS.hlsl` は既に **スクリーンUV＋法線歪みで反射テクスチャをサンプリング**しており
   （`SampleGlossyReflection(screenUV + geomNormal.xz * distort)`）、SSRへの置換は
   「テクスチャ参照→レイマーチ」の局所変更で済む。合成・圧縮（輝度ニー）・グロッシーぼかしの
   下流ロジックはそのまま使える。
2. **フォールバック経路が既に存在する**: `gSkyEnvReflectionEnabled` の IBL 経路
   （プリフィルタ済み環境マップ＋`kEnvMip`）。SSRミス時にこの経路へ落とすだけでよい。
   雲は CloudCubemapCapture 経由で環境マップに焼かれているため、空・雲の反射は維持される。
3. **SceneColorCopyPass が既に存在する**（Water フェーズ先頭、屈折用の背景複製）。
   SSR のカラーソースとしてそのまま再利用でき、追加コピー不要。
4. 水面はほぼ平面＋視線が浅い角度になりやすく、SSRの得意条件
   （反射レイが画面内の深度バッファに当たりやすい）に合致する。

### 1.2 SSR アルゴリズム詳細（Water.PS 内実装案）

反射専用パスを新設せず、**Water.PS の `gReflectionEnabled` ブロック内でレイマーチする**のが
最小構成（水面ピクセルにしか反射は要らないため、専用ハーフ解像度パスより無駄がない）。

入力（すべて既存リソース）:
- `SceneDepth`（メインビュー深度）… レイとの交差判定
- `SceneColorCopy`（水面合成前のライティング済みシーンカラー）… ヒット時の色
- `gDepthReconstruction` 相当の `viewProj` / `invViewProj`（Water.PS の cbuffer に追加。
  水面パスは GameView 限定なのでメインカメラ行列で良い）

手順:

```
1. レイ生成
   worldPos = 水面ピクセルのワールド座標（VS出力をそのまま使用）
   V = normalize(worldPos - cameraPos)
   N = 低周波法線（フレネル用のカスケード0法線 or geomNormal を使用。
       高周波法線でレイを飛ばすとノイズ状のちらつきになる）
   R = reflect(V, N)

2. スクリーンスペース線形マーチ（まず線形で実装、Hi-Zは後述の任意最適化）
   起点 p0 = worldPos, 終点 p1 = worldPos + R * kMaxRayDistance（例: 200〜500m）
   両端を viewProj で NDC→UVへ射影し、UV空間でDDA的に等間隔ステップ
   （ステップ数 16〜24。ピクセル距離でクランプ）
   各ステップ:
     rayDepth  = レイ上の点のデバイス深度（NDC.zを線形補間ではなく
                 1/viewZ の線形補間から算出。透視補正必須）
     sceneDepth = SceneDepth.SampleLevel(point, uv, 0)
     if (rayDepth > sceneDepth + bias && rayDepth - sceneDepth < thickness)
        → ヒット候補。二分探索リファイン 4〜5 回で精密化して終了

   thickness（深度の「厚み」仮定）: 0.5〜2m 程度から調整。
   薄すぎると柱の裏をすり抜け、厚すぎると手前物体が誤って伸びる。

3. 合成重み（すべて乗算してSSR寄与率 ssrWeight を作る）
   - 画面端フェード: smoothstep(0, 0.1, min(uv, 1-uv)) の xy 積
   - カメラ向きレイのフェード: saturate(dot(R, V))が負方向（カメラへ向かうレイ）は
     深度バッファに情報がなく信頼できないため減衰
   - ヒット失敗: ssrWeight = 0

4. 出力
   reflColor = lerp(SkyEnvFallback(R), SceneColorCopyのヒット色, ssrWeight)
   以降は既存の SampleGlossyReflection 相当のぼかし・輝度圧縮・
   フレネル合成をそのまま通す
```

グロッシー表現: ヒットUVを中心に既存の `kWaterReflectionBlurTexels` ポアソンぼかしを
`SceneColorCopy` に対して行えば現状の見た目を概ね維持できる。

#### 任意の品質/性能強化（Phase 2 以降）
- **Hi-Z トラバーサル**: SceneDepth の min-mip チェーンを CS で生成し、
  ミップを昇降しながら空間をスキップ。ステップ数が激減し長距離レイでも安定。
  線形マーチで品質が出るならスキップ可。
- **時間的安定化**: レイ方向に画素ごとのブルーノイズジッタ＋前フレーム反射色との
  指数移動平均。波で法線が毎フレーム動く水面ではちらつき抑制に効く。
- **ハイブリッドRT**: ssrWeight==0 の画素だけ DXR レイを撃つ
  （RTWaterRefraction のパイプラインを流用した RTWaterReflectionPass）。
  画面外の物体・カメラ背後の島も映したい場合の品質トグル。

### 1.3 既知の限界と許容判断

| 限界 | 影響 | 対処 |
|---|---|---|
| 画面外の物体が映らない | カメラ背後の島は反射に出ない | 天空フォールバック（空が支配的な水面では目立ちにくい）。必要ならハイブリッドRT |
| 手前物体の背面が映らない | 岸辺のオブジェクト直下で反射が途切れる | thickness調整＋グロッシーぼかしでごまかしが効く |
| SceneColorCopy は水面合成前の色 | 現行の反射RTも同条件（PostEffect無効）なので**劣化なし** | — |

### 1.3.5 【2026-07-21 追記・推奨変更】方式A: RTレイトレ交差＋スクリーン再投影を主経路にする

`RTWaterRefraction.hlsl` の実装を精査した結果、**SSRではなくRT反射（再投影方式）を主経路に変更**する。

根拠: 既存のRT屈折は「TraceRayで交点距離のみ取得（closesthitはhitTを返すだけ）→
交点をスクリーンへ再投影 → SceneColor から色を取得 → 深度不一致/画面外はフォールバック」
という構成であり、反射はこの**完全な対称形**（`refract()`→`reflect()`、
バイアス `-waterNormal`→`+waterNormal`、レイの TMax はそのまま）で書ける。

SSR比の優位点:
- 交差が厳密（thickness・ステップ数・Hi-Zのチューニングが原理的に不要）
- FFT波面の交点反復解法・画面端フェード（ComputeScreenBoundsFade）・
  深度不一致チェック・デバッグ可視化・Root Signature構成をすべて流用できる
- TLASは ASBuildPass で構築済み。コストは1レイ/水面ピクセルの固定コスト（既存RT屈折と同等≒1ms前後）

SSRと共通の制約（変わらない点）:
- 色はSceneColor再投影で拾うため**画面に映っていないもの（カメラ背後・物体の裏面）は映らず**、
  天空環境マップへフォールバックする。これを超えるには方式B
  （closesthit内でマテリアル/ライティングを評価するヒットシェーディング＝ミニForwardレンダラー）
  が必要で、シェーダーテーブル設計からの大工事になるため将来の品質トグルとする。

実装手順（SSR案の Phase 2 を置き換え）:
1. `RTWaterReflectionPass` を `RTWaterRefractionPass` のクローンとして新設
   （Water フェーズ、SceneColorCopy 後・WaterSurfacePass 前。出力RT: RTWaterReflection）
2. `Water.PS.hlsl` の `gReflectionEnabled` を三値化（0=IBL, 1=従来RT, 2=RT反射）してA/B比較
3. 承認後、鏡像カメラ関連を全削除（下記 1.4 の手順どおり）

注意: DXR系は手動レジスタ指定Root Signatureのため、C++側 `D3D12_ROOT_PARAMETER` /
`Num32BitValues` とシェーダー側cbufferの完全一致必須（WorldPosition廃止作業で実証済みの罠）。

### 1.4 廃止手順（実装順）

1. `Water.PS.hlsl` に SSR 経路を追加（`gReflectionEnabled` を 0/1/2 の三値化:
   0=IBLのみ, 1=従来RT, 2=SSR。**cbuffer 変更は C++ 側と一致必須**。
   A/B 比較トグルを WaterSurfaceParameterPanel に出す）
2. SSR の見た目が承認されたら:
   - `WaterTestScene::BuildRenderViewRequests` の反射リクエスト発行を削除
   - `SetupWaterReflectionView` / `RestoreWaterReflectionView` / `ApplyWaterRenderViewResult` と
     `reflectionPass_`（鏡像カメラ・クリップ平面）を削除
   - 反射専用 GBuffer/DepthStencilManager（RenderDomainContext の半解像度リソース）と
     `ReflectionView` RT、`kReflectionViewResolutionScale` を削除
   - `BaseModelRenderer::lodBias` の反射用 +1 設定（GBufferPass）を削除
3. 残してよいもの（無害・将来のCaptureViewで再利用）:
   - `RenderViewSettings::viewName` とビュー別GPU計測
   - `DeferredLightingTechnique` のビュー別 `depthReconstructionBuffers_[3]`
   - `IsEnabledForView` の仕組み

期待効果: 反射ビューの全パス（GBuffer 4.7ms＋ライティング/スカイ等）が消え、
SSR 追加コスト 1ms 前後に置換。**フレーム全体で 5〜8ms 短縮**の見込み。

---

## 2. レンダリング最適化ロードマップ

現状の支配項は「メイン GBuffer 13ms（頂点バウンド）」。効果の大きい順に並べる。

### 2.1 【最優先・無料】Release ビルドで計測し直す

現在の実測は **Debug ビルド**。Debug は
- C++ 最適化なし（Draw発行・カリング等のCPUコストが数倍）
- D3D12 デバッグレイヤー有効（GPU/CPU両方に大きなオーバーヘッド）
であり、60fps 判定を Debug で行うのは基準が誤っている。
**チューニングは Release＋デバッグレイヤー無効で計測すること。**
これだけで 60fps に到達する可能性すらある。

### 2.2 頂点バウンド対策（メイン GBuffer 13ms の削減）

1. **フラスタムカリング（サブメッシュ単位）** — 未実装なら最優先。
   サブメッシュ AABB をワールド変換して視錐台6平面と判定（CPU、1オブジェクト数十ns）。
   反射廃止後はメインビュー1本なので実装も単純。
2. **LOD 閾値のチューニング** — 現行 coverage 0.6/0.2 は保守的。
   0.75/0.35 程度へ上げて近距離でも LOD1 へ落ちやすくする
   （EngineStatsWindow の LOD別インスタンス数テーブルで効果を確認しながら調整）。
   マイクロトライアングル（1px未満の三角形）はラスタ効率を崩壊させるため、
   「画面上で三角形が平均2px以上になる」ことを目安に閾値を決める。
3. **meshoptimizer の残り2機能を適用** — 既に導入済みの vcacheoptimizer に加え:
   - `meshopt_optimizeVertexCache`（ロード時、LOD生成後の各インデックス列へ）
   - `meshopt_optimizeOverdraw` ＋ `meshopt_optimizeVertexFetch`
   頂点バウンドのシーンでは頂点キャッシュヒット率の改善がそのまま効く（数割の削減例あり）。
4. **前後ソート（early-Z 活用）** — InstanceBatch をカメラ距離で前→後にソートして
   G-Buffer 描画。深度プリパスは頂点コストを倍払うため**このシーンでは非推奨**。
   ソートだけなら頂点コスト増なしでピクセル側の無駄を削れる。
5. **頂点フォーマット圧縮**（帯域・フェッチ削減）: 法線/接線を R10G10B10A2、
   UV を half2 へ。頂点サイズ半減はフェッチバウンド時に直接効く。

### 2.3 パス単位の削減

1. **シャドウマップの静的キャッシュ**: シーンが静的（島・地形）なら、
   ライト方向・オブジェクトが変化したフレームだけ再描画し、他はコピー/再利用。
   頂点バウンドのシーンではシャドウパスも同じジオメトリを舐めるため効果大。
   併せてシャドウ描画にも LOD バイアス（+1）を適用する（反射で実証済みの手法）。
2. **ASBuildPass の確認**: 静的メッシュの BLAS が毎フレーム再構築されていないか、
   TLAS はフルビルドでなく更新（refit）になっているかを確認。
3. **CSM 分割ごとの更新間隔**（実装している場合）: 遠方カスケードは 2〜4 フレームに1回。

### 2.4 最後の手段（上記で届かない場合）

- **レンダースケール**: SceneColor を 0.85〜0.9 倍で描画し最終出力でアップスケール
  （UI/ImGui はフル解像度）。ピクセル系コストが二乗で下がる。
- **GPUオクルージョンカリング（Hi-Z）**: 前フレーム深度のミップチェーンで
  インスタンス単位の可視判定を CS で行い、ExecuteIndirect で描画。
  実装コストが大きいため、島のような「遮蔽が少ない開けたシーン」では費用対効果を要検討。

### 2.5 目標達成の見積もり

| 施策 | 期待短縮（Debug基準） |
|---|---|
| SSR移行（反射ビュー全廃） | −5〜8ms |
| Release ビルド | −20〜40%（全体） |
| フラスタムカリング＋LOD閾値＋vcache | メインGBuffer 13ms → 7〜9ms |

27.7ms − 反射廃止 − GBuffer削減 ≒ 14〜16ms（Debugですら60fps圏）。
Release では余裕を持って 60fps 到達見込み。

---

## 2.6 【実装完了・2026-07-22】DXR反射への置き換えとビフォーアフター実測

方式A（DXRレイトレ交差＋スクリーン再投影）を実装し、鏡像カメラを廃止した。

**実装内容（詳細は memory: highpoly-perf-plan）:**
- `RTWaterReflection.hlsl` / `WaterReflectionRayTracingManager` / `RTWaterReflectionPass` を新設
  （屈折パスの対称形。Waterフェーズ、SceneColorCopy後・WaterSurface前）。
- `Water.PS` の反射ブロックをRT方式へ書き換え（`gReflectionTexture` を screenUV で直接サンプル、
  alpha<0.5 のミスは `gSkyEnvironmentMap` へフォールバック）。
- `WaterTestScene::BuildRenderViewRequests` を `return {}` にして反射ビューを廃止。

**実測ビフォーアフター（Debugビルド・Small Tropical Island x4・島全景framing）:**

| 指標 | Before（鏡像カメラ） | After（DXR反射） |
|---|---|---|
| FPS | 37.6 | 約45（44〜46） |
| frame time | 約26.6ms | 約21ms |
| GBuffer（メイン） | 12.8ms | 11.7〜13.0ms（不変） |
| WaterReflection/* 全パス | 約6.3ms（GBuffer4.2+FFTOcean1.6+AtmosphereLUT0.42+他） | **0ms（消滅）** |
| RTWaterReflectionPass | — | **0.617ms（新規）** |
| ドローコール | 15バッチ / 51インスタンス | 8バッチ / 26インスタンス |
| 描画三角形数 | 1,361,908 | 1,120,072 |

**結論: 反射ビュー廃止で約6.3ms削減、RT反射追加で+0.62ms、差引 約5.6ms短縮（予測通り）。**
GPU総和 約26.5ms → 約20.9ms。視覚劣化なし（黒斑/まだら無し）、D3D12エラー0。
残る支配項はメインGBuffer 11.7ms（頂点バウンド）で、これは §2.2 のフラスタムカリング・
LOD閾値・Releaseビルドで対処する次フェーズの課題。

## 3. 実装フェーズ分割（推奨順）

| Phase | 内容 | 検証方法 |
|---|---|---|
| 1 | Release ビルド計測（基準値の取り直し） | ビュー別GPUタイマー |
| 2 | Water.PS に SSR 経路追加（三値トグルでA/B比較） | 目視＋タイマー |
| 3 | 鏡像カメラ・反射ビュー・専用リソースの削除 | タイマー（反射スロット消滅を確認） |
| 4 | フラスタムカリング＋LOD閾値調整＋meshopt残り適用 | LOD統計テーブル＋タイマー |
| 5 | シャドウキャッシュ／AS確認 | タイマー |
| 6 | （必要時）Hi-Z SSR・時間的安定化・ハイブリッドRT | 目視 |

## 4. 落とし穴（本エンジン固有）

- **cbuffer 変更の多重一致**: Water 系は `Water.PS.hlsl` / `RTWaterSurfaceCommon.hlsli` /
  C++ 側定数バッファの複数箇所一致が必須（過去に複数回踏んでいる）。
  SSR 追加時の `viewProj` / トグル追加も同様に全箇所同期すること。
- **補助ビューの per-view CBV 罠**: SSR は GameView 限定なので該当しないが、
  もし将来 CaptureView 等で水面を描くなら、共有CBV+Map/Unmap ではなく
  ビュー別バッファか Root Constants を使う（DeferredLighting のちらつきバグの教訓）。
- **夜間シーンの検証**: デバッグ可視化は自動露出 +8EV で白飛びする既知の罠があるため、
  SSR の目視検証は昼シーン基準で行い、夜は露出固定で確認する。
- **反射リソース削除の順序**: RenderGraph が `ReflectionView` RT 名を参照する箇所
  （RenderTargetDescriptor / RenderDomainContext / EngineSystem の差し替えロジック）を
  すべて外してからリソース削除しないと、未解決リソース名で Graph 構築が失敗する。
