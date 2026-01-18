# パーティクルシステム - 使用方法ガイド

## 概要

このパーティクルシステムは、Unityライクなモジュラー設計を採用した C++ ベースのリアルタイムパーティクルシステムです。  
様々な視覚効果を簡単に作成できます。

## フォルダ構成

```
Engine/Particle/
├── ParticleSystem.h         # メインのパーティクルシステムクラス
├── ParticleSystem.cpp       # 実装ファイル
├── Modules/                 # モジュールシステム
│   ├── ParticleModule.h     # 基底モジュールクラス
│   ├── EmissionModule.h/cpp # パーティクル生成モジュール
│   ├── VelocityModule.h/cpp # 速度制御モジュール
│   ├── ColorModule.h/cpp    # 色変化モジュール
│   ├── LifetimeModule.h/cpp # 寿命管理モジュール
│   ├── ForceModule.h/cpp    # 物理力モジュール
│   ├── SizeModule.h/cpp     # サイズ変化モジュール
│   └── RotationModule.h/cpp # 回転制御モジュール
└── README.md               # このファイル
```

## 基本的な使用方法

### 1. パーティクルサブシステムの初期化

```cpp
#include "Engine/EngineSystem/SubSystem/Particle/ParticleSubSystem.h"

// パーティクルサブシステムのインスタンス作成
std::unique_ptr<ParticleSubSystem> particle_ = std::make_unique<ParticleSubSystem>();

// 初期化
particle_->Initialize();
particle_->SetInitializeParams(
    graphics->GetDirectXCommon(),
    graphics->GetResourceFactory()
);
```

### 2. 基本的なエフェクトの作成

```cpp
// パーティクルシステムの取得
auto* particleSystem = particle_->GetParticleSystem();

// エミッター位置の設定
particleSystem->SetEmitterPosition({0.0f, 5.0f, 0.0f});

// ビルボード設定
particleSystem->SetBillboardType(BillboardType::ViewFacing);

// ブレンドモード設定
particleSystem->SetBlendMode(BlendMode::kBlendModeAdd);

// エフェクト開始
particleSystem->Play();
```

### 3. 更新と描画

```cpp
// Update関数内
if (particle_) {
    particle_->Update(camera_->GetViewMatrix(), camera_->GetProjectionMatrix());
}

// Draw関数内
if (particle_) {
    particle_->Draw(
        graphics->GetDirectXCommon()->GetCommandList(),
        textureHandle.gpuHandle
    );
}
```

## モジュール設定ガイド

### EmissionModule（放出モジュール）
- パーティクルを発生させるモジュール
- 放出方式:
  - `rateOverTime`: 1秒あたりの放出数
  - `burstCount` + `burstTime`: 指定時刻にまとめて放出
- 放出形状: 点、ボックス、球体、円、コーン、半球、リング、ライン、円柱、エッジ
- `loop` が有効な場合は `duration` 毎にループ

### VelocityModule（速度モジュール）
- パーティクルの初期速度を決定
- `startSpeed`: 基本速度ベクトル
- `startSpeedMultiplier`: 全体倍率
- `randomSpeedRange`: ランダム速度の範囲
- `useRandomDirection`: 方向をランダム化するオプション

### ColorModule（色モジュール）
- パーティクルの開始色・終了色を設定
- `useGradient`: ライフタイムに応じて色を線形補間
- `randomColorRange`: 開始色に対するランダム揺らぎ

### LifetimeModule（寿命モジュール）
- パーティクルの寿命（生存時間）を決定
- `startLifetime`: 基本寿命
- `lifetimeRandomness`: ランダム性を加える

### ForceModule（力場モジュール）
- 外力（重力・風・抵抗・加速度フィールド）を適用
- `gravity`: 重力ベクトル
- `wind`: 風の方向と強さ
- `drag`: 抵抗係数（0～1）
- `useAccelerationField`: 領域指定の加速度フィールド適用

### SizeModule（サイズモジュール）
- 開始サイズ・終了サイズを設定し、ライフタイムに応じて補間
- `startSize` / `endSize`: 基本サイズ
- `use3DSize`: 軸ごとにサイズ設定
- `sizeOverLifetime`: 寿命での変化有効化
- 補間カーブ: 線形 / EaseIn / EaseOut / EaseInOut / 一定

### RotationModule（回転モジュール）
- 2D（Z軸のみ）または3D回転を適用
- `startRotation`: 初期回転角
- `rotationSpeed`: 回転速度
- `rotationOverLifetime`: 寿命に応じて速度を補間
- `alignToVelocity`: 進行方向に回転を合わせる

## ビルボードタイプ

- **ViewFacing**: パーティクルが常にカメラに向く
- **YAxisOnly**: Y軸（上方向）を固定してカメラに向く
- **ScreenAligned**: スクリーン平面と平行
- **None**: ビルボード無効

## 統計情報とデバッグ

パーティクルシステムは統計情報を自動収集し、ImGuiでリアルタイム表示します：

- 作成/破棄されたパーティクル数
- 最大同時パーティクル数
- システム稼働時間
- パフォーマンス警告（使用率80%以上で警告表示）

## 使用例

### 基本的なエフェクト

```cpp
void CreateBasicEffect() {
    auto* ps = particle_->GetParticleSystem();
    
    // エミッションの設定
    auto& emissionModule = ps->GetEmissionModule();
    auto emissionData = emissionModule.GetEmissionData();
    emissionData.rateOverTime = 50;
    emissionData.shapeType = EmissionModule::ShapeType::Sphere;
    emissionData.radius = 1.0f;
    emissionModule.SetEmissionData(emissionData);
    
    // 色の設定
    auto& colorModule = ps->GetColorModule();
    auto colorData = colorModule.GetColorData();
    colorData.startColor = {1.0f, 0.5f, 0.2f, 1.0f}; // オレンジ
    colorData.endColor = {1.0f, 0.1f, 0.1f, 0.0f};   // 赤→透明
    colorData.useGradient = true;
    colorModule.SetColorData(colorData);
    
    ps->Play();
}
```

## 注意点とベストプラクティス

1. **初期化順序**: ParticleSubSystemを先に初期化してからSetInitializeParamsを呼ぶ
2. **リソース管理**: パーティクル数は最大4096個まで（kNumMaxInstance）
3. **パフォーマンス**: デバッグモードでは統計情報でパフォーマンスを監視
4. **モジュール設定**: 各モジュールは独立しているため、必要に応じて有効/無効を切り替え可能

## 関連ファイル

- **エンジンサブシステム**: `Engine/EngineSystem/SubSystem/Particle/ParticleSubSystem.h`  
- **使用例**: `Engine/Scene/TestScene/TestScene.cpp`

---

**作成者**: タナハラ  
**更新日**: 2025年  
**バージョン**: 2.0 (簡素化版)
