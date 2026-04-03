# SpriteObject

`SpriteObject` は 2D スプライトを描画するためのゲームオブジェクトです。  
`GameObject` を継承し、テクスチャベースの 2D 描画機能を提供します。

## ヘッダ

```cpp
#include "ObjectCommon/Sprite/SpriteObject.h"
```

## 概要

- テクスチャファイルパスを指定して初期化します
- 色・UV変換・アンカーポイントなどの 2D 固有パラメータを設定できます
- `RenderPassType::Sprite` として自動的にスプライト描画パスで処理されます

## 使い方

### シーン内での生成

```cpp
void UIScene::OnInitialize() {
    SetSceneName("UIScene");

    // スプライトオブジェクトの生成
    auto hpBar = CreateObject<SpriteObject>();
    hpBar->Initialize("hpBar.png", "HPBar");
    hpBar->SetActive(true);
}
```

### カスタムスプライトオブジェクトの作成

```cpp
class TitleLogo : public CoreEngine::SpriteObject {
public:
    void Initialize() override {
        SpriteObject::Initialize("title_logo.png", "TitleLogo");
    }

    void Update() override {
        SpriteObject::Update();
        // ロゴ固有のアニメーション処理
    }
};
```

## プロパティ設定

```cpp
auto sprite = CreateObject<SpriteObject>();
sprite->Initialize("button.png");

// 色の設定（RGBA）
sprite->SetColor(Vector4{ 1.0f, 1.0f, 1.0f, 0.8f });

// アンカーポイントの設定
// (0,0)=左上  (0.5,0.5)=中央  (1,1)=右下
sprite->SetAnchor(Vector2{ 0.5f, 0.5f });

// テクスチャの変更
sprite->SetTexture("button_pressed.png");

// テクスチャサイズの取得
Vector2 size = sprite->GetTextureSize();
```

## 主要メソッド

| メソッド | 説明 |
|---------|------|
| `Initialize(texturePath, name)` | テクスチャとオブジェクト名を指定して初期化 |
| `SetTexture(path)` | テクスチャを変更 |
| `SetColor(color)` | 色を設定（Vector4: RGBA） |
| `GetColor()` | 現在の色を取得 |
| `SetAnchor(anchor)` | アンカーポイントを設定 |
| `GetAnchor()` | アンカーポイントを取得 |
| `SetUVTransform(matrix)` | UV変換行列を設定 |
| `GetTextureSize()` | テクスチャの実際のサイズ（ピクセル）を取得 |

## シリアライズ

`SpriteObject` は Transform と active 状態を自動的に JSON に保存・復元します。
