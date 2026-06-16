# ModelGameObject

`ModelGameObject` は 3D モデルを持つゲームオブジェクトの中間基底クラスです。  
`GameObject` を継承し、モデルの読み込み・トランスフォーム・描画のボイラープレートを集約します。

## ヘッダ

```cpp
#include "ObjectCommon/Model/ModelGameObject.h"
```

## 概要

- 派生クラスは `GetModelPath()` / `GetTexturePath()` をオーバーライドするだけで 3D モデルを扱えます
- ファイル名のみ指定すれば、AssetDatabase がディレクトリを自動解決します
- `WorldTransform`（位置・回転・スケール）と `Model` を自動管理します

## 使い方

### 最小構成のオブジェクト

```cpp
#include "ObjectCommon/Model/ModelGameObject.h"

class FenceObject : public CoreEngine::ModelGameObject {
protected:
    std::string GetModelPath() const override { return "fence.obj"; }
    std::string GetTexturePath() const override { return "fence.png"; }
public:
    const char* GetObjectName() const override { return "Fence"; }
};
```

### コンストラクタでモデルパスを指定

```cpp
class ModelObject : public CoreEngine::ModelGameObject {
public:
    explicit ModelObject(const std::string& modelPath = "")
        : modelPath_(modelPath) {}

    const char* GetObjectName() const override { return "Model"; }

protected:
    std::string GetModelPath() const override { return modelPath_; }

private:
    std::string modelPath_;
};
```

### シーン内で生成・配置

```cpp
void GameScene::OnInitialize() {
    SetSceneName("GameScene");

    // モデルオブジェクトの生成
    auto fence = CreateObject<FenceObject>();
    fence->GetTransform().translate = { 5.0f, 0.0f, 0.0f };
    fence->GetTransform().scale = { 1.0f, 1.0f, 1.0f };
    fence->SetActive(true);

    // コンストラクタ引数付きで生成
    auto sphere = CreateObject<ModelObject>("sphere.obj");
    sphere->GetTransform().translate = { 0.0f, 2.0f, 0.0f };
    sphere->SetActive(true);
}
```

## トランスフォーム操作

```cpp
auto obj = CreateObject<ModelObject>("cube.obj");

// 位置
obj->GetTransform().translate = { 10.0f, 0.0f, 5.0f };

// 回転（ラジアン）
obj->GetTransform().rotate = { 0.0f, 3.14f, 0.0f };

// スケール
obj->GetTransform().scale = { 2.0f, 2.0f, 2.0f };

// クォータニオン回転モードに切り替え
obj->GetTransform().SetRotationMode(WorldTransform::RotationMode::Quaternion);
obj->GetTransform().quaternionRotate = { 0.0f, 0.707f, 0.0f, 0.707f };

// 親子関係の設定
auto child = CreateObject<ModelObject>("part.obj");
child->GetTransform().SetParent(&obj->GetTransform());
```

## PBR マテリアル操作

```cpp
auto sphere = CreateObject<ModelObject>("sphere.obj");

// PBR パラメータの設定
sphere->SetPBRParameters(
    0.8f,   // metallic  (0.0 = 非金属, 1.0 = 金属)
    0.2f,   // roughness (0.0 = 鏡面, 1.0 = 粗面)
    1.0f    // ambient occlusion
);

// マテリアルカラーの設定
sphere->SetMaterialColor(Vector4{ 1.0f, 0.5f, 0.3f, 1.0f });

// IBL の有効化
sphere->SetIBLEnabled(true);
sphere->SetIBLIntensity(1.0f);

// PBR テクスチャマップの有効/無効
sphere->SetPBRTextureMapsEnabled(
    true,   // useNormal
    true,   // useMetallic
    true,   // useRoughness
    true    // useAO
);
```

## オーバーライド可能なフック

| メソッド | タイミング | 説明 |
|---------|-----------|------|
| `GetModelPath()` | Initialize 時 | モデルファイルパスを返す |
| `GetTexturePath()` | Initialize 時 | テクスチャファイルパスを返す（空 = モデル組み込み） |
| `OnInitialize()` | Initialize の最後 | 初期化後の追加処理 |
| `OnUpdate()` | Update 内（TransferMatrix の後） | 更新後の追加処理 |
| `OnDraw(camera)` | Draw 内（model_->Draw の後） | 描画後の追加処理 |

## 保護メンバー

| メンバー | 型 | 説明 |
|---------|-----|------|
| `model_` | `unique_ptr<Model>` | 3D モデル |
| `transform_` | `WorldTransform` | ワールドトランスフォーム |
| `texture_` | `LoadedTexture` | テクスチャハンドル |

## シリアライズ

`ModelGameObject` は Transform と active 状態を自動的に JSON に保存・復元します。  
追加のプロパティを保存したい場合は `OnSerialize()` / `OnDeserialize()` をオーバーライドしてください。
