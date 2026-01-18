# フォントレンダリングシステム 使用ガイド

FreeTypeライブラリを使用したフォントレンダリングシステムの使用方法を説明します。

## ?? 概要

このシステムは、既存のRenderManager設計に統合されており、以下の特徴があります：

- **自動レンダリング**: `RenderManager`が`TextObject`を自動的に描画
- **UTF-8対応**: 日本語、英語、数字、記号をサポート
- **効率的なキャッシング**: 一度レンダリングしたグリフを再利用
- **GameObject統合**: 既存の`SpriteObject`と同じパターンで使用可能

---

## ?? 基本的な使い方

### 1. フォントファイルの準備

プロジェクトに.ttfまたは.otfフォントファイルを配置します。

```
Project/
└── Assets/
    └── Fonts/
        └── YourFont.ttf
```

### 2. シーンでTextObjectを作成

```cpp
// TestScene.cpp などで

void TestScene::Initialize(EngineSystem* engine) {
    BaseScene::Initialize(engine);

    // TextObjectの作成
    auto textObject = CreateObject<TextObject>();
    textObject->Initialize("Assets/Fonts/YourFont.ttf", 32);  // フォントパスとサイズ
    textObject->SetText("Hello, World!");
    textObject->GetTransform().translate = { 100.0f, 100.0f, 0.0f };
    textObject->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });  // 白色
    textObject->SetScale(1.0f);
    textObject->SetActive(true);
}
```

### 3. 日本語テキストの表示

```cpp
auto japaneseText = CreateObject<TextObject>();
japaneseText->Initialize("Assets/Fonts/JapaneseFont.ttf", 24);
japaneseText->SetText("こんにちは、世界！");
japaneseText->GetTransform().translate = { 100.0f, 200.0f, 0.0f };
japaneseText->SetActive(true);
```

### 4. 動的なテキスト更新

```cpp
// Update()内で
void TestScene::Update() {
    BaseScene::Update();

    // フレームカウントを表示
    auto* fpsText = GetObject<TextObject>("FPSCounter");
    if (fpsText) {
        fpsText->SetText("FPS: " + std::to_string(currentFPS));
    }
}
```

---

## ?? カスタマイズオプション

### テキストの色を変更

```cpp
textObject->SetColor({ 1.0f, 0.0f, 0.0f, 1.0f });  // 赤色
textObject->SetColor({ 0.0f, 1.0f, 0.0f, 0.5f });  // 半透明の緑
```

### テキストのスケール調整

```cpp
textObject->SetScale(2.0f);   // 2倍のサイズ
textObject->SetScale(0.5f);   // 半分のサイズ
```

### 位置の設定

```cpp
textObject->GetTransform().translate = { x, y, 0.0f };
```

座標系:
- 左上が (0, 0)
- 右下が (1280, 720) ※ウィンドウサイズに依存

### ブレンドモードの設定

```cpp
textObject->SetBlendMode(BlendMode::kBlendModeNormal);  // 通常（デフォルト）
textObject->SetBlendMode(BlendMode::kBlendModeAdd);     // 加算合成
textObject->SetBlendMode(BlendMode::kBlendModeSubtract); // 減算合成
```

---

## ?? 複数のTextObjectを使用

```cpp
// タイトル
auto titleText = CreateObject<TextObject>();
titleText->Initialize("Assets/Fonts/Bold.ttf", 48);
titleText->SetText("Game Title");
titleText->GetTransform().translate = { 640.0f, 100.0f, 0.0f };
titleText->SetColor({ 1.0f, 0.8f, 0.0f, 1.0f });  // ゴールド

// スコア表示
auto scoreText = CreateObject<TextObject>();
scoreText->Initialize("Assets/Fonts/Regular.ttf", 32);
scoreText->SetText("Score: 0");
scoreText->GetTransform().translate = { 50.0f, 50.0f, 0.0f };

// 日本語説明文
auto instructionText = CreateObject<TextObject>();
instructionText->Initialize("Assets/Fonts/Japanese.ttf", 20);
instructionText->SetText("スペースキーでスタート");
instructionText->GetTransform().translate = { 400.0f, 500.0f, 0.0f };
```

---

## ?? デバッグ機能 (ImGui)

Debug/Development ビルドでは、ImGuiでテキストをリアルタイム編集できます：

```cpp
#ifdef _DEBUG
textObject->DrawImGui();  // ImGuiウィンドウで編集可能
#endif
```

編集可能な項目:
- テキスト内容
- 色 (RGBA)
- スケール
- 位置
- ブレンドモード

---

## ?? 高度な使い方

### デフォルトフォントの設定

アプリケーション起動時に共通フォントを設定:

```cpp
// EngineSystem初期化後
FontManager::GetInstance().SetDefaultFont("Assets/Fonts/Default.ttf", 24);

// 後で取得
Font* defaultFont = FontManager::GetInstance().GetDefaultFont();
```

### 改行対応

```cpp
textObject->SetText("Line 1\nLine 2\nLine 3");
```

### 文字コードの扱い

UTF-8エンコーディングで保存されたテキストファイルを読み込む:

```cpp
std::ifstream file("Assets/Text/story.txt");
std::string content((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
textObject->SetText(content);
```

---

## ?? 注意事項

1. **フォントファイルのエンコーディング**: UTF-8対応フォントを使用してください
2. **日本語フォント**: 日本語を表示する場合は、日本語グリフを含むフォントが必要
3. **パフォーマンス**: 大量のTextObjectを作成する場合、同じフォント・サイズを共有することでキャッシュが効率化されます
4. **描画順序**: RenderManagerが自動的に`Text`パスで描画します（Spriteの後）

---

## ?? 完全な使用例

```cpp
// TestScene.cpp
void TestScene::Initialize(EngineSystem* engine) {
    BaseScene::Initialize(engine);

    // タイトル
    auto title = CreateObject<TextObject>();
    title->Initialize("Assets/Fonts/Bold.ttf", 48);
    title->SetText("My Awesome Game");
    title->GetTransform().translate = { 640.0f, 100.0f, 0.0f };
    title->SetColor({ 1.0f, 0.8f, 0.0f, 1.0f });
    title->SetActive(true);

    // スコア
    scoreText_ = CreateObject<TextObject>();
    scoreText_->Initialize("Assets/Fonts/Regular.ttf", 32);
    scoreText_->SetText("Score: 0");
    scoreText_->GetTransform().translate = { 50.0f, 50.0f, 0.0f };
    scoreText_->SetActive(true);

    // 日本語テキスト
    auto instruction = CreateObject<TextObject>();
    instruction->Initialize("Assets/Fonts/NotoSansJP.ttf", 24);
    instruction->SetText("ゲームを楽しんでください！");
    instruction->GetTransform().translate = { 400.0f, 500.0f, 0.0f };
    instruction->SetActive(true);
}

void TestScene::Update() {
    BaseScene::Update();

    // スコア更新
    if (scoreText_) {
        scoreText_->SetText("Score: " + std::to_string(currentScore_));
    }
}
```

---

## ?? トラブルシューティング

### テキストが表示されない

1. フォントファイルパスが正しいか確認
2. `SetActive(true)`を呼び出しているか確認
3. 色のアルファ値が0になっていないか確認
4. FontManagerが初期化されているか確認（EngineSystemが自動で行います）

### 日本語が表示されない

1. UTF-8エンコーディングで保存されているか確認
2. 日本語グリフを含むフォントを使用しているか確認（例: Noto Sans JP, M+ FONTS）

### パフォーマンスが悪い

1. 同じフォント・サイズを複数のTextObjectで共有
2. 毎フレーム新しいTextObjectを作成しない
3. テキストが変わらない場合は`SetText()`を呼び出さない

---

## ?? 関連クラス

- `TextObject` - テキスト描画オブジェクト (GameObject継承)
- `TextRenderer` - テキスト描画用レンダラー (IRenderer継承)
- `Font` - 個別フォント管理
- `FontManager` - フォント全体の管理（シングルトン）
- `Glyph` - グリフ（文字）のデータ構造

---

これで、既存のSpriteObjectと同じように、簡単にテキストを画面に表示できます！
