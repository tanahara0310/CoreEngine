# シェーダーリフレクション実装 - 完了レポート

## ?? 概要

DirectX 12のシェーダーリフレクションAPIを使用して、ルートシグネチャとパイプライン設定を自動化する機能を実装しました。

## ? 実装完了したコンポーネント

### 1. コアシステム

- **ShaderReflectionData** (`Engine\Graphics\Shader\ShaderReflectionData.h/cpp`)
  - シェーダーリソースバインディング情報を保持
  - CBV、SRV、UAV、Samplerの情報を管理
  - 入力レイアウトの自動抽出
  - デバッグ用ToString()メソッド

- **ShaderReflectionBuilder** (`Engine\Graphics\Shader\ShaderReflectionBuilder.h/cpp`)
  - `IDxcBlob`からリフレクション実行
  - VS/PSのマージ処理
  - 入力レイアウトのフォーマット自動判定

- **RootSignatureManager** (拡張)
  - `BuildFromReflection()` メソッドを追加
  - リフレクションデータから自動的にルートシグネチャ生成
  - Descriptor Tableの最適化（連続レジスタのマージ）

- **ShaderCompiler** (拡張)
  - `GetDxcUtils()` メソッドを追加
  - リフレクションビルダーとの統合

### 2. レンダラーの移行状況

#### ? LineRendererPipeline（完了）
- **構成**: CBV 1個のみ（最もシンプル）
- **変更内容**:
  - `LineRendererRootParam` 名前空間をコメントアウト
  - `wvpRootParamIndex_` をリフレクションから取得
  - 手動設定コードをコメントで残して参照可能に

#### ? SpriteRenderer（完了）
- **構成**: CBV 2個 + SRV 1個 + Sampler 1個
- **変更内容**:
  - `SpriteRendererRootParam` 名前空間をコメントアウト
  - ルートパラメータインデックスをゲッターメソッドで公開
  - `SpriteObject.cpp` での使用箇所を修正

#### ? ModelRenderer（未着手）
- **構成**: 中程度の複雑さ
- **予定**: Phase 2で実装

#### ? SkinnedModelRenderer（未着手）
- **構成**: 最も複雑（20個のルートパラメータ）
- **予定**: Phase 3で実装

## ?? 主な機能

### 自動化された処理

1. **ルートシグネチャの自動生成**
   - CBV、SRV、UAVのバインドポイントを自動検出
   - 連続するレジスタを自動的にDescriptor Tableにまとめる
   - Static Samplerの自動追加

2. **入力レイアウトの自動抽出**
   - 頂点シェーダーのセマンティクスから自動生成
   - コンポーネント数とタイプから適切なDXGI_FORMATを決定

3. **シェーダー可視性の自動判定**
   - VS/PS別にリソースをリフレクション
   - 適切なD3D12_SHADER_VISIBILITYを設定

### デバッグ機能

- リフレクション結果の詳細ログ出力
- 手動設定との比較が容易（コメントとして保存）

## ?? 使用例

### 従来の手動設定
```cpp
void LineRendererPipeline::Initialize(ID3D12Device* device) {
    // 手動でルートパラメータを設定
    RootSignatureManager::RootDescriptorConfig cbvConfig;
    cbvConfig.shaderRegister = 0;  // b0
    cbvConfig.visibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootSignatureMg_->AddRootCBV(cbvConfig);
    rootSignatureMg_->Create(device);
    
    // 描画時も手動インデックス
    cmdList->SetGraphicsRootConstantBufferView(
        LineRendererRootParam::kWVP,  // 手動定義
        wvpBuffer_->GetGPUVirtualAddress());
}
```

### 新しいリフレクション自動設定（名前ベース）
```cpp
void LineRendererPipeline::Initialize(ID3D12Device* device) {
    // シェーダーコンパイル
    auto vsBlob = shaderCompiler_->CompileShader(L"Line.VS.hlsl", L"vs_6_0");
    auto psBlob = shaderCompiler_->CompileShader(L"Line.PS.hlsl", L"ps_6_0");
    
    // リフレクションビルダーを初期化
    reflectionBuilder_->Initialize(shaderCompiler_->GetDxcUtils());
    
    // 自動抽出 & 自動生成
    reflectionData_ = reflectionBuilder_->BuildFromShaders(vsBlob, psBlob);
    rootSignatureMg_->BuildFromReflection(device, *reflectionData_);
    
    // シェーダーのリソース名から取得（HLSLファイル内の名前と完全一致）
    wvpRootParamIndex_ = reflectionData_->GetRootParameterIndexByName("WVPMatrix");
    assert(wvpRootParamIndex_ >= 0 && "WVPMatrix not found");
    
    // 描画時はリフレクションベースのインデックス
    cmdList->SetGraphicsRootConstantBufferView(
        wvpRootParamIndex_,
        wvpBuffer_->GetGPUVirtualAddress());
}
```

### HLSLシェーダーの例
```hlsl
// Line.VS.hlsl
cbuffer WVPMatrix : register(b0) {  // ← この名前を使用
    float4x4 gWVP;
}

struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
    float alpha : ALPHA;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float alpha : ALPHA;
};

VSOutput main(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), gWVP);
    output.color = input.color;
    output.alpha = input.alpha;
    return output;
}
```

### Sprite用の例
```hlsl
// Sprite.VS.hlsl
cbuffer Transform : register(b1) {  // ← GetRootParameterIndexByName("Transform")
    float4x4 gWVP;
    float4x4 gWorld;
}

// Sprite.PS.hlsl
cbuffer Material : register(b0) {  // ← GetRootParameterIndexByName("Material")
    float4 gColor;
    float4x4 gUVTransform;
}

Texture2D gTexture : register(t0);  // ← GetRootParameterIndexByName("gTexture")
SamplerState gSampler : register(s0);
```

## ? メリット

1. **保守性の向上**
   - シェーダー変更時のC++側の修正が不要
   - リソース名の変更もHLSL側のみで対応可能

2. **ヒューマンエラーの削減**
   - レジスタの重複や抜けを防止
   - 型の不一致を自動検出
   - 名前の不一致は実行時に検出（assert）

3. **開発効率の向上**
   - 新規レンダラーの実装が高速化
   - デバッグ時間の短縮
   - HLSLとC++で同じ名前を使用できる

4. **柔軟性**
   - 名前ベースの取得により、レジスタ番号に依存しない
   - シェーダー側で自由に並び順を変更可能

## ?? 名前ベースの取得について

### リソース名の一致ルール
C++側で使用する名前は、HLSL内の**定数バッファ名**または**リソース名**と**完全に一致**する必要があります：

```hlsl
// ? 正しい例
cbuffer WVPMatrix : register(b0) {
    float4x4 gWVP;
}
// C++: reflectionData_->GetRootParameterIndexByName("WVPMatrix")

// ? 間違い（変数名ではなく、cbuffer名を使用）
// C++: reflectionData_->GetRootParameterIndexByName("gWVP")  // これはNG

// ? テクスチャの例
Texture2D gTexture : register(t0);
// C++: reflectionData_->GetRootParameterIndexByName("gTexture")
```

### エラーハンドリング
リソースが見つからない場合、`GetRootParameterIndexByName()`は`-1`を返します：

```cpp
int index = reflectionData_->GetRootParameterIndexByName("MyResource");
assert(index >= 0 && "MyResource not found in shader");
```

## ?? 制限事項と今後の課題

### 現在の制限
1. **入力レイアウトの自動適用**
   - `PipelineStateManager`がまだ対応していない
   - 現状は手動設定を維持

2. **複雑なDescriptor Table**
   - 不連続なレジスタの最適化は未実装
   - 現状は単純な連続チェックのみ

### 今後の改善予定
1. **Phase 2**: ModelRendererへの適用
2. **Phase 3**: SkinnedModelRendererへの適用
3. **Phase 4**: パーティクル、シャドウマップ等への展開
4. **最適化**: ルートシグネチャのキャッシュ機能

## ?? 検証結果

- ? LineRendererPipeline: 動作確認済み
- ? SpriteRenderer: 動作確認済み
- ? ビルド成功
- ? 既存機能との互換性維持

## ?? 参考資料

- [ID3D12ShaderReflection (Microsoft Docs)](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nn-d3d12shader-id3d12shaderreflection)
- [Root Signatures (Microsoft Docs)](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures)
- [Shader Model 6 (DXC)](https://github.com/microsoft/DirectXShaderCompiler)

## ?? 次のステップ

1. ModelRendererへのシェーダーリフレクション適用
2. 入力レイアウト自動設定のPipelineStateManager統合
3. パフォーマンステストとベンチマーク
4. ドキュメントの充実化

---

**実装日**: 2025/02/16  
**担当**: CoreEngine Development Team  
**ステータス**: Phase 1完了
