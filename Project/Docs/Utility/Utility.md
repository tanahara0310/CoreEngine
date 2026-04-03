# ユーティリティ

ゲーム開発で便利なユーティリティクラス群です。

---

## GameTimer

シーン遷移やゲーム内演出のタイミング制御に使用するタイマークラスです。

### ヘッダ

```cpp
#include "Utility/Timer/GameTimer.h"
```

### 基本的な使い方

```cpp
CoreEngine::GameTimer timer;

// タイマーの開始（3秒間、ループなし）
timer.Start(3.0f, false);

// 毎フレーム更新
timer.Update(deltaTime);

// 完了チェック
if (timer.IsFinished()) {
    // 3秒経過
}

// 進行度の取得（0.0 ～ 1.0）
float progress = timer.GetProgress();

// 経過時間の取得
float elapsed = timer.GetElapsedTime();
```

### コールバック

```cpp
// 指定時間にコールバックを発火
timer.AddCallback(1.5f, []() {
    // 1.5秒後に実行
});

// 繰り返しコールバック
timer.AddRepeatingCallback(0.5f, []() {
    // 0.5秒ごとに実行
});
```

### 制御

```cpp
timer.Stop();      // 停止
timer.Reset();     // リセット
timer.Pause();     // 一時停止
timer.Resume();    // 再開
```

---

## RandomGenerator

統一された乱数生成を提供するシングルトンクラスです。

### ヘッダ

```cpp
#include "Utility/Random/RandomGenerator.h"
```

### 使い方

```cpp
auto& rng = CoreEngine::RandomGenerator::GetInstance();

// 整数乱数（min ～ max）
int value = rng.GetInt(1, 100);

// 浮動小数点乱数（min ～ max）
float fValue = rng.GetFloat(0.0f, 10.0f);

// 0.0 ～ 1.0 の乱数
float normalized = rng.GetFloat();

// -1.0 ～ 1.0 の乱数
float signed_val = rng.GetFloatSigned();

// 確率判定（50%でtrue）
if (rng.GetBool(0.5f)) {
    // 50%の確率で実行
}
```

---

## Logger

ログ出力を提供するシングルトンクラスです。

### ヘッダ

```cpp
#include "Utility/Logger/Logger.h"
```

### 使い方

```cpp
auto& logger = CoreEngine::Logger::GetInstance();

// フォーマット付きログ出力
logger.Logf(LogLevel::INFO, LogCategory::System, "{}", "初期化完了");
logger.Logf(LogLevel::WARNING, LogCategory::Graphics, "{}", "テクスチャが見つかりません");
logger.Logf(LogLevel::Error, LogCategory::System, "{}", "致命的なエラー");
```

### ログレベル

| レベル | 説明 |
|--------|------|
| `LogLevel::INFO` | 情報メッセージ |
| `LogLevel::WARNING` | 警告メッセージ |
| `LogLevel::Error` | エラーメッセージ |

---

## JsonManager

JSON ファイルの読み書きとデータ変換を提供するシングルトンクラスです。

### ヘッダ

```cpp
#include "Utility/JsonManager/JsonManager.h"
```

### ファイル操作

```cpp
auto& jsonMgr = CoreEngine::JsonManager::GetInstance();

// JSON ファイルの読み込み
json data = jsonMgr.LoadJson("config/settings.json");

// JSON ファイルへの保存
json saveData;
saveData["score"] = 1000;
saveData["name"] = "Player1";
jsonMgr.SaveJson("saves/save01.json", saveData);

// ファイル存在チェック
if (jsonMgr.FileExists("config/settings.json")) {
    // ...
}
```

### 型変換ヘルパー

```cpp
// Vector3 ⇔ JSON
json j = JsonManager::Vector3ToJson(Vector3{1.0f, 2.0f, 3.0f});
Vector3 v = JsonManager::JsonToVector3(j);

// Vector4 ⇔ JSON
json j4 = JsonManager::Vector4ToJson(Vector4{1.0f, 0.0f, 0.0f, 1.0f});
Vector4 v4 = JsonManager::JsonToVector4(j4);
```

---

## LineManager

デバッグ用のライン描画を提供するシングルトンクラスです。

### ヘッダ

```cpp
#include "Graphics/Line/LineManager.h"
```

### 使い方

```cpp
auto& lineMgr = CoreEngine::LineManager::GetInstance();

// ラインの描画
lineMgr.DrawLine(
    Vector3{0.0f, 0.0f, 0.0f},  // 始点
    Vector3{10.0f, 0.0f, 0.0f}, // 終点
    Vector3{1.0f, 0.0f, 0.0f},  // 色（赤）
    1.0f                          // 透明度
);

// グリッドの描画
lineMgr.DrawGrid(100.0f, 20);

// ワイヤーフレームボックス
lineMgr.DrawWireBox(
    Vector3{0.0f, 1.0f, 0.0f},   // 中心
    Vector3{1.0f, 1.0f, 1.0f}    // サイズ
);

// 座標軸の描画
lineMgr.DrawAxis(Vector3{0.0f, 0.0f, 0.0f}, 5.0f);

// 円の描画（XZ平面）
lineMgr.DrawCircle(
    Vector3{0.0f, 0.0f, 0.0f}, // 中心
    3.0f,                        // 半径
    32                           // セグメント数
);
```
