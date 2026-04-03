# EngineSystem

`EngineSystem` はエンジンの中核システム管理クラスです。  
各種コンポーネント（グラフィックス、入力、オーディオ等）へのアクセスを提供します。

## ヘッダ

```cpp
#include "EngineSystem/EngineSystem.h"
```

## 概要

- DirectX12、入力、オーディオなどエンジンの各サブシステムを一括管理します
- `GetComponent<T>()` テンプレートメソッドで型安全にコンポーネントを取得できます
- `Framework::GetEngineSystem()` またはシーン内の `engine_` メンバーからアクセスします

## コンポーネント取得

```cpp
// Framework 派生クラス内
auto engine = GetEngineSystem();

// BaseScene 派生クラス内
auto engine = engine_;

// 型安全なコンポーネント取得
auto* keyboard   = engine->GetComponent<CoreEngine::KeyboardInput>();
auto* mouse      = engine->GetComponent<CoreEngine::MouseInput>();
auto* modelMgr   = engine->GetComponent<CoreEngine::ModelManager>();
auto* iblSystem  = engine->GetComponent<CoreEngine::IBLSystem>();
auto* soundMgr   = engine->GetComponent<CoreEngine::SoundManager>();
```

## 取得可能な主要コンポーネント

| コンポーネント型 | 説明 |
|----------------|------|
| `DirectXCommon` | DirectX12 の基本機能 |
| `TextureManager` | テクスチャ管理（※シングルトン経由も可） |
| `ModelManager` | 3D モデル管理 |
| `KeyboardInput` | キーボード入力 |
| `MouseInput` | マウス入力 |
| `GamePadInput` | ゲームパッド入力 |
| `SoundManager` | サウンド管理 |
| `LightManager` | ライト管理 |
| `RenderManager` | レンダリング管理 |
| `IBLSystem` | IBL（環境マップ）システム |

## コンポーネント存在確認

```cpp
if (engine->HasComponent<CoreEngine::ModelManager>()) {
    auto* modelMgr = engine->GetComponent<CoreEngine::ModelManager>();
    // ...
}
```

## デバッグ機能（`_DEBUG` ビルドのみ）

```cpp
#ifdef _DEBUG
// ImGui マネージャー
auto* imGui = engine->GetImGuiManager();

// コンソール UI
auto* console = engine->GetConsole();
console->LogInfo("メッセージ");

// ゲームデバッグ UI
auto* debugUI = engine->GetGameDebugUI();
#endif
```

## SceneManager 連携

```cpp
// SceneManager の設定
engine->SetSceneManager(sceneManager_.get());

// SceneManager の取得
auto* sceneMgr = engine->GetSceneManager();
```
