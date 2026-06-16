# パーティクルシステム

`ParticleSystem` はモジュール式のパーティクルエフェクトを提供する `GameObject` 派生クラスです。

## ヘッダ

```cpp
#include "Particle/ParticleSystem.h"
```

## 概要

- `GameObject` を継承しているため、シーンの `CreateObject` で生成・管理できます
- 各種モジュール（Emission, Shape, Velocity, Color, Size, Rotation, Force, Noise）で挙動を制御します
- ビルボードパーティクルとモデルパーティクルの 2 種類の描画モードがあります
- プリセット管理でパラメータの保存・読み込みが可能です

## 基本的な使い方

```cpp
void ParticleScene::OnInitialize() {
    SetSceneName("ParticleScene");

    auto dxCommon = engine_->GetComponent<CoreEngine::DirectXCommon>();
    auto factory  = engine_->GetComponent<CoreEngine::ResourceFactory>();

    // パーティクルシステムの生成
    auto particle = CreateObject<CoreEngine::ParticleSystem>();
    particle->Initialize(dxCommon, factory, "FireEffect");
    particle->SetActive(true);
}
```

## モジュール一覧

| モジュール | 説明 |
|-----------|------|
| `MainModule` | 最大数、持続時間、開始サイズ・色・速度などの基本設定 |
| `EmissionModule` | 放出レート、バースト設定 |
| `ShapeModule` | 放出形状（球、コーン、ボックスなど） |
| `VelocityModule` | 速度の制御 |
| `ColorModule` | 色の変化（グラデーション） |
| `SizeModule` | サイズの変化 |
| `RotationModule` | 回転の制御 |
| `ForceModule` | 外力（重力など） |
| `NoiseModule` | ノイズによる揺らぎ |

## 注意事項

- パーティクルの最大数はGPUメモリ確保時の上限値 `kNumMaxInstance = 1028` です
- 実際の最大パーティクル数は `MainModule.maxParticles` で制御します
- デバッグビルドでは ImGui からリアルタイムにパラメータを調整できます
