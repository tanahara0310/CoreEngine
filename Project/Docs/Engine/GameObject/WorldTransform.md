# WorldTransform

`WorldTransform` は 3D オブジェクトの位置・回転・スケールを管理し、GPU 用の行列を生成するクラスです。

## ヘッダ

```cpp
#include "WorldTransform/WorldTransform.h"
```

## 概要

- `ModelGameObject` の `transform_` メンバーとして自動管理されます
- `translate` / `rotate` / `scale` を直接編集できます
- 毎フレーム `TransferMatrix()` が自動的に呼ばれ、ワールド行列が GPU に転送されます
- オイラー角とクォータニオンの 2 つの回転モードをサポートします
- 親子関係（階層構造）をサポートします

## 基本的な使い方

```cpp
auto obj = CreateObject<ModelObject>("cube.obj");

// 位置を設定
obj->GetTransform().translate = { 10.0f, 5.0f, 0.0f };

// 回転を設定（ラジアン）
obj->GetTransform().rotate = { 0.0f, 1.57f, 0.0f };  // Y軸90度

// スケールを設定
obj->GetTransform().scale = { 2.0f, 1.0f, 2.0f };
```

## 公開メンバー変数

| メンバー | 型 | デフォルト値 | 説明 |
|---------|-----|------------|------|
| `scale` | `Vector3` | `{1, 1, 1}` | スケール |
| `rotate` | `Vector3` | `{0, 0, 0}` | 回転角（ラジアン）- オイラー角モード用 |
| `translate` | `Vector3` | `{0, 0, 0}` | 位置 |
| `quaternionRotate` | `Quaternion` | `{0, 0, 0, 1}` | クォータニオン回転 |

## 回転モード

```cpp
auto& transform = obj->GetTransform();

// オイラー角モード（デフォルト）
transform.SetRotationMode(WorldTransform::RotationMode::Euler);
transform.rotate = { 0.0f, 3.14f, 0.0f };

// クォータニオンモード
transform.SetRotationMode(WorldTransform::RotationMode::Quaternion);
transform.quaternionRotate = { 0.0f, 0.707f, 0.0f, 0.707f };

// オイラー角からクォータニオンに変換
transform.EulerToQuaternion();
```

## 親子関係

```cpp
auto parent = CreateObject<ModelObject>("body.obj");
auto child  = CreateObject<ModelObject>("arm.obj");

// 親を設定
child->GetTransform().SetParent(&parent->GetTransform());

// 親の解除
child->GetTransform().SetParent(nullptr);

// 親の取得
const WorldTransform* parentTransform = child->GetTransform().GetParent();
```

## ワールド座標の取得

```cpp
// ワールド空間での位置を取得
Vector3 worldPos = obj->GetTransform().GetWorldPosition();

// 計算済みワールド行列を取得
const Matrix4x4& worldMatrix = obj->GetTransform().GetWorldMatrix();
```

## 主要メソッド

| メソッド | 説明 |
|---------|------|
| `Initialize(device)` | GPU バッファの初期化（自動呼び出し） |
| `TransferMatrix()` | ワールド行列を計算して GPU に転送（自動呼び出し） |
| `GetWorldPosition()` | ワールド空間での位置を取得 |
| `GetWorldMatrix()` | 計算済みワールド行列を取得 |
| `SetParent(parent)` | 親トランスフォームを設定 |
| `GetParent()` | 親トランスフォームを取得 |
| `SetRotationMode(mode)` | 回転モードを設定 |
| `SetWorldMatrix(matrix)` | ワールド行列を直接設定（アニメーション用） |
| `EulerToQuaternion()` | オイラー角からクォータニオンに変換 |
