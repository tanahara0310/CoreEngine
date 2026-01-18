# GameTimer クラス 使用方法

`GameTimer` は、一定時間で完了する処理やアニメーション、演出などを管理するための汎用タイマーです。  
以下では **初めて使う人向けに最低限必要な使い方だけ** をまとめています。

---

## 1. 準備

### インクルード
```cpp
#include "Engine/Utility/Timer/GameTimer.h"
```

### インスタンス作成
```cpp
GameTimer timer;
```

---

## 2. タイマーの開始

### 秒指定で開始
```cpp
timer.Start(3.0f, false); // 3秒、ループなし
```

### フレーム数で開始
```cpp
timer.StartFrames(60, false, 60.0f); // 60フレーム、ループなし、FPS 60基準
```

---

## 3. 毎フレーム更新

必ず `Update(deltaTime)` を毎フレーム呼びます。

```cpp
timer.Update(deltaTime);
```

`deltaTime` はゲームループで計算された1フレームの経過時間(秒)です。

---

## 4. タイマーの状態取得

```cpp
timer.IsActive();        // 動作中か
timer.IsFinished();      // 終了したか
timer.GetElapsedTime();  // 経過時間(秒)
timer.GetRemainingTime();// 残り時間(秒)
timer.GetDuration();     // 総時間(秒)
timer.GetProgress();     // 進行度(0.0～1.0)
```

---

## 5. イージングを適用した進行度

```cpp
float t = timer.GetEasedProgress(EasingUtil::Type::EaseOutCubic);
```

---

## 6. 一時停止・再開・停止・リセット

```cpp
timer.Pause();
timer.Resume();
timer.Stop();
timer.Reset();
```

---

## 7. タイムスケール（スロー/加速）

```cpp
timer.SetTimeScale(0.5f); // 半分の速さ（スロー）
timer.SetTimeScale(2.0f); // 2倍速
```

---

## 8. コールバック（時間または進行度で一度だけ実行）

### 時間によるコールバック
```cpp
timer.AddCallback(1.5f, []() {
    // 1.5秒経過時に1回だけ実行される
});
```

### 進行度によるコールバック
```cpp
timer.AddCallbackAtProgress(0.5f, []() {
    // 進行度 50% の瞬間に1回実行
});
```

---

## 9. ループタイマーとループ検知

### ループ開始
```cpp
timer.Start(1.0f, true); // 1秒ループ
```

### このフレームでループしたか確認
```cpp
if (timer.HasLooped()) {
    // ループ境界に到達したフレームでのみ true
}
```

---

## 10. ImGui でのデバッグ表示（Debug ビルド）

```cpp
timer.DrawImGui("MyTimer");
```

タイマーの状態やボタン操作ができ、進行状況を可視化できます。

---

## まとめ
- `Start()`→`Update()`→`GetProgress()` が基本運用
- イージング・スロー・コールバック・ループなど幅広く利用可能
- Debug ビルドでは `DrawImGui()` による可視化も対応

---

## 11. インターバル機能（NEW）

タイマー内で一定間隔ごとに処理を実行できます。別途間隔変数を用意する必要がありません。

### 繰り返しコールバック（自動実行）
```cpp
// 0.5秒ごとに弾を発射
timer.Start(10.0f, false); // 10秒タイマー
timer.AddRepeatingCallback(0.5f, []() {
    SpawnBullet();
});
```

### フレーム間隔での繰り返しコールバック
```cpp
// 30フレームごとに敵を生成
timer.StartFrames(600, false, 60.0f); // 600フレーム = 10秒
timer.AddRepeatingCallbackFrames(30, []() {
    SpawnEnemy();
}, 60.0f);
```

### 手動で間隔をチェック
```cpp
// 1秒ごとにスコアを更新（手動チェック）
if (timer.CheckInterval(1.0f)) {
    UpdateScore();
}
```

### フレーム間隔のチェック
```cpp
// 60フレームごとに処理
if (timer.CheckIntervalFrames(60, 60.0f)) {
    // 1秒ごとの処理
}
```

### 間隔の経過回数を取得
```cpp
int count = timer.GetIntervalCount(1.0f); // 1秒間隔が何回経過したか
```

### 間隔チェッカーをリセット
```cpp
timer.ResetIntervalCheckers(); // すべての間隔カウンターをリセット
```

---

## 12. 完了時コールバック（NEW）

タイマー完了時やループ完了時に自動で処理を実行できます。

### タイマー完了時のコールバック
```cpp
timer.Start(5.0f, false); // 5秒タイマー
timer.SetOnComplete([]() {
    ShowGameOver(); // 5秒後に1度だけ実行
});
```

### ループ完了時のコールバック（1周ごと）
```cpp
timer.Start(2.0f, true); // 2秒ループタイマー
timer.SetOnLoopComplete([]() {
    // 2秒ごとに実行（ループのたびに実行）
    PlayLoopSound();
});
```

### ワンショットディレイ（遅延実行）
```cpp
// 3秒後に1度だけ爆発
timer.Start(10.0f, false);
timer.SetDelay(3.0f, []() {
    Explode();
});
```

---

## 13. クリア機能

### 繰り返しコールバックをクリア
```cpp
timer.ClearRepeatingCallbacks();
```

### 通常のコールバックをクリア
```cpp
timer.ClearCallbacks();
```

---

## 実用例

### 例1: 敵の攻撃パターン
```cpp
// 10秒間、0.8秒ごとに弾を発射
attackTimer.Start(10.0f, false);
attackTimer.AddRepeatingCallback(0.8f, []() {
    FireBullet();
});
attackTimer.SetOnComplete([]() {
    ChangeAttackPattern(); // 10秒後にパターン変更
});
```

### 例2: スコア更新とエフェクト
```cpp
// 60秒タイマーで1秒ごとにスコア加算
gameTimer.Start(60.0f, false);
gameTimer.Update(deltaTime);

if (gameTimer.CheckInterval(1.0f)) {
    score += 10; // 1秒ごとにスコア加算
}

// 30秒経過時にボーナスエフェクト
gameTimer.AddCallback(30.0f, []() {
    ShowBonusEffect();
});

// 60秒終了時にゲーム終了
gameTimer.SetOnComplete([]() {
    EndGame();
});
```

### 例3: ウェーブシステム
```cpp
// 3秒ごとに敵の波を生成（ループ）
waveTimer.Start(3.0f, true);
waveTimer.SetOnLoopComplete([&waveCount]() {
    waveCount++;
    SpawnEnemyWave(waveCount);
});
```

### 例4: 複数間隔の管理
```cpp
// 1つのタイマーで複数の間隔を管理
timer.Start(100.0f, true);

// 0.5秒ごとに弾を発射
timer.AddRepeatingCallback(0.5f, []() {
    FireBullet();
});

// 2秒ごとに敵を生成
timer.AddRepeatingCallback(2.0f, []() {
    SpawnEnemy();
});

// 5秒ごとにパワーアップ出現
timer.AddRepeatingCallback(5.0f, []() {
    SpawnPowerUp();
});
```

---

## 新機能のポイント

1. **インターバル機能** - タイマークラスだけで間隔処理が完結
2. **繰り返しコールバック** - 自動的に一定間隔で処理を実行
3. **完了時コールバック** - タイマー終了時の処理を簡潔に記述
4. **手動間隔チェック** - `CheckInterval()`で柔軟な間隔制御
5. **間隔カウント** - 間隔が何回経過したかを取得可能

これらの機能により、別途間隔変数を用意する必要がなくなり、タイマークラスだけでゲームに必要なタイマー機能を全て補えます。

