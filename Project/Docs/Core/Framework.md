# Framework

`Framework` はエンジンのゲームループを提供する基底クラスです。  
ゲーム固有のアプリケーションクラスはこのクラスを継承して実装します。

## ヘッダ

```cpp
#include "Framework/Framework.h"
```

## 概要

- エンジンの初期化・メインループ・終了処理を自動で管理します
- 派生クラスは `Initialize()` / `Update()` / `Draw()` / `Finalize()` をオーバーライドして、ゲーム固有のロジックを実装します
- `Run()` を呼ぶだけでゲームループが開始されます

## 使い方

### 基本的なゲームクラス

```cpp
#include "Framework/Framework.h"
#include "Scene/SceneManager.h"

class MyGame : public CoreEngine::Framework {
public:
    ~MyGame() override = default;

protected:
    void Initialize() override {
        // シーンマネージャーの初期化
        sceneManager_ = std::make_unique<CoreEngine::SceneManager>();
        sceneManager_->Initialize(GetEngineSystem());
        GetEngineSystem()->SetSceneManager(sceneManager_.get());

        // シーンの登録
        sceneManager_->RegisterScene<TitleScene>("TitleScene");
        sceneManager_->RegisterScene<GameScene>("GameScene");

        // 初期シーンを設定
        sceneManager_->SetInitialScene("TitleScene");
    }

    void Finalize() override {
        if (sceneManager_) {
            GetEngineSystem()->SetSceneManager(nullptr);
            sceneManager_->Finalize();
            sceneManager_.reset();
        }
    }

    void Update() override {
        if (sceneManager_) {
            sceneManager_->Update();
        }
    }

    void Draw() override {
        if (sceneManager_) {
            sceneManager_->Draw();
        }
    }

    void PrepareRender() override {
        if (sceneManager_) {
            sceneManager_->PrepareRender();
        }
    }

private:
    std::unique_ptr<CoreEngine::SceneManager> sceneManager_;
};
```

### エントリポイント

```cpp
#include "MyGame.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    MyGame game;
    game.Run();
    return 0;
}
```

## オーバーライド可能なメソッド

| メソッド | 説明 |
|---------|------|
| `Initialize()` | ゲーム固有の初期化処理（純粋仮想関数） |
| `Finalize()` | ゲーム固有の終了処理（純粋仮想関数） |
| `Update()` | ゲーム固有の更新処理（純粋仮想関数） |
| `Draw()` | ゲーム固有の描画処理（純粋仮想関数） |
| `PrepareRender()` | 描画前準備（任意オーバーライド） |

## ヘルパーメソッド

| メソッド | 説明 |
|---------|------|
| `GetEngineSystem()` | エンジンシステムへのポインタを取得 |

## 注意事項

- `Run()` はブロッキング関数です。ゲームループが終了するまで戻りません
- `GetEngineSystem()` は `Initialize()` 以降で使用可能です
