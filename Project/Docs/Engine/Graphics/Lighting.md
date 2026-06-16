# ライティング

CoreEngine のライティングシステムは複数種類のライトと IBL（Image-Based Lighting）をサポートしています。

## ヘッダ

```cpp
#include "Graphics/Light/LightData.h"
#include "Graphics/IBL/IBLSystem.h"
```

## ライトの種類

### DirectionalLight（平行光源）

シーン全体を均一に照らす太陽光のようなライトです。  
`BaseScene` の `directionalLight_` メンバーから直接操作できます。

```cpp
void GameScene::OnInitialize() {
    // ディレクショナルライトの設定
    directionalLight_->color     = { 1.0f, 1.0f, 1.0f, 1.0f };
    directionalLight_->direction = { 0.0f, -1.0f, 0.5f };
    directionalLight_->intensity = 1.0f;
    directionalLight_->enabled   = true;
}
```

### ライトデータ構造体

| 構造体 | 説明 | 主要フィールド |
|--------|------|---------------|
| `DirectionalLightData` | 平行光源 | color, direction, intensity |
| `PointLightData` | 点光源 | color, position, intensity, radius, decay |
| `SpotLightData` | スポットライト | color, position, direction, intensity, distance, cosAngle |
| `AreaLightData` | エリアライト | color, position, normal, width, height, range |

---

## IBL（Image-Based Lighting）

環境マップを使用したリアルなライティングを提供します。

### IBL のセットアップ

```cpp
void GameScene::OnInitialize() {
    // 環境マップテクスチャの読み込み
    auto& texMgr = TextureManager::GetInstance();
    auto envMap = texMgr.Load("environment.hdr");

    // IBL システムの取得
    auto ibl = engine_->GetComponent<IBLSystem>();

    // セットアップパラメータの設定
    IBLSystem::SetupParams params;
    params.environmentMap    = envMap.texture.Get();
    params.environmentMapSRV = envMap.gpuHandle;
    params.environmentKey    = "environment.hdr";
    params.irradianceSize    = 128;   // イラディアンスマップサイズ
    params.prefilteredSize   = 256;   // プリフィルタードマップサイズ
    params.brdfLUTSize       = 512;   // BRDF LUT サイズ

    if (ibl->Setup(params)) {
        // IBL 初期化成功
    }

    // SkyBox の作成（IBL と連動）
    auto skyBox = CreateObject<SkyBoxObject>();
    skyBox->SetTexture(envMap);
    skyBox->SetActive(true);
}
```

### オブジェクトでの IBL 使用

```cpp
auto sphere = CreateObject<ModelObject>("sphere.obj");
sphere->SetIBLEnabled(true);      // IBL を有効化
sphere->SetIBLIntensity(1.0f);    // IBL 強度
sphere->SetPBRParameters(
    0.8f,   // metallic
    0.2f,   // roughness
    1.0f    // ambient occlusion
);
```

## IBLSystem::SetupParams

| パラメータ | 型 | デフォルト | 説明 |
|-----------|-----|----------|------|
| `environmentMap` | `ID3D12Resource*` | `nullptr` | 環境マップリソース |
| `environmentMapSRV` | `GPU_DESCRIPTOR_HANDLE` | `{}` | 環境マップの SRV |
| `environmentKey` | `string` | `""` | キャッシュキー |
| `irradianceSize` | `uint32_t` | `128` | イラディアンスマップサイズ |
| `prefilteredSize` | `uint32_t` | `256` | プリフィルタードマップサイズ |
| `brdfLUTSize` | `uint32_t` | `512` | BRDF LUT サイズ |
| `forceRegenerate` | `bool` | `false` | キャッシュを無視して再生成 |
