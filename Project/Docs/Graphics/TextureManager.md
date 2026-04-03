# テクスチャ管理 (TextureManager)

`TextureManager` はテクスチャの読み込み・キャッシュを管理するシングルトンクラスです。

## ヘッダ

```cpp
#include "Graphics/Texture/TextureManager.h"
```

## 概要

- ファイル名のみ指定すれば AssetDatabase がパスを自動解決します
- 一度読み込んだテクスチャは自動的にキャッシュされます
- PNG / JPG / DDS / HDR 形式に対応しています
- HDR ファイルは自動的にキューブマップ DDS に変換されます

## 基本的な使い方

```cpp
// シングルトンインスタンスの取得
auto& texMgr = CoreEngine::TextureManager::GetInstance();

// テクスチャの読み込み
auto texture = texMgr.Load("player.png");

// 読み込み結果の利用
if (texture.texture) {
    // texture.texture  : ComPtr<ID3D12Resource>
    // texture.gpuHandle: D3D12_GPU_DESCRIPTOR_HANDLE
}
```

## LoadedTexture 構造体

`Load()` が返す構造体には以下のフィールドがあります:

| フィールド | 型 | 説明 |
|-----------|-----|------|
| `texture` | `ComPtr<ID3D12Resource>` | テクスチャリソース |
| `gpuHandle` | `D3D12_GPU_DESCRIPTOR_HANDLE` | GPU デスクリプタハンドル |

## 使用例

### モデルオブジェクトのテクスチャ

`ModelGameObject` の `GetTexturePath()` で返すだけで自動読み込みされます:

```cpp
class FenceObject : public CoreEngine::ModelGameObject {
protected:
    std::string GetTexturePath() const override { return "fence.png"; }
};
```

### 環境マップ（HDR）の読み込み

```cpp
auto& texMgr = TextureManager::GetInstance();

// HDR ファイルはキューブマップ DDS に自動変換
auto envMap = texMgr.Load("sky_environment.hdr");

// IBL システムに渡す
auto ibl = engine_->GetComponent<IBLSystem>();
IBLSystem::SetupParams params;
params.environmentMap = envMap.texture.Get();
params.environmentMapSRV = envMap.gpuHandle;
params.environmentKey = "sky_environment.hdr";
ibl->Setup(params);
```

### メタデータの取得

```cpp
auto metadata = texMgr.GetMetadata("player.png");
float width  = static_cast<float>(metadata.width);
float height = static_cast<float>(metadata.height);
float aspect = width / height;
```

## 主要メソッド

| メソッド | 説明 |
|---------|------|
| `GetInstance()` | シングルトンインスタンスの取得 |
| `Load(filePath)` | テクスチャの読み込み（キャッシュ対応） |
| `GetMetadata(filePath)` | テクスチャのメタデータ（幅・高さ等）を取得 |
| `Clear()` | 全テクスチャキャッシュをクリア |
| `GetTextureCache()` | キャッシュのスナップショットを取得 |
| `SetDDSCacheEnabled(enable)` | DDS 自動生成の有効/無効 |
| `IsDDSCacheEnabled()` | DDS 自動生成が有効か確認 |

## 注意事項

- ファイル名のみ指定した場合、`Application/Assets/` と `Engine/Assets/` 以下を自動検索します
- 相対パス（例: `Application/Assets/Textures/player.png`）での指定も可能です
- HDR → キューブマップ DDS 変換はキャッシュされるため、2 回目以降は高速です
