# RandomGenerator 仕様ガイド

## 概要

`RandomGenerator`
はエンジン全体で統一された乱数生成機能を提供するシングルトンクラスです。

## 特徴

-   シングルトンパターン
-   統一 API
-   自動初期化


## 使用方法

### インクルード

``` cpp
#include "Engine/Utility/Random/RandomGenerator.h"
```

### 基本例

``` cpp
RandomGenerator& random = RandomGenerator::GetInstance();

float v1 = random.GetFloat();               // [0.0f, 1.0f]
float v2 = random.GetFloatSigned();         // [-1.0f, 1.0f]
float v3 = random.GetFloat(5.0f, 10.0f);    // [5.0f, 10.0f]
int i1   = random.GetInt(1, 100);           // [1, 100]
bool b1  = random.GetBool();                // 50%
bool b2  = random.GetBool(0.3f);            // 30%
```

## API リファレンス

### 静的メソッド

  メソッド          説明
  ----------------- --------------------------------
  `GetInstance()`   シングルトンインスタンスを取得

### インスタンスメソッド

  ------------------------------------------------------------------------------------------
  メソッド                            戻り値                  説明
  ----------------------------------- ----------------------- ------------------------------
  `Initialize(uint32_t seed = 0)`     void                    乱数エンジン初期化（seed=0
                                                              の場合は現在時刻を使用）

  `GetInt(int min, int max)`          int                     `[min, max]` の整数乱数

  `GetFloat(float min, float max)`    float                   `[min, max]` の浮動小数乱数

  `GetFloat()`                        float                   `[0.0f, 1.0f]` の浮動小数乱数

  `GetFloatSigned()`                  float                   `[-1.0f, 1.0f]` の浮動小数乱数

  `GetBool(float probability=0.5f)`   bool                    確率 `probability` で true

  `GetEngine()`                       `std::mt19937*`         乱数エンジンへの直接アクセス
  ------------------------------------------------------------------------------------------

## 初期化

通常は自動初期化されますが、テスト用に同じ結果を得たい場合はシードを指定してください：

``` cpp
RandomGenerator::GetInstance().Initialize(12345);
```
