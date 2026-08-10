# パイプライン自動構築システム レビュー（2026-08-09）

対象: `ShaderCompiler` / `ShaderReflectionBuilder` / `ShaderReflectionData` / `RootSignatureBuilder` / `RootSignatureConfig` / `PipelineStateBuilder` / `CustomShaderPipeline` / `ICustomShaderProvider`

観点: 設計の正しさ・汎用性・拡張性・安全性・処理コスト

---

## 0. 総評

**設計の骨格（リフレクション → RootSignature 自動生成 → PSO ビルダー）は正しい方向で、責務分割も妥当。** `RootSignatureConfig` による「戦略（RootDescriptor / DescriptorTable / StaticSampler …）を名前ベースで上書きできる」レイヤは、この規模の自作エンジンとしてはかなり良く出来ている。

一方で、**「動いているから正しい」で通ってしまっている箇所が多く、そのうち実害が出ているものが 7 件ある。**特に以下は今すぐ直す価値がある。

| # | 内容 | 影響 |
|---|---|---|
| C-1 | **Release でもシェーダーが `-Od`（最適化無効）でコンパイルされる** | 全描画のフレームタイムに直撃 |
| C-2 | **Release ではシェーダーコンパイルのエラー処理が丸ごと消える（`assert` のみ）** | 起動時 null 参照クラッシュ |
| C-3 | `IDxcBlob` の参照カウントリークが全経路にある | 恒久リーク（`-Zi` 込みで数百 KB ×本数） |
| C-4 | GroupedTable の `visibility` が「最初に見つかったリソース」で上書きされる | VS/PS 混在グループで片側がバインドされない |
| C-5 | `IndependentBlendEnable` 未設定のまま MRT 用 per-RT 書き込みマスクを設定 | 死にコード＋MRT でブレンドが全 RT に波及 |
| C-6 | PSO の RT 枚数と `OMSetRenderTargets` の枚数が別々の CVar 読みで決まる | 実行時トグルで D3D12 バリデーションエラー |
| C-7 | ブレンドモードの実行時変更がカスタムシェーダーオブジェクトに反映されない | Inspector 操作が無反応・PSO 5 個が死蔵 |

「このままで大丈夫か」の結論は **§4** に書いた。

---

## 1. Critical（実害あり）

### C-1. Release ビルドでもシェーダーが最適化されない

[ShaderCompiler.cpp:83-92](Engine/Src/Graphics/Shader/ShaderCompiler.cpp:83)

```cpp
std::vector<LPCWSTR> arguments = {
    resolvedPath.c_str(),
    L"-E", L"main",
    L"-T", profile,
    L"-Zi",  // デバッグ情報を埋め込む
    L"-Od",  // 最適化を外す      ← 構成に関係なく常に
    L"-Zpr",
};
```

構成分岐が一切ない。`CompileShaderLibrary()`（DXR）も同様。**Debug / Development / Release すべてで最適化無効・デバッグ情報埋め込みのシェーダーが動いている。**

GBuffer 5.5ms や水面のシェーダーコストを測ってきた履歴があるが、その数字はすべて `-Od` の値。`-O3` にするだけで一括で効く可能性が高い（特に水面 PS のような長い分岐つきシェーダー）。

```cpp
#if defined(_DEBUG)
    L"-Zi", L"-Od", L"-Qembed_debug",
#else
    L"-O3",
#endif
    L"-Zpr",
```

Development 構成には `_DEBUG` も `NDEBUG` も定義されていない（[CoreEngine.vcxproj:158](CoreEngine.vcxproj:158)）ので、計測用途を考えると `_DEBUG` ではなく専用マクロで切るのが安全。

---

### C-2. Release でシェーダーのエラー処理が消滅する

[ShaderCompiler.cpp:71-123](Engine/Src/Graphics/Shader/ShaderCompiler.cpp:71) はエラー処理が全部 `assert` で書かれている。

```cpp
hr = dxcUtils->LoadFile(resolvedPath.c_str(), nullptr, &shaderSource);
assert(SUCCEEDED(hr));                       // NDEBUG で消える
shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();   // ← nullptr 参照
```

Release は `NDEBUG` 定義済み（[CoreEngine.vcxproj:125](CoreEngine.vcxproj:125)）なので、**シェーダーファイルが 1 つ欠けただけで無警告クラッシュする。**

さらに壊れた連鎖がある：

1. コンパイルエラー時 → `assert(false)` が消える → 処理続行 → `shaderBlob` は nullptr のまま return
2. 呼び出し側 `assert(vertexShaderBlob != nullptr)` も消える（[ModelRenderer.cpp:20](Engine/Src/Graphics/Render/Model/ModelRenderer.cpp:20)）
3. `CreatePipelineStateDesc()` が `vs->GetBufferPointer()` を無条件参照（[PipelineStateManager.cpp:444](Engine/Src/Graphics/Pipeline/PipelineStateManager.cpp:444)）→ クラッシュ

ヘッダのコメントは「失敗時 nullptr」（[ShaderCompiler.h:23](Engine/Src/Graphics/Shader/ShaderCompiler.h:23)）と宣言しているのに、実装は「nullptr を返すか、その前に落ちるか」のどちらか。**契約と実装が食い違っている。**

対処: `assert` を早期 return + `Logger` に置換し、`CreatePipelineStateDesc` の先頭で `vs == nullptr` を弾く。

---

### C-3. `IDxcBlob` の参照リーク

`CompileShader()` は `GetOutput(DXC_OUT_OBJECT, ...)` で得た**参照所有権つき生ポインタ**を返す。全呼び出し側がこれを解放していない。

```cpp
// ModelRenderer.cpp:19 — 生ポインタのまま放置
auto vertexShaderBlob = shaderCompiler_->CompileShader(...);   // 永久リーク

// PostEffectComputeBase.cpp:19 — ComPtr 代入は AddRef するので +1 されたまま
computeShaderBlob_ = compiler.CompileShader(...);              // refcount 2、Release 1 回だけ
```

`ComPtr::operator=(T*)` は `AddRef` する仕様なので、**ComPtr に入れても直らない**（むしろ気づきにくい）。`-Zi` でデバッグ情報が埋まっているぶん 1 本あたりが太い。

対処: 戻り値を `Microsoft::WRL::ComPtr<IDxcBlob>` にする。全呼び出し側は `.Get()` 化するだけで済む（既に多くが ComPtr 受けなので差分は小さい）。

---

### C-4. GroupedTable の `visibility` が壊れる

[RootSignatureBuilder.cpp:183-222](Engine/Src/Graphics/RootSignature/RootSignatureBuilder.cpp:183)

```cpp
D3D12_SHADER_VISIBILITY visibility = groupConfig.visibility;   // 設定値
...
    if (ranges.size() == 1) {
        visibility = srv->visibility;   // ← 最初の1本で上書き。設定値は捨てられる
    }
```

グループに VS 用と PS 用のリソースを混ぜると、**テーブル全体が「最初に見つかった 1 本の visibility」になる。** 先頭が VS リソースなら PS からは一切見えず、無音で真っ黒 or デバイスリムーブ。`CreateTableGroup()` で指定した visibility が無視される点も API 契約違反。

正しくは「全メンバの visibility を畳み込み、食い違えば `ALL`」。`ShaderReflectionData::AddOrMergeBinding()`（[ShaderReflectionData.cpp:32](Engine/Src/Graphics/Shader/ShaderReflectionData.cpp:32)）では既に正しくやっているので、同じ規則をここにも適用すればよい。

---

### C-5. `IndependentBlendEnable` 未設定 + MRT

[PipelineStateManager.cpp:351-366](Engine/Src/Graphics/Pipeline/PipelineStateManager.cpp:351)

```cpp
D3D12_BLEND_DESC desc{};                     // IndependentBlendEnable = FALSE
for (UINT i = 0; i < numRenderTargets_; ++i) {
    desc.RenderTarget[i].RenderTargetWriteMask = writeMask;   // ← RT[1..7] は無視される
}
```

D3D12 は `IndependentBlendEnable == FALSE` のとき **`RenderTarget[0]` しか読まない。** つまり:

- 「MRT 対応: 全アクティブスロットにライトマスクを設定する」というコメント通りの動作にはなっていない（今は全部同じマスクなので結果的に無害＝**死にコード**）
- 逆に、MRT で `kBlendModeNone` 以外を選ぶと **RT[0] のアルファブレンドが MotionVector RT にも適用される。** 水面は現状 `kBlendModeNone` なので顕在化していないだけ

さらに [CustomShaderPipeline.cpp:132-143](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.cpp:132) は MRT 2 枚を設定した直後に `BuildAllBlendModes()` を呼ぶので、**水面は起動のたびに「MRT で BuildAllBlendModes が呼ばれました。BuildGBuffer を使ってください」という自前警告を出している**（Debug 時）。設計意図と実装がねじれている。

対処: `IndependentBlendEnable = (numRenderTargets_ > 1)` を明示し、MRT 時は RT[0] 以外を `BlendEnable = FALSE` で埋める。あわせて MRT + 全ブレンドモードという組み合わせ自体を禁止するか、`BuildMRT(modes)` を用意する。

---

### C-6. PSO の RT 枚数が CVar 依存で、実行時に破綻しうる

- PSO 側: `WaterPlaneObject::WritesMotionVector()` → `WaterCVars::WriteMotionVector.Get()`（[WaterPlaneObject.cpp:20](Engine/Src/Graphics/Water/Surface/WaterPlaneObject.cpp:20)）を **オブジェクト生成時に 1 回だけ**評価
- パス側: [WaterSurfacePass.cpp:76](Engine/Src/Graphics/Render/Pass/WaterSurfacePass.cpp:76) が **毎フレーム同じ CVar を読む**

CVar は実行時に ImGui から変えられる。**変えた瞬間に PSO の `NumRenderTargets` と `OMSetRenderTargets` の枚数が食い違い、D3D12 バリデーションエラーになる。** ソース内のコメントもこの不変条件を「守れ」と書いているだけで、機構としては何も守っていない。

これは個別バグというより**設計の穴**: 「PSO を構成する状態が可変なランタイム設定に依存しているのに、無効化 → 再ビルドの経路が無い」。最低限、この CVar を再起動時のみ反映（読み取り専用表示）にするか、CVar 変更コールバックで `CustomShaderPipeline` を再ビルドする必要がある。

---

### C-7. ブレンドモードの実行時変更が効かない

[MeshRendererComponent.cpp:146](Engine/Src/GameObject/Component/Render/MeshRendererComponent.cpp:146)

```cpp
model_->SetCustomForwardPSO(customShaderPipeline_->GetForwardPSO(blendMode_));
```

初期化時の `blendMode_` で **PSO を 1 個スナップショット**して `Model` に生ポインタで持たせている。`SetBlendMode()`（[MeshRendererComponent.h:71](Engine/Src/GameObject/Component/Render/MeshRendererComponent.h:71)）は変数を書き換えるだけなので、**Inspector でブレンドモードを変えても描画は変わらない。**

同時に、`BuildAllBlendModes()` が生成した 6 個のうち 5 個は永久に使われない（≒ カスタムシェーダー 1 個につき PSO 5 個ぶんの無駄なメモリと生成時間）。

対処: 描画時に `customPipeline_->GetForwardPSO(blendMode)` を引くか、`SetBlendMode()` で PSO を差し替える。そのうえで**必要なブレンドモードだけ**ビルドする。

---

## 2. 設計面（汎用性・拡張性）

### D-1. `ICustomShaderProvider` が bool の寄せ集めになっている

現状の PSO 制御は `GetCullMode()` / `GetDepthWriteEnable()` / `WritesMotionVector()` の 3 つだけ。深度比較関数、フィルモード、深度バイアス、トポロジ、RTV フォーマット、必要なブレンドモード集合……を足すたびに **インターフェースに仮想関数が増え、`CustomShaderPipeline::BuildForwardPipeline()` の引数も増える**（すでに 8 引数）。開放閉鎖原則に反していて、この方向のまま増やすと確実に破綻する。

特に `WritesMotionVector()` は「RT レイアウトの選択」を bool で表現した漏れのある抽象化で、C-5 / C-6 の根本原因になっている。

**推奨:**

```cpp
struct ForwardPipelineDesc {
    D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK;
    bool depthWrite = true;
    D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    std::vector<DXGI_FORMAT> rtvFormats;       // 空 = エンジン既定
    std::vector<BlendMode>   blendModes{ BlendMode::kBlendModeNone };
    // 以降ここに足すだけ。インターフェースは増えない
};
virtual ForwardPipelineDesc GetForwardPipelineDesc() const { return {}; }
```

### D-2. RootSignature 設定のフックが無い（他基底クラスとの不整合）

`PostEffectComputeBase` / `PostEffectGraphicsBase` / `RenderingTechniqueBase` はいずれも `OnConfigureRootSignature(config)` を持ち、派生が自由にサンプラーや戦略を足せる。ところが **`CustomShaderPipeline` だけがフックを持たず、`MakeForwardConfig()` にアプリ固有の名前をベタ書きしている**（[CustomShaderPipeline.cpp:31-43](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.cpp:31)）:

```cpp
config.ConfigureResource("gInstanceData", BindingStrategy::RootDescriptor);
config.ConfigureSampler("gSampler",       SamplerConfig::Anisotropic());
config.ConfigureSampler("gShadowSampler", SamplerConfig::Shadow());
config.ConfigureSampler("gLinearClamp",   SamplerConfig::LinearClamp());
```

コンピュート側も同様に `gLUTSampler`（大気）と `gLinearClamp`（レンズフレア）がエンジン汎用パスに直書きされている（[CustomShaderPipeline.cpp:180-183](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.cpp:180)）。**「この名前を持たないシェーダーでは無視されるから無害」というコメントで正当化されているが、これはエンジン層に個別機能の知識が漏れている状態。** 新機能が独自サンプラーを欲しがるたびにここが伸びる。

→ `ICustomShaderProvider::OnConfigureRootSignature(RootSignatureConfig&)` を追加し、既定値だけをエンジンに残す。

### D-3. シェーダーバリアント（マクロ順列）機構が存在しない

`-D` を渡す口が `ShaderCompiler` に無い。結果として:

- 分岐はすべてランタイム `if`（`useFFTOceanNormalMap` のように CB フラグで分岐）
- でなければ `Water.VS.hlsl` / `FFTWater.VS.hlsl` のような**ファイルまるごとコピー**

シェーダーが増えるほどこのコストが効いてくる。`defines` を `CompileShader()` の引数に足し、キャッシュキーに含めるのが最小の一歩。

### D-4. コンパイル結果・RootSignature・PSO のキャッシュが無い

- `CustomShaderPipeline` は `MeshRendererComponent` ごとに `make_unique` される（[MeshRendererComponent.cpp:137](Engine/Src/GameObject/Component/Render/MeshRendererComponent.cpp:137)）
- 同じ HLSL を使うオブジェクトが N 個あれば **DXC コンパイル N 回 + RootSignature N 個 + PSO 6N 個**
- `ShaderCompiler` 自体も PostEffect / RenderingTechnique ごとにローカル生成され、**エフェクトの数だけ DXC インスタンスが作られる**（[PostEffectGraphicsBase.cpp:17](Engine/Src/Graphics/PostEffect/Effect/PostEffectGraphicsBase.cpp:17)）
- `ID3D12PipelineLibrary` / `CachedPSO` によるディスクキャッシュも無し（grep でヒット 0）
- コンパイルは全部メインスレッド直列

水面 1 枚なら誤差だが、**「カスタムシェーダーで書けるオブジェクト」という汎用機構としては、使われるほど起動時間が線形に伸びる。** `(shaderPath, defines, pipelineDescHash)` をキーにした共有キャッシュを 1 枚挟むだけで、この軸の問題はほぼ消える。

### D-5. RTV フォーマットの二重管理

[CustomShaderPipeline.cpp:135-138](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.cpp:135) が `R16G16B16A16_FLOAT` / `R16G16_FLOAT` をハードコードし、コメントで「`GBufferManager::kRenderTargetFormats` と一致させること」と書いている。`ModelRenderer` は同じ配列を正しく参照している（[ModelRenderer.cpp:113](Engine/Src/Graphics/Render/Model/ModelRenderer.cpp:113)）ので、**ここだけ単一ソース化から漏れている。** 水面リファクタで散々潰してきた「複数箇所一致の不変条件」がまた 1 つ増えている状態。

### D-6. 入力レイアウトが「リフレクション順 + APPEND」で、CPU 側と突き合わせていない

[PipelineStateManager.cpp:115-141](Engine/Src/Graphics/Pipeline/PipelineStateManager.cpp:115) は `alignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT` 固定（[ShaderReflectionData.h:43](Engine/Src/Graphics/Shader/ShaderReflectionData.h:43)）。つまり**頂点バッファのレイアウトが「HLSL の宣言順」で決まる。**

現状は全 VS が `position → texcoord → normal → tangent` で `VertexData`（[VertexData.h:9](Engine/Src/Graphics/Model/VertexData.h:9)）と一致しているので動いている。しかしこれは**規約であって検証されていない。** 誰かが HLSL 側で属性を並べ替える／途中の属性（例: texcoord）を省いた VS を書いた瞬間、オフセットがずれて**無音で壊れた頂点データを読む**。

対処は 2 択:
- リフレクションはセマンティック集合の検証だけに使い、**オフセットは C++ の `VertexData` レイアウト表から引く**（推奨）
- 少なくとも「合計ストライド == `sizeof(VertexData)`」を起動時にアサートする

### D-7. スロット自動検出が部分一致ヒューリスティック

[ShaderReflectionData.cpp:381-402](Engine/Src/Graphics/Shader/ShaderReflectionData.cpp:381)

```cpp
if (upperSemantic.find(skinSemantic) != std::string::npos) return 1;
```

`"INDEX"` の**部分一致**なので、`TEXINDEX` や `PATCHINDEX` のようなセマンティックが将来出てきたら黙ってスロット 1 に飛ぶ。完全一致テーブルにするか、そもそも D-6 と一緒に「レイアウトはプロバイダが宣言する」方式へ寄せるのが筋。

なお `ApplyAutoSlotDetection()`（非 const・破壊的）と `GetInputElementsWithAutoSlots()`（const・コピー）で同じロジックが二重に存在し、実際に使われているのは後者だけ。前者は削除候補。

### D-8. RootSignature の作り方が素朴すぎる

| 項目 | 現状 | 影響 |
|---|---|---|
| バージョン | `D3D_ROOT_SIGNATURE_VERSION_1` 固定（[RootSignatureBuilder.cpp:49](Engine/Src/Graphics/RootSignature/RootSignatureBuilder.cpp:49)） | 1.1 の `DATA_STATIC` 等が使えず、ドライバ最適化を捨てている |
| DENY フラグ | 一切使わない（既定は `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` のみ） | 未使用ステージ（HS/DS/GS/AS/MS）を deny すれば無料で軽くなる。リフレクション結果から自動導出できるのに未実装 |
| コンピュート | `PerformanceOptimized()` を流用しており IA フラグが付いたまま（[CustomShaderPipeline.cpp:174](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.cpp:174)） | 無意味なフラグ。`PostEffectComputeBase` は正しく `FLAG_NONE` にしている＝ここも不整合 |
| 64 DWORD 予算 | 検査なし | CBV を Root Descriptor（2 DWORD/本）で積むので、CBV 10 本 + SRV 15 本で 35 DWORD。超えると `D3D12SerializeRootSignature` 失敗としてしか現れず、原因が分かりにくい |
| SRV | **1 本ごとに独立した DescriptorTable** | Water.PS だけで SRV 9 本 → ルートパラメータ 9 個、ドローごとに `SetGraphicsRootDescriptorTable` 9 回。連番レジスタは 1 テーブル 1 レンジにまとめられるはず |
| 並び順 | CBV → SRV → UAV → Sampler の固定順 | 更新頻度順（低頻度を先頭）に並べるとドライバに優しい。今は意味のある順序になっていない |

`GroupedTable` の仕組み自体は用意されているのに、**エンジン内で誰も `CreateTableGroup()` を使っていない**（grep で config 定義以外にヒットなし）。せっかくの機構が死んでいる。

### D-9. 名前 → ルートパラメータの単一 map

`std::map<std::string, UINT>`（[RootSignatureBuilder.h:16](Engine/Src/Graphics/RootSignature/RootSignatureBuilder.h:16)）に `mapping[name] = currentIndex++` で入れている。

- **同名で異なるレジスタ**（例: VS の `gLight` が b3、PS の `gLight` が b4）だと 2 個のルートパラメータが作られるのに map は後勝ちで 1 個しか覚えず、**片方が永久に未バインド**になる。`AddOrMergeBinding` は bindPoint も一致しないとマージしないので、この状況は作れてしまう
- ドローごとに文字列キーで `std::map` を引く設計（`GetRootParamIndex(const std::string&)`）。`ModelRenderer` は `CacheRootParamIndices()` で回避しているが、`CustomShaderPipeline` 利用側（`WaterShaderResourceBinder` など）はキャッシュ規約が保証されていない

対処: 衝突を検出して警告 or ビルド失敗にする。参照側は起動時に index をキャッシュする規約をインターフェースで強制する（`ResolveBindings()` のような 1 回呼ぶフェーズを設ける）。

### D-10. `const_cast` による契約破り

- [RootSignatureManager.cpp:22](Engine/Src/Graphics/RootSignature/RootSignatureManager.cpp:22): `const ShaderReflectionData&` を `const_cast` して書き換える。**さらに `CustomShaderPipeline` では `reflectionData` がローカル `unique_ptr` なので、書き込んだマッピングは関数を抜けた瞬間に捨てられる**（＝完全に無駄な副作用）
- [CustomShaderPipeline.cpp:216](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.cpp:216): `GetPipelineState` が非 const なせいで `const_cast`

どちらも「const にすべき側を const にしていない」だけなので、`PipelineStateManager::GetPipelineState() const` 化と、マッピングを戻り値で受け取る形への変更で消える。

### D-11. ホットリロード・非同期コンパイルが無い

grep でヒット 0。シェーダー 1 行の調整に毎回フルビルド + 再起動が要る。リフレクション基盤があるので、`CustomShaderPipeline` 単位の再ビルドは実装コストが低い（PSO を差し替えるだけ）。水面のように試行錯誤が多い機能では効果が大きい。

---

## 3. 中〜小の指摘

| # | 箇所 | 内容 |
|---|---|---|
| M-1 | [CustomShaderPipeline.cpp:156](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.cpp:156) | `BuildComputePipeline` が `void`。CS 失敗が握り潰される。`Build()` の戻り値も `forward \|\| compute` なので**部分失敗が成功として返る** |
| M-2 | [CustomShaderPipeline.h:98](Engine/Src/Graphics/Pipeline/CustomShaderPipeline.h:98) | `hasForwardPSO_` / `hasComputePSO_` が再ビルド時にリセットされない。`forwardPsoMg_.Clear()` も呼ばれない |
| M-3 | [PipelineStateManager.cpp:286-290](Engine/Src/Graphics/Pipeline/PipelineStateManager.cpp:286) | `CreateGraphicsPipelineState` 失敗時に HRESULT もブレンドモードもログしない。上位も「Failed to build forward PSO.」だけ。デバッグ不能 |
| M-4 | [PipelineStateManager.cpp:281-283](Engine/Src/Graphics/Pipeline/PipelineStateManager.cpp:281) | `Build()` が「None 以外のブレンドモードなら深度書き込みを勝手に ZERO にする」。呼び出し側の `SetDepthStencil(true, true)` が黙って覆される暗黙の結合 |
| M-5 | [RootSignatureBuilder.cpp:218-219](Engine/Src/Graphics/RootSignature/RootSignatureBuilder.cpp:218) | `rangeStorage.back().data()` を保持している。`std::vector<std::vector<>>` の再確保時に内側がムーブされてもバッファポインタは保たれるため**現状は安全だが、`std::array` や SBO 型に変えた瞬間ダングリング**する。`std::deque` にするか `reserve()` を明示し、意図をコメントで固定すべき |
| M-6 | [RootSignatureBuilder.cpp:281](Engine/Src/Graphics/RootSignature/RootSignatureBuilder.cpp:281) | `ProcessUAVs` が `ProcessSRVs` と**同じテーブルグループを再走査**する。SRV と UAV を同居させたグループは 2 個のルートパラメータに割れる（意図的なら要コメント） |
| M-7 | [ShaderReflectionBuilder.cpp:211-215](Engine/Src/Graphics/Shader/ShaderReflectionBuilder.cpp:211) | `ConstantBuffer<T>` 構文で `ConstantBuffers` に現れない場合、`size = 0` の CBV が登録される。`ValidateCBVSize` が偽の不一致を報告し、`RootConstants` 戦略なら `Num32BitValues = 0` の不正パラメータになる |
| M-8 | [ShaderReflectionData.cpp:290](Engine/Src/Graphics/Shader/ShaderReflectionData.cpp:290) | `ValidateCBVSize` は CBV が見つからないとき **`true`（成功）を返す**。タイプミスした CBV 名は永久に検証をすり抜ける |
| M-9 | [ShaderCompiler.cpp:55](Engine/Src/Graphics/Shader/ShaderCompiler.cpp:55) | パス解決が**ファイル名のみ**。同名シェーダーが別ディレクトリにあると先勝ちで意図しない方を掴む。`Water/Surface/Water.VS.hlsl` のような相対パス指定も許すべき |
| M-10 | [ShaderReflectionData.h:134](Engine/Src/Graphics/Shader/ShaderReflectionData.h:134) | `HasBinding()` は宣言も定義もあるが**どこからも呼ばれていない**死にコード（`AddOrMergeBinding` に置き換わった残骸） |
| M-11 | [PipelineStateManager.cpp:149-158](Engine/Src/Graphics/Pipeline/PipelineStateManager.cpp:149) | ログの `std::setw` 幅計算が `55 - name.length() - format.length()` で、長い名前だと負値 → `setw(負)` は未定義ではないが枠が崩れる。診断ログとしては許容範囲だが要注意 |
| M-12 | 全体 | Development 構成は `_DEBUG` が無いので `#ifdef _DEBUG` のリフレクションログ・RootSignature ログ・誤用警告が**全部消える**。計測に使う構成でこそ欲しい診断が出ない |

---

## 4. 「今の設計のままで大丈夫か」への回答

### 汎用性 — △（限定的に大丈夫）

「HLSL を書けば RootSignature と PSO が自動で出来る」という核は正しく、実際に水面・PostEffect・RenderingTechnique・ModelRenderer が同じ基盤に乗っている。ただし **エンジン層に個別機能の名前（`gLUTSampler`, `gShadowSampler`, `gInstanceData`…）が直書きされており（D-2）、「汎用」を名乗るには漏れが多い。**フック 1 個の追加で解消する。

### 拡張性 — ✕（このままでは詰む）

3 つの壁が既に見えている。

1. **PSO パラメータを増やすたびにインターフェースと関数シグネチャが伸びる（D-1）** — 次に深度バイアスかブレンドモードを足したくなった時点で限界
2. **シェーダーバリアントが作れない（D-3）** — マテリアルの種類が増えると必ず要る
3. **キャッシュが無く、オブジェクト数に対して線形にコンパイル・PSO が増える（D-4）** — カスタムシェーダーオブジェクトを 10 個置いた時点で起動が体感で遅くなる

いずれも「今すぐ壊れる」ではないが、**今の作りのまま機能を足していくと後戻りコストが上がり続ける**タイプの負債。

### 安全性 — ✕（要修正）

- Release で**シェーダー関連のエラー処理が丸ごと消える**（C-2）のは、出荷構成としてそのままにはできない
- C-4（visibility 破壊）と C-6（RT 枚数の不整合）は、**踏むと原因究明に丸一日かかる種類**のバグ。しかも「規約をコメントで書いてある」だけで機構的な保護がない
- 「無音で壊れる」経路が多い（D-6 の頂点レイアウト、D-9 の同名衝突、M-8 の CBV 名タイポ）。この基盤は**自動化している以上、検証で守るのが本体**なので、ここが弱いのは設計として片手落ち

### 処理コスト — ✕（大きな取りこぼしあり）

C-1（`-Od` 固定）は**このレビューで見つかった中で最も費用対効果が高い**。他に、SRV ごとの独立テーブル（D-8）、使わない PSO を 5 個作る（C-7）、DENY フラグ未使用（D-8）が積み上がっている。

---

## 5. 推奨する着手順

| 優先 | 項目 | 見積 | 効果 |
|---|---|---|---|
| 1 | **C-1** `-Od` → 構成別（Release は `-O3`） | 10 分 | 全描画のフレームタイム。まずこれ |
| 2 | **C-2** `assert` → ログ + 早期 return、`CreatePipelineStateDesc` の null ガード | 30 分 | Release クラッシュの除去 |
| 3 | **C-3** `CompileShader` の戻り値を `ComPtr` 化 | 1 時間 | リーク解消。呼び出し側の差分は小さい |
| 4 | **C-7** 描画時に blendMode から PSO を引く + 必要モードのみビルド | 1 時間 | 機能バグ修正 + PSO 5/6 削減 |
| 5 | **C-4 / C-5** visibility 畳み込み・`IndependentBlendEnable` 明示 | 1 時間 | 潜在バグの除去 |
| 6 | **C-6** RT レイアウトを `ForwardPipelineDesc` に載せ、CVar を再起動時反映に | 2 時間 | D-1 の先取りにもなる |
| 7 | **D-1 / D-2** `ForwardPipelineDesc` + `OnConfigureRootSignature` フック | 半日 | 拡張性の壁を 2 つ同時に崩す |
| 8 | **D-4** `(path, defines, descHash)` キーの PSO / RS / Blob キャッシュ | 半日 | 起動時間の線形増加を止める |
| 9 | **D-6** 頂点レイアウトを C++ 側の単一ソースへ + ストライド検証 | 半日 | 「無音で壊れる」経路を 1 本潰す |
| 10 | **D-8** DENY フラグ自動導出・連番 SRV のテーブル統合・64 DWORD 検査 | 半日 | 描画コスト + 失敗時の可読性 |

1〜5 は独立して適用でき、合計 3〜4 時間で Critical をほぼ潰せる。6 以降は `ForwardPipelineDesc` の導入を軸に一括で進めるのが効率的。
