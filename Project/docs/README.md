# CoreEngine

**高性能な3D/2Dゲームエンジン - DirectX12ベース**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![DirectX12](https://img.shields.io/badge/DirectX-12-green.svg)](https://docs.microsoft.com/en-us/windows/win32/direct3d12/direct3d-12-graphics)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

## ?? 目次

- [概要](#概要)
- [主な機能](#主な機能)
- [システム要件](#システム要件)
- [プロジェクト構成](#プロジェクト構成)
- [クイックスタート](#クイックスタート)
- [使い方](#使い方)
- [アーキテクチャ](#アーキテクチャ)
- [ドキュメント](#ドキュメント)
- [ライセンス](#ライセンス)

---

## 概要

CoreEngineは、C++20とDirectX12を使用した高性能なゲームエンジンです。モダンなC++の機能を活用し、メモリ安全性とパフォーマンスの両立を実現しています。

### 特徴

? **モダンなC++20設計** - スマートポインタ、テンプレート、constexpr等を活用  
?? **コンポーネントベースアーキテクチャ** - 柔軟で拡張性の高い設計  
?? **高性能レンダリング** - DirectX12による最適化されたグラフィックスパイプライン  
?? **強力なパーティクルシステム** - Unity風のモジュラー設計  
?? **包括的なデバッグツール** - ImGuiによるリアルタイムデバッグUI  
?? **統合オーディオシステム** - XAudio2による高品質サウンド再生  

---

## 主な機能

### グラフィックス

- **3Dモデルレンダリング**
  - 静的モデル（OBJ形式対応）
  - スキンメッシュアニメーション
  - スケルトンアニメーション
  - 法線マッピング、スペキュラマッピング

- **2Dグラフィックス**
  - スプライトシステム
  - テキストレンダリング（TrueTypeフォント対応）
  - アルファブレンディング

- **ポストエフェクト**
  - ブルーム
  - ビネット
  - グレースケール
  - カスタムエフェクトチェーン対応

- **パーティクルシステム**
  - ビルボードパーティクル
  - 3Dモデルパーティクル
  - エミッション制御
  - グラデーションカラー
  - サイズ・回転アニメーション

### エンジン機能

- **シーン管理**
  - シーンの動的切り替え
  - トランジションエフェクト
  - シーンライフサイクル管理

- **GameObjectシステム**
  - 自動メモリ管理
  - GPU同期を考慮した安全な削除
  - LateUpdateパターン
  - コンポーネントベース設計

- **カメラシステム**
  - 3D透視投影カメラ
  - 2D正射影カメラ
  - デバッグカメラ（フリールック）
  - カメラマネージャーによる一元管理

- **衝突判定**
  - AABB（軸平行境界ボックス）
  - 球体コライダー
  - レイヤーベースの衝突フィルタリング
  - コールバックシステム

- **入力システム**
  - キーボード（DirectInput）
  - マウス
  - ゲームパッド（XInput対応）

- **オーディオ**
  - BGM・SE再生
  - ボリューム制御
  - ループ再生
  - 複数形式対応（WAV, MP3）

### 開発支援

- **デバッグUI（ImGui統合）**
  - シーン切り替え
  - パフォーマンスモニタ
  - オブジェクトインスペクター
  - パーティクルエディタ
  - ポストエフェクトエディタ

- **ロギングシステム**
  - レベル別ログ出力
  - ファイル出力
  - リアルタイムコンソール表示

- **プリセット管理**
  - パーティクルプリセット
  - ポストエフェクトプリセット
  - JSON形式での保存

---

## システム要件

### 必須環境

- **OS**: Windows 10/11 (64-bit)
- **CPU**: SSE2対応プロセッサ
- **GPU**: DirectX 12対応グラフィックスカード
- **RAM**: 4GB以上
- **ストレージ**: 500MB以上の空き容量

### 開発環境

- **IDE**: Visual Studio 2022以降
- **C++標準**: C++20
- **Windows SDK**: 10.0.26100.0以降
- **ビルドツール**: MSBuild

### 依存ライブラリ

- DirectX 12
- XAudio2
- DirectInput
- ImGui (統合済み)
- assimp (モデル読み込み)
- stb_image (画像読み込み)
- FreeType (フォントレンダリング)

---

## プロジェクト構成

```
CoreEngine/
├── Project/
│   ├── Application/          # アプリケーション層
│   │   ├── MyGame.cpp        # ゲームメインクラス
│   │   └── MyGame.h
│   │
│   ├── Engine/               # エンジンコア
│   │   ├── Audio/            # オーディオシステム
│   │   ├── Camera/           # カメラシステム
│   │   ├── Collider/         # 衝突判定
│   │   ├── EngineSystem/     # エンジンシステム統合
│   │   ├── Framework/        # フレームワーク基底
│   │   ├── Graphics/         # グラフィックスシステム
│   │   │   ├── Common/       # DirectX基盤
│   │   │   ├── Font/         # フォントレンダリング
│   │   │   ├── Light/        # ライティング
│   │   │   ├── Model/        # 3Dモデル
│   │   │   ├── PostEffect/   # ポストエフェクト
│   │   │   ├── Render/       # レンダリングパイプライン
│   │   │   └── Shader/       # シェーダー管理
│   │   ├── Input/            # 入力システム
│   │   ├── ObjectCommon/     # GameObjectシステム
│   │   ├── Particle/         # パーティクルシステム
│   │   ├── Scene/            # シーン管理
│   │   └── Utility/          # ユーティリティ
│   │
│   ├── Assets/               # ゲームアセット
│   │   ├── Models/           # 3Dモデル
│   │   ├── Textures/         # テクスチャ
│   │   ├── Shaders/          # HLSLシェーダー
│   │   └── Audio/            # サウンドファイル
│   │
│   └── Docs/                 # ドキュメント
│       ├── README.md         # このファイル
│       └── SceneGuide.md     # シーン使用ガイド
│
└── External/                 # 外部ライブラリ
```

---

## クイックスタート

### 1. プロジェクトのクローン

```bash
git clone https://github.com/tanahara0310/CoreEngine.git
cd CoreEngine
```

### 2. Visual Studioでプロジェクトを開く

```
Project/CoreEngine.sln
```

### 3. ビルド構成の選択

- **Debug**: デバッグビルド（ImGuiデバッグUI有効）
- **Release**: リリースビルド（最適化）

### 4. ビルドと実行

```
F5キーを押してビルド＆実行
```

---

## 使い方

### ゲームアプリケーションの作成

#### 1. MyGameクラスの編集

```cpp
// Application/MyGame.cpp
#include "MyGame.h"

void MyGame::Initialize()
{
    // シーン管理システムの初期化
    sceneManager_ = std::make_unique<SceneManager>();
    sceneManager_->Initialize(GetEngineSystem());

    // シーンの登録
    sceneManager_->RegisterScene<GameScene>("GameScene");
    sceneManager_->RegisterScene<TitleScene>("TitleScene");

    // 初期シーンを設定
    sceneManager_->SetInitialScene("TitleScene");
}

void MyGame::Update()
{
    // シーンの更新
    if (sceneManager_) {
        sceneManager_->Update();
    }
}

void MyGame::Draw()
{
    // シーンの描画
    if (sceneManager_) {
        sceneManager_->Draw();
    }
}
```

#### 2. カスタムシーンの作成

```cpp
// GameScene.h
#pragma once
#include "Scene/BaseScene.h"

class GameScene : public BaseScene {
public:
    void Initialize(EngineSystem* engine) override;
    void Draw() override;
    void Finalize() override;

protected:
    void OnUpdate() override;

private:
    PlayerObject* player_ = nullptr;
};

// GameScene.cpp
#include "GameScene.h"

void GameScene::Initialize(EngineSystem* engine)
{
    // 基底クラスの初期化（必須）
    BaseScene::Initialize(engine);

    // プレイヤーオブジェクトの作成
    auto player = CreateObject<PlayerObject>();
    player->Initialize();
    player->SetActive(true);
    player_ = player;
}

void GameScene::OnUpdate()
{
    auto keyboard = engine_->GetComponent<KeyboardInput>();
    
    // プレイヤー移動
    if (player_) {
        Vector3 move = {0.0f, 0.0f, 0.0f};
        if (keyboard->IsKeyPressed(DIK_W)) move.z += 0.1f;
        if (keyboard->IsKeyPressed(DIK_S)) move.z -= 0.1f;
        if (keyboard->IsKeyPressed(DIK_A)) move.x -= 0.1f;
        if (keyboard->IsKeyPressed(DIK_D)) move.x += 0.1f;
        
        player_->AddVelocity(move);
    }
}

void GameScene::Draw()
{
    BaseScene::Draw();
}

void GameScene::Finalize()
{
    // 特別な終了処理があれば実装
}
```

#### 3. GameObjectの作成

```cpp
// PlayerObject.h
#pragma once
#include "ObjectCommon/GameObject.h"

class PlayerObject : public GameObject {
public:
    void Initialize();
    void Update() override;
    void Draw(const ICamera* camera) override;
    
    void AddVelocity(const Vector3& velocity);
    
    RenderPassType GetRenderPassType() const override {
        return RenderPassType::Model;
    }

private:
    Vector3 velocity_;
    float speed_ = 5.0f;
};

// PlayerObject.cpp
#include "PlayerObject.h"

void PlayerObject::Initialize()
{
    auto modelManager = GetEngineSystem()->GetComponent<ModelManager>();
    model_ = modelManager->CreateStaticModel("Models/Player.obj");
    
    transform_.SetScale({1.0f, 1.0f, 1.0f});
    transform_.SetPosition({0.0f, 0.0f, 0.0f});
}

void PlayerObject::Update()
{
    // 速度を適用
    auto pos = transform_.GetPosition();
    pos += velocity_;
    transform_.SetPosition(pos);
    
    // 速度を減衰
    velocity_ *= 0.9f;
}

void PlayerObject::Draw(const ICamera* camera)
{
    if (model_) {
        model_->Draw(transform_, camera);
    }
}

void PlayerObject::AddVelocity(const Vector3& velocity)
{
    velocity_ += velocity * speed_;
}
```

### パーティクルシステムの使用

```cpp
void GameScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    auto dxCommon = engine_->GetComponent<DirectXCommon>();
    auto resourceFactory = engine_->GetComponent<ResourceFactory>();
    
    // パーティクルシステム作成
    auto particle = CreateObject<ParticleSystem>();
    particle->Initialize(dxCommon, resourceFactory, "Explosion");
    
    // 基本設定
    particle->SetEmitterPosition({0.0f, 0.0f, 0.0f});
    particle->SetBlendMode(BlendMode::kBlendModeAdd);
    particle->SetBillboardType(BillboardType::ViewFacing);
    
    // エミッション設定
    auto& emissionModule = particle->GetEmissionModule();
    auto emissionData = emissionModule.GetEmissionData();
    emissionData.rateOverTime = 50;
    emissionModule.SetEmissionData(emissionData);
    
    // 速度設定
    auto& velocityModule = particle->GetVelocityModule();
    auto velocityData = velocityModule.GetVelocityData();
    velocityData.startSpeed = {0.0f, 2.0f, 0.0f};
    velocityData.useRandomDirection = true;
    velocityModule.SetVelocityData(velocityData);
    
    // 再生開始
    particle->Play();
}
```

### ポストエフェクトの使用

```cpp
void GameScene::Initialize(EngineSystem* engine)
{
    BaseScene::Initialize(engine);
    
    auto postEffectManager = engine_->GetComponent<PostEffectManager>();
    
    // ブルームエフェクトを有効化
    postEffectManager->EnableEffect("Bloom");
    
    // パラメータ調整
    auto* bloom = postEffectManager->GetEffect<Bloom>("Bloom");
    if (bloom) {
        Bloom::BloomParams params;
        params.threshold = 0.8f;
        params.intensity = 1.5f;
        bloom->SetParams(params);
    }
}
```

---

## アーキテクチャ

### システム構成図

```
┌─────────────────────────────────────────────────────────┐
│                   Application Layer                      │
│                      (MyGame)                            │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                   Framework Layer                        │
│                    (Framework)                           │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                   Engine Layer                           │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐      │
│  │   Scene     │ │   Render    │ │   Physics   │      │
│  │   Manager   │ │   Manager   │ │   System    │      │
│  └─────────────┘ └─────────────┘ └─────────────┘      │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐      │
│  │  GameObject │ │   Camera    │ │    Input    │      │
│  │   Manager   │ │   Manager   │ │   Manager   │      │
│  └─────────────┘ └─────────────┘ └─────────────┘      │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐      │
│  │   Audio     │ │  Particle   │ │PostEffect   │      │
│  │   System    │ │   System    │ │   Manager   │      │
│  └─────────────┘ └─────────────┘ └─────────────┘      │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                 Graphics API Layer                       │
│                    (DirectX 12)                          │
└─────────────────────────────────────────────────────────┘
```

### GameObjectライフサイクル

```
┌─────────────────────────────────────────────────────────┐
│ CreateObject<T>()                                       │
│   ↓                                                     │
│ GameObject生成 (unique_ptr)                             │
│   ↓                                                     │
│ GameObjectManager::AddObject()                          │
│   ↓                                                     │
│ objects_.push_back(std::move(obj))                      │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 毎フレーム: UpdateAll()                                 │
│   ↓                                                     │
│ for each active && !markedForDestroy                    │
│   obj->Update()                                         │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 毎フレーム: RegisterAllToRender()                       │
│   ↓                                                     │
│ RenderManager::AddDrawable(obj)                         │
│   ↓                                                     │
│ DrawQueue追加                                           │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ obj->Destroy() が呼ばれた場合                            │
│   ↓                                                     │
│ markedForDestroy_ = true                                │
│   ↓                                                     │
│ CleanupDestroyed()                                      │
│   ↓                                                     │
│ destroyQueue_.push_back(std::move(obj))                 │
│   ↓                                                     │
│ 次フレームで実際に削除（GPU同期完了後）                  │
└─────────────────────────────────────────────────────────┘
```

### レンダリングパイプライン

```
┌─────────────────────────────────────────────────────────┐
│ 1. オフスクリーンレンダリング開始                         │
│    Render::OffscreenPreDraw(0)                          │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 2. シーン描画                                           │
│    BaseScene::Draw()                                    │
│      ↓                                                  │
│    RenderManager::DrawAll()                             │
│      ↓                                                  │
│    パスごとにソート＆描画                                │
│      - Model Pass                                       │
│      - SkinnedModel Pass                                │
│      - SkyBox Pass                                      │
│      - Particle Pass                                    │
│      - Sprite Pass                                      │
│      - Text Pass                                        │
│      - Line Pass                                        │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 3. オフスクリーンレンダリング終了                         │
│    Render::OffscreenPostDraw(0)                         │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 4. ポストエフェクトチェーン適用                           │
│    PostEffectManager::ExecuteEffectChain()              │
│      ↓                                                  │
│    各エフェクトを順次適用                                │
│      - Bloom                                            │
│      - Vignette                                         │
│      - GrayScale                                        │
│      - etc...                                           │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 5. バックバッファに最終結果を描画                         │
│    Render::BackBufferPreDraw()                          │
│      ↓                                                  │
│    PostEffect::ExecuteEffect("FullScreen")              │
│      ↓                                                  │
│    ImGui描画 (DEBUG)                                    │
│      ↓                                                  │
│    Render::BackBufferPostDraw()                         │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│ 6. Present                                              │
│    DirectXCommon::Present()                             │
└─────────────────────────────────────────────────────────┘
```

---

## ドキュメント

詳細なドキュメントは`Docs/`フォルダに格納されています。

- **[シーンシステム使用ガイド](SceneGuide.md)** - シーンの作成と管理方法
- **[GameObjectシステム](GameObjectSystem.md)** - GameObjectの作成と使い方
- **[パーティクルシステム](ParticleSystem.md)** - パーティクルエフェクトの使い方
- **[ポストエフェクト](PostEffect.md)** - ポストエフェクトの適用方法
- **[APIリファレンス](API/index.html)** - 完全なAPIドキュメント

---

## パフォーマンス最適化

### 実装済みの最適化

? **レンダリングパイプラインの最適化**
- ドローコールのバッチング
- ステート変更の最小化
- 描画パスごとのソート

? **メモリ管理の最適化**
- スマートポインタによる自動メモリ管理
- オブジェクトプーリング（一部実装）
- GPU同期を考慮したダブルバッファリング削除

? **更新処理の最適化**
- 不要なコピーの削除
- 二重ループの除去
- キャッシュ効率の向上

### パフォーマンス測定

```cpp
// デバッグビルドでFPS表示
auto frameRate = engine->GetComponent<FrameRateController>();
float fps = frameRate->GetFPS();
float deltaTime = frameRate->GetDeltaTime();
```

---

## トラブルシューティング

### よくある問題

#### Q1: ビルドエラーが発生する

**解決策:**
1. Windows SDKのバージョンを確認（10.0.26100.0以降）
2. C++標準をC++20に設定
3. プロジェクトのクリーン＆リビルド

#### Q2: 実行時にクラッシュする

**解決策:**
1. デバッグビルドで実行
2. `Output`ウィンドウでエラーログを確認
3. `Logs/`フォルダのログファイルを確認

#### Q3: モデルが表示されない

**チェックリスト:**
- モデルファイルのパスが正しいか
- `SetActive(true)`を呼んでいるか
- カメラの位置・向きが適切か
- ライトが設定されているか

#### Q4: パーティクルが動作しない

**チェックリスト:**
- `Initialize()`を呼んでいるか
- `Play()`を呼んでいるか
- エミッション設定が正しいか
- カメラとの距離が適切か

---

## コントリビューション

貢献を歓迎します！以下の手順でコントリビュートできます。

1. このリポジトリをフォーク
2. フィーチャーブランチを作成 (`git checkout -b feature/AmazingFeature`)
3. 変更をコミット (`git commit -m 'Add some AmazingFeature'`)
4. ブランチにプッシュ (`git push origin feature/AmazingFeature`)
5. プルリクエストを作成

### コーディング規約

- C++20標準に準拠
- スマートポインタを優先的に使用
- コメントは日本語でもOK
- ImGuiを使ったデバッグUIの追加を推奨

---

## ライセンス

このプロジェクトはMITライセンスの下でライセンスされています。詳細は[LICENSE](../LICENSE)ファイルを参照してください。

---

## クレジット

### 開発

- **Main Developer**: tanahara0310
- **Repository**: https://github.com/tanahara0310/CoreEngine

### 使用ライブラリ

- [DirectX 12](https://docs.microsoft.com/en-us/windows/win32/direct3d12/) - Microsoft
- [ImGui](https://github.com/ocornut/imgui) - Omar Cornut
- [Assimp](https://www.assimp.org/) - Open Asset Import Library
- [stb](https://github.com/nothings/stb) - Sean Barrett
- [FreeType](https://www.freetype.org/) - David Turner

---

## 連絡先

プロジェクトに関する質問や提案は、以下の方法でお願いします：

- **GitHub Issues**: [https://github.com/tanahara0310/CoreEngine/issues](https://github.com/tanahara0310/CoreEngine/issues)
- **Email**: (メールアドレスがあれば記載)

---

## 更新履歴

### v1.0.0 (2024-01-03)
- 初回リリース
- GameObjectシステム実装
- パーティクルシステム実装
- ポストエフェクトシステム実装
- シーン管理システム実装

---

**CoreEngine - Build Your Dreams ??**
