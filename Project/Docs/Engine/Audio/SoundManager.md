# サウンド管理 (SoundManager)

`SoundManager` はサウンドの読み込み・再生・音量制御を管理するクラスです。

## ヘッダ

```cpp
#include "Audio/SoundManager.h"
```

## 概要

- WAV / MP3 形式に対応しています
- `SoundResource` クラスによる RAII 管理で、自動的にリソースが解放されます
- フェードイン・フェードアウト機能を内蔵しています
- XAudio2 と Media Foundation を使用しています

## 基本的な使い方

### SoundManager の取得

```cpp
auto soundManager = engine_->GetComponent<CoreEngine::SoundManager>();
```

### サウンドの読み込みと再生

```cpp
// サウンドリソースの作成（RAII 管理）
auto bgm = soundManager->CreateSoundResource("bgm_title.wav");

// 再生（ループ再生）
bgm->Play(true);

// 再生（1回のみ）
auto se = soundManager->CreateSoundResource("se_click.wav");
se->Play(false);
```

### 音量・ピッチの制御

```cpp
// 音量の設定（0.0f ～ 1.0f）
bgm->SetVolume(0.5f);
float vol = bgm->GetVolume();

// ピッチの設定
bgm->SetPitch(1.2f);  // 1.2倍速
float pitch = bgm->GetPitch();
```

### 再生制御

```cpp
// 停止
bgm->Stop();

// 一時停止
bgm->Pause();

// 再開
bgm->Resume();

// 状態確認
if (bgm->IsPlaying()) { /* 再生中 */ }
if (bgm->IsPaused())  { /* 一時停止中 */ }
```

### フェード

```cpp
// フェードイン（2秒かけて音量 1.0 まで上げる）
bgm->FadeIn(2.0f, 1.0f);

// フェードアウト（1.5秒かけてフェードアウト、完了後に停止）
bgm->FadeOut(1.5f, true);

// フェード更新（毎フレーム呼び出す必要がある）
bgm->UpdateFade(deltaTime);

// フェード中かどうか
if (bgm->IsFading()) { /* ... */ }
```

### マスター音量

```cpp
// マスター音量の設定（全サウンドに影響）
soundManager->SetMasterVolume(0.8f);
float masterVol = soundManager->GetMasterVolume();
```

## SoundResource メソッド一覧

| メソッド | 説明 |
|---------|------|
| `Play(loop)` | 再生開始（loop: ループ再生するか） |
| `Stop()` | 停止 |
| `Pause()` | 一時停止 |
| `Resume()` | 再開 |
| `SetVolume(volume)` | 音量設定（0.0 ～ 1.0） |
| `GetVolume()` | 音量取得 |
| `SetPitch(pitch)` | ピッチ設定 |
| `GetPitch()` | ピッチ取得 |
| `IsPlaying()` | 再生中か |
| `IsPaused()` | 一時停止中か |
| `FadeIn(duration, targetVolume)` | フェードイン開始 |
| `FadeOut(duration, stopAfterFade)` | フェードアウト開始 |
| `UpdateFade(deltaTime)` | フェード更新 |
| `IsFading()` | フェード中か |
| `IsValid()` | 有効なリソースか |

## 使用例：BGM とシーン遷移

```cpp
class GameScene : public CoreEngine::BaseScene {
private:
    std::unique_ptr<CoreEngine::SoundManager::SoundResource> bgm_;

    void OnInitialize() override {
        SetSceneName("GameScene");

        auto soundMgr = engine_->GetComponent<CoreEngine::SoundManager>();
        bgm_ = soundMgr->CreateSoundResource("bgm_game.wav");
        bgm_->SetVolume(0.7f);
        bgm_->Play(true);  // ループ再生
    }

    void OnUpdate() override {
        // フェード更新
        if (bgm_) bgm_->UpdateFade(deltaTime);

        auto keyboard = engine_->GetComponent<CoreEngine::KeyboardInput>();
        if (keyboard->IsKeyTriggered(DIK_ESCAPE)) {
            // シーン遷移前にフェードアウト
            bgm_->FadeOut(1.0f, true);
            sceneManager_->ChangeScene("TitleScene");
        }
    }

    void Finalize() override {
        bgm_.reset();
    }
};
```

## 注意事項

- ファイルパスは `Application/Assets/` を省略できます（自動検索）
- `SoundResource` はスコープを抜けると自動解放されるため、メンバー変数で保持してください
- `UpdateFade()` はフェードを使用する場合、毎フレーム呼び出す必要があります
