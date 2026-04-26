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
auto* inputMgr   = engine->GetComponent<CoreEngine::InputManager>();
auto* modelMgr   = engine->GetComponent<CoreEngine::ModelManager>();
auto* soundMgr   = engine->GetComponent<CoreEngine::SoundManager>();
auto* lightMgr   = engine->GetComponent<CoreEngine::LightManager>();
```

## 取得可能な主要コンポーネント

| コンポーネント型 | 説明 |
|----------------|------|
| `DirectXCommon` | DirectX12 の基本機能 |
| `TextureManager` | テクスチャ管理（※シングルトン経由も可） |
| `ModelManager` | 3D モデル管理 |
| `InputManager` | 入力管理（`InputQuery` 経由でキーボード・マウス・ゲームパッドにアクセス） |
| `SoundManager` | サウンド管理 |
| `LightManager` | ライト管理 |
| `RenderManager` | レンダリング管理 |
| `PostEffectManager` | ポストエフェクト管理 |
| `FrameRateController` | フレームレート制御 |

## サブシステム取得

エンジン内部のサブシステムは `GetSubsystem<T>()` で取得できます。

```cpp
#ifdef USE_IMGUI
// DebugSubsystem（デバッグ機能）
auto* debugSys = engine->GetSubsystem<CoreEngine::DebugSubsystem>();
// GetDebugSubsystem() は GetSubsystem<DebugSubsystem>() のショートカット
auto* debugSys = engine->GetDebugSubsystem();
#endif
```

## コンポーネント存在確認

```cpp
if (engine->HasComponent<CoreEngine::ModelManager>()) {
    auto* modelMgr = engine->GetComponent<CoreEngine::ModelManager>();
    // ...
}
```

## デバッグ機能（`USE_IMGUI` ビルドのみ）

```cpp
#ifdef USE_IMGUI
// DebugSubsystem 経由でデバッグ機能にアクセス
auto* debugSys = engine->GetDebugSubsystem();

// ImGui マネージャー
auto* imGui = debugSys->GetImGuiManager();

// コンソール UI
auto* console = debugSys->GetConsole();
console->LogInfo("メッセージ");

// ゲームデバッグ UI
auto* debugUI = debugSys->GetGameDebugUI();

// ドッキング UI
auto* dockingUI = debugSys->GetDockingUI();
#endif
```

## SceneManager 連携

```cpp
// SceneManager の設定
engine->SetSceneManager(sceneManager_.get());

// SceneManager の取得
auto* sceneMgr = engine->GetSceneManager();
```
