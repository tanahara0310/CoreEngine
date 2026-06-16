# 数学ライブラリ (MathCore)

CoreEngine の数学ライブラリは `MathCore` 名前空間で提供されます。  
ベクトル、行列、クォータニオン、イージング関数など、ゲーム開発に必要な数学機能を網羅しています。

## ヘッダ

```cpp
#include "Math/MathCore.h"       // Vector, Matrix, Quaternion, Transform の統合ヘッダ
#include "Math/Easing/EasingUtil.h"  // イージング関数
```

## 型一覧

| 型 | ヘッダ | 説明 |
|----|--------|------|
| `Vector2` | `Math/Vector/Vector2.h` | 2D ベクトル |
| `Vector3` | `Math/Vector/Vector3.h` | 3D ベクトル |
| `Vector4` | `Math/Vector/Vector4.h` | 4D ベクトル（色にも使用） |
| `Matrix4x4` | `Math/Matrix/Matrix4x4.h` | 4x4 行列 |
| `Quaternion` | `Math/Quaternion/Quaternion.h` | クォータニオン |
| `EulerTransform` | `Math/EulerTransform.h` | 位置・回転・スケール |

## ベクトル演算

```cpp
using namespace CoreEngine;

Vector3 a = { 1.0f, 2.0f, 3.0f };
Vector3 b = { 4.0f, 5.0f, 6.0f };

// 基本演算（演算子オーバーロード）
Vector3 sum = a + b;
Vector3 diff = a - b;
Vector3 scaled = a * 2.0f;
Vector3 scaled2 = 2.0f * a;

// MathCore 名前空間のユーティリティ
using namespace MathCore;
float dot    = Vector::Dot(a, b);       // 内積
float len    = Vector::Length(a);        // 長さ
Vector3 norm = Vector::Normalize(a);    // 正規化
Vector3 cross = Vector::Cross(a, b);    // 外積
Vector3 proj = Vector::Project(a, b);   // 投影
```

## 行列演算

```cpp
using namespace CoreEngine::MathCore;

// 単位行列
Matrix4x4 identity = Matrix::Identity();

// 変換行列
Matrix4x4 scale     = Matrix::MakeScale({2.0f, 2.0f, 2.0f});
Matrix4x4 rotateY   = Matrix::MakeRotateY(3.14f);
Matrix4x4 translate  = Matrix::MakeTranslate({10.0f, 0.0f, 5.0f});

// 行列の合成
Matrix4x4 world = Matrix::Multiply(Matrix::Multiply(scale, rotateY), translate);

// 逆行列・転置行列
Matrix4x4 inv = Matrix::Inverse(world);
Matrix4x4 trans = Matrix::Transpose(world);
```

## イージング関数

```cpp
using namespace CoreEngine::EasingUtil;

// 0.0 ～ 1.0 の t を入力し、イージング後の値を取得
float t = 0.5f;

float value = Ease(Type::EaseInOutQuad, t);
// → S字カーブで補間された値が返る
```

### イージングタイプ

| タイプ | 特徴 |
|-------|------|
| `Linear` | 等速 |
| `EaseInQuad` / `EaseOutQuad` / `EaseInOutQuad` | 2乗（基本的な加減速） |
| `EaseInCubic` / `EaseOutCubic` / `EaseInOutCubic` | 3乗（やや強い加減速） |
| `EaseInQuart` / `EaseOutQuart` / `EaseInOutQuart` | 4乗（強い加減速） |
| `EaseInQuint` / `EaseOutQuint` / `EaseInOutQuint` | 5乗（最も強い加減速） |
| `EaseInBack` / `EaseOutBack` / `EaseInOutBack` | 戻りを含む動き |
| `EaseInElastic` / `EaseOutElastic` | バネ的な動き |
| `EaseInBounce` / `EaseOutBounce` | バウンド |

## 色の表現

色は `Vector4` で RGBA を表現します:

```cpp
Vector4 red   = { 1.0f, 0.0f, 0.0f, 1.0f };  // 赤（不透明）
Vector4 green = { 0.0f, 1.0f, 0.0f, 1.0f };  // 緑（不透明）
Vector4 blue  = { 0.0f, 0.0f, 1.0f, 0.5f };  // 青（半透明）
Vector4 white = { 1.0f, 1.0f, 1.0f, 1.0f };  // 白（不透明）
```
