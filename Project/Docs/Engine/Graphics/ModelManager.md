# モデル管理 (ModelManager)

`ModelManager` は 3D モデルの作成・キャッシュを管理するクラスです。

## ヘッダ

```cpp
#include "Graphics/Model/ModelManager.h"
```

## 概要

- OBJ / glTF 形式の 3D モデルに対応しています
- 静的モデル、キーフレームアニメーションモデル、スケルトンアニメーションモデルの 3 種類を作成できます
- `ModelGameObject` を使用する場合は自動的にモデルが読み込まれるため、直接操作する必要はほとんどありません
- ファイル名のみ指定すれば AssetDatabase がパスを自動解決します

## 基本的な使い方

通常は `ModelGameObject` を継承するだけでモデルは自動管理されます。  
直接 `ModelManager` を使う場合は以下のようにします:

```cpp
auto modelManager = engine_->GetComponent<CoreEngine::ModelManager>();

// 静的モデルの作成
auto staticModel = modelManager->CreateStaticModel("character.gltf");

// スケルトンアニメーションモデルの作成（AnimationPlayer 付き）
auto skeletonModel = modelManager->CreateSkeletonModel(
    "character.gltf",
    "walk",   // アニメーション名（空 = 最初のアニメーション）
    true      // ループ再生
);

// アニメーションの切り替え・ブレンドは AnimationPlayer 経由で行う
if (auto* player = skeletonModel->GetAnimationPlayer()) {
    player->SwitchWithBlend("run", 0.3f, true);
}
```

## アニメーションの追加読み込み

```cpp
CoreEngine::AnimationLoadInfo loadInfo;
loadInfo.modelFile = "character.gltf";           // モデルファイル
loadInfo.animationName = "runAnimation";         // 識別名
loadInfo.animationFile = "character_run.gltf";   // アニメーションファイル（空 = modelFile と同じ）

bool success = modelManager->LoadAnimation(loadInfo);
```

## 主要メソッド

| メソッド | 説明 |
|---------|------|
| `CreateStaticModel(filePath)` | 静的モデルを作成（アニメーションなし） |
| `CreateSkeletonModel(filePath, animName, loop)` | スケルトンアニメーションモデルを作成（AnimationPlayer 付き） |
| `LoadAnimation(loadInfo)` | アニメーションを追加読み込み |
| `ClearCache()` | 全キャッシュをクリア |
| `IsInitialized()` | 初期化済みか確認 |

## ModelGameObject との関係

`ModelGameObject` を継承したクラスでは、`GetModelPath()` でファイル名を返すだけでモデルが自動的に読み込まれます:

```cpp
class EnemyObject : public CoreEngine::ModelGameObject {
protected:
    std::string GetModelPath() const override { return "enemy.gltf"; }
public:
    const char* GetObjectName() const override { return "Enemy"; }
};
```

これにより内部で `ModelManager::CreateStaticModel()` が呼ばれ、モデルが自動生成されます。
