# モデルシステム大規模リファクタリング計画（PBR前提設計）

作成日: 2026-07-13 / 対象ブランチ: feat/model

本書は Engine/Src/Graphics/Model・Material・Render/Model・GameObject/Model 全域のレビュー結果と、
PBR（物理ベースレンダリング）を第一級市民として扱う設計への移行計画をまとめたもの。

---

## 1. 現状の構造

```
Model (インスタンス)
 ├─ ModelResource*            … 共有GPUリソース（VB/IB/テクスチャハンドル/アニメ/スケルトン/BLAS）
 ├─ MaterialInstance ×1       … PBR定数バッファ（モデル全体で1個）
 ├─ wvpResources_ ×3          … Game/Scene/Shadow スロット別 TransformationMatrix CBV
 ├─ skeleton_ / skinCluster_  … リソースからコピーしたインスタンス複製
 ├─ IAnimationController / Factory
 └─ カスタムシェーダー用ポインタ ×4 (PSO/RS/Provider/Pipeline)

ModelManager … リソースキャッシュ + Create{Static,Keyframe,Skeleton}Model + InstanceBatchManager/SkinningDispatcher 所有
ModelLoader  … assimp 読み込み（static Importer ×2）
BaseModelRenderer (ModelRenderer / SkinnedModelRenderer) … RS/PSO・シーン定数バインド・ModelDrawPacket 消費
InstanceBatchManager … 通常モデルのインスタンシング集約（キー: リソース+サブメッシュ+SRV+CBV+カスタムPSO…）
ModelGameObject / AnimatedModelObject / DynamicModelObject … テンプレートメソッド式の GameObject 層
```

描画経路が3系統に分裂している:
- 通常モデル: `Model::Draw` → `InstanceBatchManager::Submit`（遅延・バッチ描画）
- スキニング: `Model::Draw` → `BuildSkinningDrawPacket` → 即時 `BindModelDrawPacket`
- シャドウ: `Model::DrawShadow` が Model 内で直接 D3D12 コマンド発行（IA設定・RootParam解決まで）

---

## 2. レビュー結果

### 2.1 PBR正確性の問題（最優先）

| # | 問題 | 場所 |
|---|------|------|
| A1 | **MetallicRoughness のチャネル取り違え**。glTF 仕様は G=Roughness / B=Metallic だが、同じ MR テクスチャを gMetallicMap(t8)・gRoughnessMap(t9) に二重バインドし、両方 `Texture2D<float>`（= R チャネル）でサンプルしている。標準 glTF アセットでは両パラメータとも誤った値になる | BaseModelRenderer.cpp:117-130, Object3dForward.hlsli:95-97, GBuffer.PS.hlsl:59-72 |
| A2 | **マテリアルファクター未読み込み**。`MaterialAsset` はテクスチャパス5本のみで、baseColorFactor / metallicFactor / roughnessFactor / emissiveFactor / normalScale / occlusionStrength / alphaMode / doubleSided を glTF から読んでいない。テクスチャ無しマテリアルは全て「白・metallic 0・roughness 0.5」に潰れる | MaterialAsset.h, ModelLoader.cpp:130-171 |
| A3 | **factor と map の合成方式が仕様と乖離**。glTF は「factor × texture」の乗算合成だが、現状は use*Map フラグで「テクスチャか定数かの二者択一」。テクスチャ有りマテリアルの factor 調整が不可能 | MaterialConstants.h, GetPBRParameters (両シェーダー) |
| A4 | **Emissive が完全に死んでいる**。ローダーと ModelResource はロード・ハンドル保持までするが、MaterialConstants にも ModelDrawPacket にもシェーダーにも存在せず、GBuffer は emissive=0 固定書き込み。ロード分のメモリと転送が純粋な無駄 | ModelResource.cpp:148-155, GBuffer.PS.hlsl:169 |
| A5 | **全テクスチャ sRGB 強制ロード**。`WIC_FLAGS_FORCE_SRGB` により法線マップ・MR・AO などリニアデータまで sRGB デコードされる。法線・粗さが系統的に歪む。DDS キャッシュも BC3_SRGB 固定 | TextureImageProcessor.cpp:36-49, TextureDdsCacheGenerator.cpp:39 |
| A6 | **マテリアルがモデル単位で1個**。サブメッシュ（マテリアルスロット）ごとの MaterialInstance が無く、マルチマテリアルモデルはテクスチャ以外の全パラメータを共有。さらにマップ有無フラグの自動設定・`HasNormalMap()` 等は `subMeshes[0]` の materialIndex しか見ない | Model.cpp:36-57, 516-529 |
| A7 | 法線マップのフォールバックが白テクスチャ（平坦法線は (0.5,0.5,1)）。現状は useNormalMap フラグで守られているが、フラグ廃止時に事故る地雷 | ModelResource.cpp:135 |
| A8 | IBL の有効/無効が**マテリアル単位**（ShadingMode::PBR_IBL）。IBL はシーン環境の性質であり、マテリアルごとに切るものではない。`Model::IsIBLAvailable()` がレンダラーを覗くねじれ、`SetIBLEnabled` 後方互換ラッパーもここに起因 | MaterialInstance.h:55-68, Model.cpp:22-25 |
| A9 | Lambert / HalfLambert レガシーモードが「PBR専用」マテリアルに同居し、GBuffer の worldPosition.a に pixelFlag (0/2/3/4/5) のマジックナンバーで伝搬。PBR前提なら削除対象 | MaterialConstants.h:11-17, GBuffer.PS.hlsl:151-165 |
| A10 | アルファ判定が forward（`gMaterial.color.a * texture.a` ≦ cutoff）と GBuffer（`texture.a` のみ ≦ cutoff）で不一致 | Object3dForward.hlsli:384, GBuffer.PS.hlsl:118 |

### 2.2 クラス設計の問題

| # | 問題 | 場所 |
|---|------|------|
| B1 | **Model が God class**。描画・行列バッファ管理・アニメーション制御・スキニング・カスタムシェーダー配管を1クラスで担い、ヘッダー依存も肥大 | Model.h 全体 |
| B2 | **static なグローバル描画スロット** `s_currentRenderSlot_`。Draw() のデフォルト引数を実行時にグローバル変数で差し替える設計は追跡困難。BaseScene との暗黙結合 | Model.h:160-166, 198 |
| B3 | **描画経路の3分裂**（バッチ/即時/Model内直接発行）。ModelDrawPacket による「Model から D3D12 コマンド発行責務を除去する」という設計意図が中途半端。DrawShadow は IA 設定から RootParam 解決まで Model 内に残存 | Model.cpp:156-299 |
| B4 | **カスタムシェーダー配管の散乱**。Model に4ポインタ、InstanceBatchKey にも同じ4ポインタ、構築は ModelGameObject、再バインドは BaseModelRenderer。「マテリアル＝シェーダー＋パラメータ」という抽象が無いことの代償 | Model.h:168-182, InstanceBatch.h:26-29, ModelGameObject.cpp:46-71 |
| B5 | IMaterial / MaterialBase の抽象が形骸化。共通契約が SetColor/GetColor/GetGPUVirtualAddress のみで多態として使われていない。「MaterialInstance」という名前だが元になる共有 Material が存在しない | IMaterial.h, MaterialBase.h |
| B6 | **スケルトンのコピー地獄**。リソース→インスタンスへ optional コピー、`UpdateAnimation` で毎フレーム `skeleton_ = *skel`（joints vector 丸コピー）、ブレンド切替時にも `currentSkeleton` コピー | Model.cpp:66-68, 307-311, 411 |
| B7 | **キーフレーム（Node）アニメが描画に反映されない**。`Animator::GetNodeLocalMatrix` の呼び出し箇所ゼロ。`CreateKeyframeModel` は時間だけ進む死に機能 | Animator.h:45, ModelManager.cpp:79-124 |
| B8 | 死にAPI: `SetNormalMapOverride`（呼び出しゼロ）、`RenderType`/`GetRenderType`（HasSkinCluster の別名）、`SetModelResource`（materialInstance/skinCluster と不整合を起こせる危険な public setter） | Model.h:86, 40-43, 154 |
| B9 | `ModelRenderContext::IsValid()` が instanceBatchManager / skinningDispatcher を検証しないのに Draw 側は assert 依存。Model が context を値コピー保持し ModelManager と二重管理 | ModelRenderContext.h:33-41 |
| B10 | `TransformationMatrix`(320B) に `lightViewProjection` を含めて**インスタンスごとに複製**。ライトVPはシーン定数であり、per-instance データに入れる必然性が無い | TransformationMatrix.h, Model.cpp:134-148 |
| B11 | マテリアルCBが常時 Map の upload heap 単一バッファ。in-flight フレームと書き込みが競合し得る（現状は実害が出にくいだけ） | MaterialBase.h |
| B12 | ModelGameObject にインスペクター UI（Transform/Render/Texture セクション）約300行がベタ書き。マテリアルのシリアライズもフィールド手書き列挙で MaterialInstance 側に Serialize が無い | ModelGameObject.cpp:140-217, 248-494 |

### 2.3 リソース/ローダーの問題

| # | 問題 | 場所 |
|---|------|------|
| C1 | **static Assimp::Importer ×2 はスレッド非安全**。`PreloadModels` は ThreadPool で異なるパスを並列ロードするため、共有 static Importer にデータ競合の危険。さらに Importer が前回シーンを保持し続けメモリも無駄 | ModelLoader.cpp:44-117, ModelManager.cpp:313-334 |
| C2 | **2パス読み込み**。スキニング有無判定のために checkImporter でフルパース→本読み込みで再パース。ロード時間ほぼ2倍 | ModelLoader.cpp:52-103 |
| C3 | ModelData の CPU データ（vertices/indices/skinClusterData）を GPU 転送後も永続保持。スキニング/BLAS 不要な static モデルでは純粋な無駄 | ModelResource.h:157 |
| C4 | `LoadFromFile` / `LoadFromModelData` で VB/IB 作成・AABB 計算コードが丸ごと重複 | ModelResource.cpp:22-228 |
| C5 | VB/IB が upload heap のまま描画に使用（default heap + コピーなし）。帯域面で不利 | ResourceFactory 経由全般 |
| C6 | baseColor を `aiTextureType_DIFFUSE` で取得（レガシーキー）。`aiTextureType_BASE_COLOR` を優先すべき。MR も `aiTextureType_UNKNOWN` 頼み | ModelLoader.cpp:151-155 |

### 2.4 シェーダー側の問題

| # | 問題 | 場所 |
|---|------|------|
| D1 | Material 構造体・GetPBRParameters・GetNormalFromMap・Bayer 行列が Object3dForward.hlsli と GBuffer.PS.hlsl に**コピペ重複**。CPU 側 MaterialConstants 変更時に3箇所同期が必要 | 両ファイル |
| D2 | ShadingMode の意味がコメント散在（C++ enum / 各シェーダー / pixelFlag と3表現） | 同上 |

---

## 3. 目標アーキテクチャ

「PBRマテリアルを glTF 準拠のデータモデルで一級市民にする」「Model は参照の束に痩せさせる」の2軸。

```
[アセット層]  ModelAsset(CPU): MeshData + PBRMaterialDesc[] + Skeleton + Animations
                PBRMaterialDesc = ファクター一式 + テクスチャパス一式（glTF 準拠）

[リソース層]  MeshResource(GPU): VB/IB + SubMesh[] + AABB + BLAS
              PBRMaterial(GPU): PBRMaterialConstants CB + テクスチャSRVセット
                → ModelResource は MeshResource + デフォルト PBRMaterial[] の複合

[インスタンス層] ModelInstance: MeshResource* + PBRMaterial[]（スロット毎・必要時のみ複製）
              SkinnedModelInstance: + SkinCluster + AnimationPlayer 参照
              AnimationPlayer: controller/factory/switch/blend を Model から分離

[描画層]     DrawPacket 統一（Forward/GBuffer/Shadow 全パス同一経路で Submit）
              PassContext を引数で明示（static スロット廃止）
              カスタムシェーダーは「ShaderMaterial（PSO+RS+バインダ）」として Material 側へ
```

### PBRMaterialConstants（目標形・HLSL と単一ソース共有）

```hlsl
float4 baseColorFactor;     // texture と乗算
float  metallicFactor;      // MR.b と乗算
float  roughnessFactor;     // MR.g と乗算
float  occlusionStrength;   // lerp(1, AO.r, strength)
float  normalScale;
float3 emissiveFactor;      // emissive.rgb と乗算
float  alphaCutoff;
uint   flags;               // hasNormalMap / unlit / dithering など bit フラグ
float  ditheringScale;
float  iblIntensity;        // マテリアル側は強度のみ。IBL 有無はシーン側
float4x4 uvTransform;
```

- use*Map フラグは廃止し「常にサンプルして factor と乗算」（フォールバックは白1x1、法線のみ hasNormalMap ビット維持）
- MR テクスチャは 1 レジスタ `gMetallicRoughnessMap`、`.g=roughness .b=metallic` で読む
- Lambert/HalfLambert は削除（デバッグ表示が要るなら DeferredLighting のビューモードとして実装）

---

## 4. フェーズ計画

### Phase 1: PBR 正確性の修正（描画結果が正しくなる）— ✅ 完了 (2026-07-13)
1. ✅ MR テクスチャを 1 レジスタ化（`gMetallicRoughnessMap` t8, G=Roughness/B=Metallic）。t9 は `gEmissiveMap` に転用（A1）
2. ✅ ModelLoader で glTF ファクター読み込み（BASE_COLOR / METALLIC_FACTOR / ROUGHNESS_FACTOR / COLOR_EMISSIVE / GLTF_ALPHACUTOFF）+ `aiTextureType_BASE_COLOR` 優先・`METALNESS` フォールバック（A2, C6）。alphaMode/doubleSided は PSO 変更が必要なため Phase 2 以降
3. ✅ MaterialConstants を factor×map 乗算方式へ刷新。use*Map 廃止（法線のみ `useNormalMap` フラグ維持）。`ao` → `occlusionStrength`（`lerp(1, AO, strength)`）、`emissiveFactor` 追加（A3）
4. ✅ Emissive 配線: MaterialAsset → MaterialInstance → ModelDrawPacket/InstanceBatchKey → Forward 加算 / GBuffer emissiveMetallic.rgb（DeferredLighting は既に加算対応済みだった）（A4）
5. ✅ `TextureColorSpace` enum 新設。TextureManager::Load に色空間引数、normal/MR/AO を Linear ロード（WIC_FLAGS_IGNORE_SRGB / BC3_UNORM 圧縮 / `_linear.dds` キャッシュ分離 / キャッシュキー `|linear` サフィックス）（A5）
6. ✅ `ObjectMaterial.hlsli` 新設（Material 構造体・テクスチャ宣言・GetPBRParameters・GetNormalFromMap・ShouldDiscardByAlpha を共有）。forward/GBuffer のアルファ判定を `color.a × texture.a` に統一（D1, A10）

**Phase 1 実装メモ:**
- MaterialConstants の新レイアウトは ObjectMaterial.hlsli と1対1対応（144バイト）。変更時は両方同期
- Water.PS / FFTWater.PS は Object3dForward.hlsli を include しているため新レイアウトに自動追随（旧 `gMaterial.ao` 参照は `ao=1.0` 固定に変更）
- MaterialAsset のファクターデフォルトは metallic=0 / roughness=0.5（OBJ 等ファクター無し形式向け）。glTF は assimp が spec デフォルト(1.0/1.0)を補完する
- Model::Initialize は subMeshes[0] のマテリアルからファクターを適用（サブメッシュ毎対応は Phase 2）
- シリアライズ: 旧 `"ao"` キーは `occlusionStrength` として読み替え、`metallicMap/roughnessMap/aoMap` キーは廃止（読み込み時無視）、`"emissive"` 追加
- アプリ側 API: `ModelObject::SetPBRTextureMapsEnabled` → `SetNormalMapEnabled` に置換、`SetPBRParameters` の第3引数は occlusionStrength に意味変更
- 検証済み: Debug ビルド成功、TestScene（sponza glTF + PBR 球グリッド）60fps 描画・全シェーダーコンパイル成功・エラー0・`_linear.dds` キャッシュ44個生成・glTF ファクター抽出をログで確認

### Phase 2: マテリアルのスロット対応と所有権整理 — ✅ 完了 (2026-07-13)
1. ✅ Model がマテリアルスロット数分の MaterialInstance を持つ（`materialInstances_` vector、サブメッシュの materialIndex で参照）。`GetMaterial(index=0)` / `GetMaterialCount()` / `ForEachMaterial()` を追加（A6）
2. ✅ ファクター初期値を全スロットへ MaterialAsset から適用。`Has*Map(materialIndex=0)` に引数追加し [0] 固定を廃止
3. ✅ IBL をシーン側判定へ: `IBLSceneParams.sceneIBLEnabled`（レンダラーが HasIBLMaps() を書き込む）× マテリアル iblIntensity（0=オプトアウト）。ShadingMode enum・Lambert/HalfLambert 分岐（forward/GBuffer/DeferredLighting）・pixelFlag 4/5 を削除。`SetIBLEnabled` は MaterialInstance から削除（アプリ層 ModelObject/WaterPlaneObject には intensity 1/0 のラッパーとして残置）（A8, A9, D2）
4. ✅ MaterialInstance::ToJson/FromJson を実装。ModelGameObject は `"materials"` 配列でスロット毎にシリアライズ、旧 `"material"` 単一オブジェクトは全スロット適用で互換読み込み（B12 前半）
5. ✅ IMaterial.h 削除。MaterialBase は非仮想の「CB確保テンプレート」に整理。SkyBoxObject::GetMaterial は具象型返しに変更（B5）

**Phase 2 実装メモ:**
- IBL のデフォルトが「オプトイン」から「オプトアウト」に反転した。シーンに IBL マップがあれば全マテリアルに適用され、無効化したいオブジェクトは `SetIBLIntensity(0)`（InfiniteGround・水面が該当）
- pixelFlag は 0=背景 / 2=PBR(IBLオプトアウト) / 3=PBR+IBL の3値のみ。DeferredLighting は gIrradianceMap の GetDimensions でシーンIBL有無を判定
- IBLSceneParamsCPU は 32 バイトに拡張（sceneIBLEnabled 追加）。ValidateAllCBVSizes が HLSL とのサイズ一致を起動時検証する
- モデル全体への一括設定（ティント・IBL強度等）は `Model::ForEachMaterial()` を使う。`GetMaterial()`（=スロット0）への設定はマルチマテリアルモデルでは一部にしか効かない
- Shading デバッグパネルはシェーディングモード切替 → IBL 強度制御に置き換え

### Phase 3: Model の責務分割・死に機能削除 — ✅ 完了 (2026-07-13)
1. ✅ `AnimationPlayer`（Animation/AnimationPlayer.h/.cpp）を新設し、controller/factory/Switch/SwitchWithBlend/Reset/GetTime/IsFinished を Model から移設。Model は `SetAnimationPlayer`/`GetAnimationPlayer`/`UpdateAnimation`（プレイヤー更新＋SkinCluster同期）のみ保持（B1）
2. ✅ Skeleton コピー撤廃: Model の `std::optional<Skeleton> skeleton_` を削除。SkinCluster 生成はリソースのバインドポーズを直接参照し、毎フレームの姿勢反映は `UpdateSkinCluster(const Skeleton&)` にアニメーターのスケルトンを参照渡し（毎フレームの joints vector 丸コピーと Blend 切替時の余分なコピーを解消）（B6）
3. ✅ 死に機能削除: `SetNormalMapOverride`/`normalMapOverride_`・`RenderType`/`GetRenderType`・`SetModelResource`・`GetSkeleton`（呼び出しゼロ）・キーフレーム Animator クラスと `CreateKeyframeModel`（描画に反映されない死に機能。スケルトン無しモデルは警告ログ付きで静的モデルとして返す）（B7, B8）
4. ✅ `ModelRenderContext::IsComplete()` を追加（内部生成分含む全依存の検証）。`Model::Initialize` が前提条件として assert（B9）

**Phase 3 実装メモ:**
- アニメーション切り替え・ブレンドは `model->GetAnimationPlayer()->Switch()/SwitchWithBlend()` を使う（Model 直のAPIは削除済み）
- スケルトンの実体は常にコントローラー（SkeletonAnimator/AnimationBlender）が所有する。Model はコピーを持たない
- 検証済み: walk.gltf（スキニングモデル）を TestScene で一時有効化し、AnimationPlayer → SkinCluster → GPUスキニング経路でポーズが毎フレーム変化することをスクリーンショット差分（キャラ領域2948px差）で確認。60fps・全ログエラー0

### Phase 4: 描画経路の統一
1. Shadow / Skinned も DrawPacket + Submit 経路へ統一し、Model から D3D12 コマンド発行を完全排除（B3）
2. `s_currentRenderSlot_` 廃止 → PassContext（ビュー種別・パス種別）を Draw 引数で明示（B2）
3. per-instance CBV ×3 を InstanceBatchManager と同型のフレームリングバッファへ統合、TransformationMatrix から lightViewProjection を削除（B10, B11）
4. カスタムシェーダーを ShaderMaterial 化。InstanceBatchKey を「メッシュ+サブメッシュ+マテリアルID」に縮小（B4）

### Phase 5: ローダー/リソース整備
1. Assimp Importer をローカル変数化（スレッド安全）+ 2パス読み込み解消（C1, C2）
2. ModelData CPU データの解放ポリシー（static モデルは GPU 転送後に破棄、skinning/BLAS 用のみ保持）（C3）
3. LoadFromFile/LoadFromModelData の共通部抽出（C4）
4. （任意）VB/IB の default heap 化（C5）

---

## 5. 検証方針

- 各 Phase 後に既存シーン（TestScene 等）のスクリーンショット比較（PrintWindow 手法）
- Phase 1 は Damaged Helmet 等の glTF 標準サンプルで metallic/roughness/emissive の見た目検証が有効
- シリアライズ互換: Phase 2 で material JSON のキーが変わるため、旧キー読み込みフォールバックを OnDeserialize に残す
