# 水面システム コードレビュー＆リファクタリング計画

作成日: 2026-08-02
ブランチ: future/water-refactoring
調査方法: 4観点（FFTシミュレーション層 / 描画層 / RT・コースティクス層 / UI・横断関心）の全ファイルレビュー

---

## 1. 現状の全体像

規模: C++ 約40ファイル + シェーダー20本 ≒ 14,000行。

| 層 | 主要ファイル | 行数 | 状態 |
|---|---|---|---|
| シミュレーション | FFTOceanManager (.h/.cpp) | 425/1239 | Helper 6クラスに切り出し済みだが全て static 関数の「友達クラス」で、状態は Manager に残留 |
| 描画 | Water.PS.hlsl | 1226 | 責務ブロック9個が1ファイルに同居。デバッグ専用コード90行超が本体側 |
| 描画 | WaterRenderFeature / WaterPlaneObject 他 | 475/310 | 直近リファクタ（ApplyFrameBinding 単一入口化）で責務境界は比較的良好 |
| RT | RayTracingManager ×3 + RTシェーダー3本 | 各220-290 | Initialize/Dispatch が **85〜90% コピペ** |
| ライティング | DeferredLighting.PS.hlsl | 695 | うち約208行（30%）が水中処理に侵食 |
| UI | WaterTestScene 配下 7クラス | 約1,700 | FFT設定の同型構造体が3つ・手書きフィールドコピー6箇所。CVar 未移行の最後の孤島 |

依存方向は概ね健全（循環なし）。問題は「依存の向き」ではなく **①重複定義（cbuffer/構造体/波評価式）②神クラスの状態散乱 ③コピペ ④デッドコード ⑤パラメータ伝搬の多段コピー** の5つ。

関連既存計画: [WaterReflection_SSR_Migration_Plan.md](WaterReflection_SSR_Migration_Plan.md)（鏡像カメラ廃止→SSR移行、未実施）。本計画の Phase 6 で接続する。

---

## 2. レビュー所見

### 2.1 リファクタ前に直すべき実バグ

| # | 場所 | 内容 |
|---|---|---|
| B1 | RayTracingSubsystem.cpp:374 | **RTコースティクスに大気透過率未適用の生ライト色を渡している**。DeferredLighting の直接光（LightManager::GetEffectiveLightColorRGB）を「置換」する立場なのに色基準が違うため、日没時に水面線の連続性が崩れる。SS版（WaterCausticsTechnique.cpp:226）は正しい。→ LightInput 構築を共通ヘルパー1箇所へ |
| B2 | FFTOceanResourceFactory.cpp:343-348 | **スペクトル SRV が UPLOAD ヒープ側バッファに作られ、DEFAULT 側 spectrumBuffer_ は一度もコピーされない死蔵リソース**。時間発展CSが毎フレーム3カスケード×2MB をPCIe経由で読む（VB/IB UPLOADヒープ事故と同型）。→ BuildSpectrum 後に Upload→Default コピーを1回積み、SRV を DEFAULT 側へ |
| B3 | FFTOceanSpectrumDebugHelper.cpp:27-29 | CPU参照実装の h0Minus 項の実部符号が GPU 実装と不一致（プローブログが誤誘導する） |
| B4 | WaterSurfaceParameterPanelSerialization.cpp:147-161 | fftResolution の保存・復元は facade がコピーせず Manager も sanitize で拒否するため実質デッドコード。コメント「Applyが再初期化まで面倒を見る」は誤り |
| B5 | WaterCausticsRayTracingManager.cpp:103 | ペイロードサイズ float×4 宣言だが実体は float×2（反射/屈折は更新済み、ここだけ追随漏れ） |

### 2.2 構造的問題（高）

**H1. cbuffer / 構造体 / 波評価式の多重定義（事故の温床の本丸）**
- `WaterFrameConstants`: C++ (WaterSurfaceTypes.h:59) + Water.PS + Water.VS + FFTWater.VS の **4箇所**。VS 2本はフィールド名まで食い違っている（サイズが偶然一致しているだけ）
- `WaveParams` 32B レイアウト: **4型に重複**（WaterSurfaceTypes.h / WaterSurfaceData.h / Water.VS.hlsl / GerstnerWave.hlsli）
- `SpectrumSample`: 3重定義＋static_assert なしの reinterpret_cast（FFTOceanManager.cpp:774）
- FFT定数 (fftOceanEnabled/patchLength/resolution): RTシェーダー3本の b0 + C++構造体3つに重複 → これがシェーダー共通化を阻んでいる
- **Water.VS.hlsl:87-125 が GerstnerWave.hlsli と別の独自 Gerstner 実装を持つ**。今は式が一致しているが「波打ち際の線」バグ⑤⑧はまさにこの型の事故
- FFT設定: FFTOceanParameters / WaterEditorFFTSettings / FFTOceanManager::Settings の同型3構造体 + 手書きコピー6箇所（Panel L170,404,487,512,532 + Serialization L148）

**H2. FFTOceanManager の神クラス構造**
Helper 6クラスは全て static 関数で状態を持たず、リソース本体・state・SRV/UAV ハンドルが Manager に並列メンバ配列（例: spectrumA 系5並列配列×A/B）として散乱。結果 DispatchFinalizePass は13引数、CreateIntermediateTextures は15引数。デバッグ readback/プローブ（Release でも動く・関数ローカル static カウンタ4本）が本体 Dispatch に常時混入し、ヘッダ約60行を占有。

**H3. RT 3マネージャの85〜90%コピペ**
差分は「シェーダーパス・エントリ名・ペイロードサイズ・SRVテーブル名1本・root constants サイズ」のみ。反射 cbuffer が屈折とレイアウトを揃えるための gUnused0/1 を持つ事実自体が「本来1つの構成駆動ファミリ」の証拠。

**H4. デッドコード（約300行超）**
- NormalMipGen 一式: パイプラインはビルドされ**失敗すると海面全体の初期化が失敗する**のに Dispatch は呼ばれない。FFTOceanResourceFactory の CreateOutputTextures / CreateNormalMipChainViews（約160行）は呼び出し元ゼロ
- Water.PS.hlsl の SampleGlossyReflection（50行）はデバッグモード19専用
- RTWaterCaustics/RefractionPass の ReflectionView 分岐は IsEnabledForView が GameView 限定のため到達不能
- **RenderManager::DrawGeometryPass → DrawWaterQueuePass の迂回路**: 現在デッドパスだが、キュー層にビューガードがなく、誰かが呼ぶと「水面が自分の平面反射に描き込まれる」修正済みバグが即再発する

**H5. DeferredLighting.PS.hlsl への水中処理侵食（208行/30%）**
水中判定・直接光置換・濡れ暗色化・コースティクス合成・デバッグ4モード。置換は albedo/F0/metallic が要るため別パス化は不可 → 関数抽出（UnderwaterLighting.hlsli）が現実解。gWaterCaustics の GetDimensions ガード＋サンプルが3回重複。

**H6. パラメータ系の二重体制と WaterTestScene 縛り**
- 泡設定は UI→setter引数6個→WaterFrameConstants→FoamSettings の**最大4段コピー**
- CVars.json（エンジン全体）+ Water.json（水面だけ手書きセクション）の永続化二重体制。IEditorSettingsSection 実装は CVarSettingsSection と WaterSettingsSection の2つだけ
- WaterRenderFeature は AddFeature 1行で任意シーンに載るのに、**設定復元が WaterSceneController 経由でしか走らないため他シーンでは調整済みパラメータが復元されない**
- refractiveIndex が technique と RTマネージャの2つの真実の源を持つ（WaterEditorFacade.cpp:105-116）
- Application が FFTOceanManager 等エンジン内部へ直接到達（facade 自体がアプリ層にある）

### 2.3 中程度の所見（抜粋）

- Water.PS.hlsl 1226行の分割必要性（→ Phase 4）
- WaterConstantBufferSet がフレームインフライト非対応の単一 UPLOAD バッファ（時限問題）
- WaterShaderResourceBinder が毎ドロー文字列ルックアップ15回（水面1ドローなので実害小）
- デバッグ enum/名前/番号が WaterDebugViewMode.h / Water.Debug.hlsli / WaterSurfaceDebugPanel.cpp の3箇所手動同期（名前配列を **Application 側が所有**している）
- RTWaterReflection の深度不一致が2値棄却のまま（屈折側は連続信頼度化済み。縁ちらつきの潜在源）
- RTWaterCaustics.hlsl:169 `kWaterMeshSubdivisions=256` ハードコード（シーン側の変更で静かに壊れる → b1 経由で渡すべき）
- SSコースティクスは Gerstner 専用で FFT シーンでは何も出ないのに UI 上は選択可能。RT→SS フォールバック時は合成式自体が変わる
- WaterSurfacePass の DeclareResources と実際の SRV 結線（WaterRenderFeature::BuildFrameBinding）が別ファイル・別タイミング（RenderGraph 迂回）
- マップ済み UPLOAD（write-combined）メモリからの CPU 読み（FFTOceanManager.cpp:152,199）
- WaterSurfaceSimulator 抽象は名前より狭く、実態は「DXR用スナップショット生成」のみ。simulation type の enum/定数二重体制
- Root 32bit 定数 44〜48 DWORD（予算64の7割強）— 行列2本を CBV へ移すと余裕が出る
- FFTOceanManager.h:23 `patchLength=96` は形骸パラメータ（実カスケードと無関係、RT有効判定にのみ使用）
- コースティクスの見た目パラメータ（永続化対象）がデバッグパネル側にある
- WaterSceneController.cpp:43 が Initialize をクリア用途に転用

### 2.4 低（代表のみ）

既定値二重定義（マテリアル初期値の roughness が 0.04 vs 0.03 で既に食い違い）/ kEnvMipCount=5 ハードコード2箇所 / GetWaves() が cbuffer ミラーへの可変ポインタを公開 / dynamic_cast 全オブジェクト線形走査 / Align256 重複 / ログ様式の3マネージャ分裂 / directionalWeight の名前が嘘（実体は正規化バンド）/ FFTOceanFinalize の debugScale=1.0 残骸 / WaterCausticsDebug cbuffer の未使用フィールドと名前の乖離。

---

## 3. リファクタリング計画（フェーズ分け）

原則: **各フェーズは独立にマージ可能・低リスク→高リスクの順・過去に実際に事故った型（cbuffer ずれ / 基準不一致 / 反射自己描画 / UPLOAD 読み）の再発防止を最優先**。

### Phase 0: 実バグ修正 + デッドコード削除（リスク: 低、効果: 即）✅ 完了 2026-08-03

実施内容: B1〜B5 修正・NormalMipGen 一式削除・DrawWaterQueuePass に GameView ガード追加・
デバッグ4関数を Water.Debug.hlsli へ移動（dxc 検証済み）・RTパスの到達不能 ReflectionView 分岐削除。
Development クリーンビルド成功、25秒起動スモークテスト通過（spectrum copy ログで B2 の動作確認済み）。
残検証: 日没時の RT コースティクス輝度の見た目比較（B1）。

**ビルド注意**: 全体 `/t:Rebuild` は禁止 — DirectXTex の ATGDeleteShaders が Shaders/Compiled/*.inc を
消し、以後 CompileShaders が Windows 11 の NoDefaultCurrentDirectoryInExePath で 9009 失敗する。
クリーンビルドは `../generated/CoreEngine/obj/<Config>` を手動削除して `/t:Build`。
復旧: `cmd /c "cd /d externals\DirectXTex\Shaders && .\CompileShaders.cmd"`（WindowsSdkVerBinPath 必須）。
1. B1〜B5 の修正（B1 は見た目が変わるので日没シーンで前後比較を撮る）
2. NormalMipGen 一式削除（パイプライン・Provider・Dispatch関数・Factory 2関数・シェーダー）
3. DrawGeometryPass から水キュー呼び出しを外す or DrawWaterQueuePass 冒頭に viewType ガード二重化
4. SampleGlossyReflection / VisualizeDepthValue / VisualizeJacobian / VisualizeRTRefractionReason を Water.Debug.hlsli へ移動
5. 到達不能な ReflectionView 分岐の削除（ViewID 整理は Phase 3 で）

検証: debugViewMode=4 の水面線連続性（緑判定）、日没時のコースティクス輝度前後比較、フルビルド成功。

### Phase 1: 単一ソース化（リスク: 中、効果: 事故再発防止の本丸) ✅ 完了 2026-08-03

実施内容:
1. `Water/Common/WaterFrameConstants.hlsli` を新設し、b5 cbuffer を Water.PS / Water.VS / FFTWater.VS の
   3 本から include 参照へ（VS 2本のフィールド名食い違い gDebugPadding も解消）。C++ 側とは
   WaterSurfaceTypes.h の static_assert 5 本で 2 箇所一致を維持
2. Water.VS.hlsl の独自 CalcGerstnerWave を削除し GerstnerWave.hlsli
   （EvaluateGerstnerWaveOffset / AccumulateGerstnerWaveDerivatives / BuildGerstnerNormal）へ一本化。
   b4 の波構造体も GerstnerWave 型を直接使用
3. WaterWaveParam を `using WaterWaveParam = ::WaveParams` に統合（32B 同一レイアウトの重複定義を解消、
   GerstnerWaterSimulator のフィールド単位変換も削除）
4. FFTOceanManager::SpectrumSample を SpectrumBuilder の型へ一本化（reinterpret_cast 削除）。
   SpectrumSample(40B)/SimulationConstants(32B)/FoamConstants(48B・cascadeWeights offset16) に
   static_assert 追加 — **導入時に「32B と思い込んでいたが実際は 40B」を即検出し、コピーログの
   7,864,320 bytes = 256²×40×3 とも整合確認**
5. デバッグビュー名テーブルを WaterDebugViewMode.h（Engine 側）の kWaterDebugViewModeNames へ移設、
   enum に Count を追加して個数 static_assert（Application 側の手動同期テーブルを削除）
6. 既定値の一本化: WaterRenderFeature のマテリアル/スクロール/タイリング初期値を
   GetWaterPresetData(Lake) 参照へ（roughness 0.04 vs 0.03 の食い違いはプリセット側 0.03 に統一）

検証: dxc で 3 シェーダー単体コンパイル成功・Development 増分ビルド成功・起動 30 秒＋
PrintWindow キャプチャで水面描画正常（色化け・帯なし）・spectrum copy ログ正常。
1. **WaterFrameConstants.hlsli 抽出**: cbuffer 宣言を Water/Common/ へ置き PS/VS/FFTWater.VS の3本で include（HLSL側を物理的に1箇所へ）。C++ とは既存 static_assert 5本で2箇所一致を維持（WaterRefractionEncoding.hlsli / FFTOceanCascadeValues.hlsli で実証済みパターン）。WaterConstants(b4) も同様
2. **Water.VS.hlsl の独自 Gerstner 実装を削除**し GerstnerWave.hlsli の EvaluateGerstnerWaveOffset / AccumulateGerstnerWaveDerivatives へ置換（乗算順序注意: GerstnerWave.hlsli:65-67）
3. WaveParams 系 C++ 2型（WaterSurfaceTypes.h / WaterSurfaceData.h）の統合
4. SpectrumSample を SpectrumBuilder の型へ一本化 + sizeof/offsetof static_assert。SimulationConstants / FoamConstants にも static_assert 追加
5. デバッグビュー名テーブルを Engine 側（WaterDebugViewMode.h 併置）へ移し、個数 static_assert 追加
6. 既定値の一本化: マテリアル初期値→GetWaterPresetData(Lake) 参照、泡既定値コメント修正

検証: レイアウト static_assert が全構成で通ること、全デバッグビュー表示確認、Gerstner シーンの波形が前後一致（スクリーンショット比較）。

### Phase 2: FFTOceanManager 解体（リスク: 中）✅ 完了 2026-08-03

実施内容:
1. **FFTOceanGpuTexture / FFTOceanPingPong / FFTOceanSpectrumBufferSet 導入**
   （FFTOceanGpuResources.h 新設）: Manager の並列メンバ配列 約40本 → 構造体8個へ。
   DispatchHelper は「束」を受け取る本物の部品になり、DispatchEvolutionPass 10引数→7、
   DispatchIFFTPass 10→6、Finalize 13→9。ResourceFactory::CreateIntermediateTextures 15→5、
   CreateSpectrumBuffers 11→5。Manager 側の出力テクスチャは CascadeOutputTexture
   （配列SRV＋スライスUAV）に集約
2. **FFTOceanDebugProbe 抽出**（新規 .h/.cpp、vcxproj/filters 登録済み）: readback 6本・
   プローブログ・120F間引きカウンタ（旧: 関数ローカル static 4本＝インスタンス間共有の欠陥）を
   1クラスへ。CVar **"d.FFTOcean.DebugProbe"（既定 off）** でオプトイン、リードバックバッファは
   有効時に遅延生成。Manager.h は 425→約340行、Dispatch から診断コードが消滅。
   実測: Pipeline ログが常時スパム→全8行に減少（サマリ/プローブ/IFFTログはゲートで停止）
3. currentSimulationTime_ をメンバ化（write-combined UPLOAD からの CPU 読み戻しを解消）
4. **Settings::patchLength（形骸の 96.0f）を全連鎖削除**: Manager Settings →
   WaterEditorFFTSettings → UI パネル（スライダー・プリセット6種・一致判定）→
   Water.json（fftPatchLength キー）→ FFTOceanInput → RT 3マネージャ →
   RT シェーダー3本。cbuffer スロットは gFFTOceanPad0 としてレイアウト維持、
   有効判定は gFFTOceanEnabled && gFFTOceanResolution > 0 へ
5. DispatchHelper の ping-pong index1 毎フレーム COMMON 遷移を削除
   （時間発展は index0 しか書かない。無意味なバリア 2本/カスケード/フレームを解消）

検証: dxc で RT シェーダー3本 lib_6_6 コンパイル成功・Development ビルド成功・
起動→UIオートメーションで WaterTestScene へ切替→水面/岸際泡の描画正常・
BuildSpectrum 較正値がリファクタ前と完全一致・spectrum copy 正常・エラー0。
1. **`struct UavTexture2D { resource; state; srv; uav; }` を導入して並列メンバ配列を畳む** → Helper の10〜15引数爆発が消え、DispatchHelper が本物の部品になる
2. **FFTOceanDebugProbe 抽出**: readback / プローブログ / static カウンタ / LogHelper を1クラスへ吸収し CVar でオプトイン（Manager が .h/.cpp とも2〜3割痩せる）
3. currentSimulationTime_ をメンバ化（write-combined 読みの解消）
4. patchLength 形骸パラメータの削除（RT有効判定は enabled フラグ/SRV 有無へ。FFTOceanInput と cbuffer メンバも連鎖削除）
5. DispatchHelper の ping-pong index1 → COMMON 遷移の意図特定（不要なら削除、必要ならコメント化）

検証: FFT 海面の見た目不変（波高・泡・カスケード境界）、ODR 事故防止のためクリーンビルド必須。

### Phase 3: RT層統合（リスク: 中〜高）✅ 完了 2026-08-03

実施内容:
1. **構成データ駆動化**: WaterRayTracingPassBase に `RTWaterPipelineDesc`（シェーダーパス・
   エントリ名・SRVテーブル名・定数サイズだけの差分記述）と `InitializeFromDesc()` /
   `BindAndDispatchRays()` を追加。3 マネージャの Initialize（各80行）は desc 構築14行に、
   Dispatch 末尾のバインド40行は 1 呼び出しに置換（85-90% コピペの解消）。
   shaderBlob_ メンバも各マネージャから削除（InitializeFromDesc ローカル化）
2. **ViewID の 3 重定義を RTWaterViewID へ一本化**（各マネージャは using エイリアスで API 互換維持）
3. **FFT 定数を b1（WaterSurfaceConstants）へ一本化**: fftOceanEnabled / fftOceanResolution を
   b1 へ追加（+static_assert）。3 シェーダーの b0 の同フィールドはレイアウト維持のためパディング化。
   UploadSurfaceDataForDispatch は FFTOceanInput を受け取る形へ
4. **kWaterMeshSubdivisions=256 のハードコード撤廃**: WaterSurfaceData に meshSubdivisions を追加し
   WaterRenderFeature が実メッシュ解像度を毎フレーム供給 → b1 経由でシェーダーへ
   （シーン側のメッシュ変更で coverage 判定が静かに壊れる構造を解消）
5. **共通 hlsli 第2弾（RTWaterSurfaceCommon.hlsli へ集約）**: UseFFTOceanSurface / 
   EvaluateWaterOffset / EvaluateWaterNormal（旧 3+6 個の複製ラッパー）・RTWaterPayload
   （旧 3 個の同型ペイロード）・kRTReason* 失敗コード表（値は不変＝デバッグ色対応維持）・
   ComputeRTScreenBoundsFade・VisualizeRTScalar・RefineWaterSurfaceIntersection
   （フラット平面シード→3回固定点反復。屈折/反射の完全同一ループを統合）。
   **リソース宣言は共通ヘッダーに置かず引数渡し**（lib_6_6 未参照宣言の Trace 2倍問題の回避）
6. **UnderwaterLighting.hlsli 抽出**（Include/Lighting/）: BuildUnderwaterContext / 
   ApplyWetDarkening / CompositeUnderwaterCaustics / CompositeLegacyCaustics / 
   BuildUnderwaterDebugColor（モード3/4）。DeferredLighting.PS.hlsl の水中処理 約130行を関数化し、
   gWaterCaustics のサンプルを 1 回に統合。ライトループ内の置換（albedo/F0 が要る）のみ本体に残置

検証: dxc で RT 3本(lib_6_6)＋DeferredLighting(ps_6_0) コンパイル成功・Development ビルド成功・
起動して WaterTestScene で全 RT 水面パス実行を Frame Timing で確認・
水面線の連続性/濡れ暗色化/岸泡が Phase 2 基準と同一の見た目・3 マネージャ初期化ログ正常・エラー0。

未実施（任意項目として次回以降へ）: 行列2本の root constants → CBV 移動（予算緩和）、
反射の深度不一致 2 値棄却の連続ブレンド化（見た目が変わるため単独で検証すべき）。
1. **構成データ駆動化**: `RTWaterPipelineDesc { shaderPath, entryNames, payloadBytes, srvTableNames, rootConstantsBytes }` を WaterRayTracingPassBase の InitializeFromDesc() に渡す。Dispatch は BindAndDispatch(resources, srvBindings, constantsBlob) ヘルパーへ（テンプレート不要）
2. **FFT定数を b1 (WaterSurfaceData) へ移動** → UseFFTOceanSurface() ×3 と EvaluateXxxWaterOffset/Normal ×6 を RTWaterSurfaceCommon.hlsli の1組へ統合
3. 共通 hlsli 第2弾（ペイロード/miss/closesthit・ComputeScreenBoundsFade・失敗コード表・精密化ループ・VisualizeScalar）。**ただしリソース宣言は絶対に共通ヘッダーへ置かない**（lib_6_6 未参照宣言で Trace 2倍の既知問題）— 関数＋引数渡しを維持
4. ViewID の3重定義を共通 enum へ一本化
5. kWaterMeshSubdivisions を b1 経由の供給へ
6. 行列2本を root constants から CBV へ移動
7. **UnderwaterLighting.hlsli 抽出**: DeferredLighting.PS から BuildUnderwaterContext / ApplyWetDarkening / ReplaceMainLightWithCaustics / CompositeCaustics + デバッグ4モードを関数抽出。gWaterCaustics サンプル3回→1回。WaterCausticsDebug cbuffer の未使用フィールド削除＋改名（C++ と同時）
8. （任意）反射の深度不一致棄却を屈折と同じ連続ブレンドへ統一

検証: 不変条件リスト §4-C を全項目チェック。RTシェーダー変更後は必ず Trace 時間を前後計測（未参照宣言事故の検出）。水面線 debugViewMode=4、夜間の明暗斑、波打ち際の各既知バグ再現手順を一巡。

### Phase 4: Water.PS.hlsl 分割（リスク: 低〜中）✅ 完了 2026-08-03

実施内容: 1098 行 → 本体 448 行＋責務別 hlsli 4 本（コードは無変更の移動。コメントも保全）:
- **WaterColumn.hlsli**（260行）: LinearizeDepth・屈折換算・解析水柱・
  WaterColumnResult/ResolveWaterColumn。「線を出さない」中核で、4 供給源の連続合成と
  ブレンド定数（1m/4m）はここが単一の置き場所
- **WaterVolume.hlsli**（147行）: 天空光 SH 評価・水中インスキャッタ環境光・
  Beer-Lambert＋単一散乱の体積色・RT 屈折の透過色解決（IsRTColorValid 2値切替禁止の
  コメントごと移動）
- **WaterFoam.hlsli**（201行）: 泡定数一式・ノイズ/パターン/レース・瞬時＋蓄積マスク・
  岸際泡・泡色（飽和対策の「エンベロープは蓄積項のみ」規約ごと移動）
- **WaterNormals.hlsli**（128行）: 面法線（3カスケード合成＋距離フェードAA）・
  フレネル用低周波法線（kFresnelNormalFlatten=0.35 のまだら対策）
本体に残るのは リソース宣言・FresnelSchlick・反射幾何遮蔽・サングリッター・
フォワード PBR・main の合成のみ。include 順は
WaterColumn → WaterVolume → WaterFoam（Volume の SH を使う）→ WaterNormals → Water.Debug。
各ファイル冒頭に暗黙依存（資源/cbuffer/関数）を明記（Water.Debug.hlsli と同じ契約形式）。

検証: dxc 単体コンパイル成功・Development ビルド成功・実機で WaterTestScene へ切替し
泡（whitecap/岸際シート）・汀線連続性・濡れ暗色化が分割前と同一の見た目・エラー0。

### Phase 5: パラメータ系統合 — CVar 移行 + エンジン常駐化（リスク: 中、効果: UI層の大幅削減）
1. 水面パラメータ約30個を CVar 化（見た目6・水質7・泡6・FFT8・コースティクス8・DXR屈折1）。壁は3つ:
   - Vector2 型追加（scrollSpeed / uvTiling / fftWindDirection の3つ。UI/シリアライズ/レジストリの3ファイル改修）
   - enum/コンボは int CVar + NoUI フラグで専用パネル維持（Vignette パターン）
   - FFT 設定は SetSettings が再構築を伴うため revision 監視で適用
2. σa/σs は**ベース値＋濁度を CVar に持ち**、WaterRenderFeature が合成（実効値からの逆算は不能なため）
3. これにより削除可能: Water.json / WaterSettingsSection / WaterSurfaceParameterPanelSerialization.cpp（182行）/ WaterEditorFFTSettings / UIキャッシュ構造体4つの大半
4. **WaterEditor をエンジン常駐化**（Engine/Src/Editor/Environment へ。AtmosphereEditor + DebugSubsystem.cpp:159 の前例どおり）→ 他シーンでも調整値が復元される
5. refractiveIndex の真実の源を一本化（同期を一方通行に）
6. コースティクス見た目パラメータを DebugPanel から ParameterPanel へ移設
7. Gerstner 個別波と WaveToolState は CVar 不適（ワークフロー状態）— 現状どおり非永続

検証: 全パラメータの保存→再起動→復元一巡、プリセット適用→カスタム判定、復元順序（既定プリセット→Deserialize 上書き）の維持。

### Phase 6（将来・任意）
- SSR 移行（WaterReflection_SSR_Migration_Plan.md）— Phase 3 で RT 反射が整理されてから
- WaterConstantBufferSet のフレームインフライト・リングバッファ化
- SS コースティクスの扱い決定（FFT 対応 or 「RT 非対応 GPU 用」と文書化して UI で制約明示）
- WaterShaderResourceBinder のスロットキャッシュ
- WaterSurfaceSimulator の改名（IWaterSurfaceSnapshotSource）or 真の抽象化

---

## 4. 壊してはいけない不変条件（統合版）

リファクタ時は該当フェーズで必ず再確認すること。

### A. シミュレーション層
1. カスケード定数の単一情報源は `FFTOceanCascadeValues.hlsli`（C++ は FFTOceanManager.cpp:27 の直 include + static_assert）。**手コピーへ戻すの厳禁**。Src→Assets の相対 include は意図的トレードオフ
2. 泡の飽和対策3点セット: ①重み付き detJ（FoamAccumulate.CS:56-61 + ComputeFFTCombinedDetJ、weights {1,0.5,0.2} の源は WaterSurfaceTypes.h:100）②注入二乗（FoamAccumulate.CS:68-69）③蓄積参加率 {1,0.4,0}（FoamAccumulate.CS:45、シェーダー内ハードコード）
3. 泡 ping-pong: 書込先 = foamFrameIndex_&1 の純関数、フレーム内「SRV結線→泡Dispatch」順序。foamResetPending_ の3トリガと dt クランプ 0〜0.1s
4. PM較正チェーン: Hs≈0.21v²/g → targetRms=Hs/4 × kCascadeRmsShare {1,0.35,0.12} → RMS正規化。amplitudeScale は相対倍率のみ
5. カスケード毎の乱数シード必須 / 風向はカスケード格子へ順回転（シェーダー側逆回転と対）
6. 波群エンベロープは変位・勾配テンソル・傾きへ同倍率 / 頂点変位は2カスケードのみ / IFFT 正規化は最終縦ステージのみ / Finalize の checker 符号
7. FFTOceanPass はフレーム内1回 Dispatch、fftOceanSimulationTime は表示状態と独立 / SetSettings 前に WaitForPreviousFrame / 解像度は実行時変更不可

### B. 描画層
1. **反射ビューで水面を描かない**（WaterSurfacePass::IsEnabledForView が唯一のガード。ヘッダに事故機序の記録）
2. WaterFrameConstants の static_assert 5本（128B・float3 の16B境界・cameraNearZ offset 80）。フィールド追加は末尾＋assert 更新＋HLSL 3本同時
3. RT屈折 α の「光路長有効/色有効」分離は WaterRefractionEncoding.hlsli が単一情報源。**IsRTColorValid での2値切替禁止**（二重線再発）
4. 水柱厚さは連続場: 浅瀬0〜1m 解析100%、1〜4m smoothstep 遷移。岸際泡の入力は解析水深のみ。**岸際に2値切替・人工フェード持ち込み禁止**
5. 太陽下り光路吸収は DeferredLighting 側のみ（Water.PS 再導入は二重計上）
6. 合成 detJ は ComputeFFTCombinedDetJ 経由必須 / 波群エンベロープの瞬時項二重適用禁止
7. cameraNearZ/farZ は実描画カメラから毎フレーム供給、不正値は前回値保持
8. ApplyFrameBinding が外部結線の唯一の入口（シェーダーフラグは SRV 実在から導出）
9. Finalize 順序: waterPlane_=nullptr → provider 切断 → publish 取り消し
10. 反射は「置き換え」・グリッターは反射有効時のみ加算 / PSO 再構築前 WaitForPreviousFrame

### C. RT・コースティクス層
1. 幾何項の水深0.6mブレンドは **2箇所**（RTWaterCaustics.hlsl:401-405 と :577-580）— 片方だけの変更禁止
2. coverage 判定はメッシュ同一基準（kWaterMeshSubdivisions・SampleMeshVertexDisplacement・EvaluateDrawnSurfaceHeight の三角形分割一致）
3. α=coverage プロトコル: DeferredLighting は α をそのまま置換率に使う。自前水中判定の復活禁止。**影のときも α は coverage のまま**
4. baselineRadiance: rgb=0 を返せるのは遮蔽確定時のみ。失敗系は全て baseline で埋める
5. 入射点を受光点真上に固定しない（固定点反復3回）/ refract() へは lightDir（-lightDir 禁止）
6. コースティクス合成の ×kD×albedo/PI（/PI 欠落で +1.65EV の白線）/ 水上影引き継ぎは置換前直接光と同一の 0.3 フロア
7. 屈折の連続性3点: 水柱ゼロ＝光路長0でフォールバック / 失敗でも opticalPathLength 伝搬 / 読み手は colorValid で色を切り替えない
8. cbuffer の gPadding0 / gUnused0-1 はレイアウト維持用（削除禁止）
9. absorptionCoeff 同期: WaterRenderFeature → RT設定 → gAbsorptionCoeff、および b5。Water.PS と同値必須。UI が RTマネージャを直接書くの禁止（次フレームで上書き）
10. FFT サンプリングはバイリニア限定 / レイのバイアス方向（屈折 -N、反射 +N）と RAY_FLAG_NONE
11. **共通 hlsli にリソース宣言を置かない**（lib_6_6 未参照宣言で Trace 2倍）
12. 「Blackboard 未登録＝下流自動無効」のセマンティクス維持

### D. UI・設定層
1. 復元順序: Panel::Initialize（既定プリセット）→ RegisterSection（即 Deserialize 上書き）。逆は復元値がプリセットで潰れる
2. 破棄順序: UnregisterSections（最終保存）→ UI 登録解除 → Feature 破棄。Shutdown は冪等
3. Deserialize は Draw と同一経路で適用（frameCB_ 直書きショートカット禁止 — FFT再構築・泡リセットの副作用が抜ける）
4. σa/σs の永続化は必ずベース値（実効値から逆算不能）
5. 風向は正規化済みで保存（プリセット一致判定 ε=1e-3 が前提）
6. SetSettings は変更検知付きで呼ぶ（毎フレーム無条件はスペクトル再構築連発）

---

## 5. 推奨実施順序と理由

```
Phase 0（バグ+死コード）→ Phase 1（単一ソース化）→ Phase 2（Manager解体）
                                    ↘ Phase 3（RT統合）→ Phase 4（PS分割）
Phase 5（CVar+常駐化）は Phase 1 完了後ならいつでも並行可
```

- Phase 0-1 を先にやる理由: 以降の全フェーズが触る場所の「編集時に壊れる罠」（cbuffer 4箇所一致・Gerstner 二重実装・死コードの初期化失敗リスク）を先に除去する
- Phase 2 と 3 は独立（触るファイルが重ならない）
- Phase 5 は UI 層をほぼ書き直すため、エンジン側 API が安定した後（Phase 2 の Settings 整理後）が効率的
- 各フェーズ完了時にクリーンビルド必須（ODR 事故防止）+ 既知バグ再現手順の一巡（水面線 debugViewMode=4・夜間明暗斑・波打ち際・コースティクスデバッグ表示）
