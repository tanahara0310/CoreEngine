# ImGuiドッキング機能 - タブ登録ガイド

## 概要
このドキュメントはImGuiのドッキング機能を使用して新しいタブ（ウィンドウ）を登録する手順を説明します。

## 必要なファイル
- `Engine/Utility/Debug/ImGui/DockingUI.h` - ドッキングUI管理クラス
- `Engine/Utility/Debug/ImGui/DockingUI.cpp` - 実装ファイル

## ドッキングエリア
利用可能なドッキングエリアは以下の通りです：

```cpp
enum class DockArea {
    LeftTop,        // 左上（エンジン情報など）
    LeftBottom,     // 左下（カメラ情報など）
    Center,         // 中央（シーンビュー）
    Right,          // 右側（インスペクター）
    BottomLeft,     // 下部左（ライティング）
    BottomRight     // 下部右（オブジェクト制御）
};
```

## 基本的な使用方法

### 1. DockingUIの取得
```cpp
auto dockingUI = engine->GetDockingUI();
```

### 2. ウィンドウの登録
```cpp
// ウィンドウ名とドッキングエリアを指定して登録
dockingUI->RegisterWindow("MyWindow", DockArea::Right);
```

### 3. ImGuiウィンドウの作成
```cpp
// 通常のImGuiウィンドウを作成
if (ImGui::Begin("MyWindow")) {
    // ここにUI要素を記述
    ImGui::Text("Hello, World!");
}
ImGui::End();
```

## 実装例

### シンプルなウィンドウ登録
```cpp
void MyClass::Initialize(EngineSystem* engine) {
    auto dockingUI = engine->GetDockingUI();
    
    // 右側エリアに"Object Inspector"ウィンドウを登録
    dockingUI->RegisterWindow("Object Inspector", DockArea::Right);
}

void MyClass::Update() {
    // ImGuiウィンドウを描画
    if (ImGui::Begin("Object Inspector")) {
        ImGui::Text("オブジェクト情報");
        // 他のUI要素...
    }
    ImGui::End();
}
```

### TestSceneでの実装例
```cpp
void TestScene::Initialize(EngineSystem* engine) {
    engine_ = engine;
    
    // ドッキングUIの取得と登録
    auto dockingUI = engine_->GetDockingUI();
    if (dockingUI) {
        // オブジェクト制御ウィンドウを右下に登録
        dockingUI->RegisterWindow("ObjectControl", DockArea::BottomRight);
        
        // カスタムデバッグウィンドウを左上に登録
        dockingUI->RegisterWindow("Custom Debug", DockArea::LeftTop);
    }
}

void TestScene::Update() {
    // オブジェクト制御ウィンドウ（既存のコード）
    if (ImGui::Begin("ObjectControl")) {
        // オブジェクト制御のUI...
    }
    ImGui::End();
    
    // カスタムデバッグウィンドウ
    if (ImGui::Begin("Custom Debug")) {
        ImGui::Text("カスタムデバッグ情報");
        // カスタムUI要素...
    }
    ImGui::End();
}
```

## ウィンドウの登録解除
```cpp
// ウィンドウの登録を解除
dockingUI->UnregisterWindow("MyWindow");
```

## 注意事項

1. **ウィンドウ名の一意性**: 同じ名前のウィンドウを複数登録しないでください
2. **ライフサイクル**: ウィンドウを使用しなくなったら`UnregisterWindow`で登録を解除してください
3. **初期化タイミング**: エンジンシステムの初期化後にウィンドウを登録してください
4. **既存のウィンドウ**: 既に使用されているウィンドウ名（"ObjectControl"など）と重複しないようにしてください

## よくある使用パターン

### デバッグ情報表示
```cpp
dockingUI->RegisterWindow("Debug Info", DockArea::LeftTop);
```

### オブジェクト制御UI
```cpp
dockingUI->RegisterWindow("Object Control", DockArea::BottomRight);
```

### シーンビューアー
```cpp
dockingUI->RegisterWindow("Scene View", DockArea::Center);
```

### ライティング制御
```cpp
dockingUI->RegisterWindow("Lighting Control", DockArea::BottomLeft);
```

## トラブルシューティング

### ウィンドウが表示されない
- ウィンドウ名が正しく登録されているか確認
- `ImGui::Begin()`と`ImGui::End()`が正しくペアになっているか確認
- エンジンシステムが正しく初期化されているか確認

### レイアウトが崩れる
- ドッキングUIの初期化が完了してから登録しているか確認
- 複数の同名ウィンドウが登録されていないか確認

### 既存のウィンドウと競合する
- TestSceneで使用されている"ObjectControl"など、既存のウィンドウ名を避ける
- 独自の命名規則を使用する（例：プロジェクト名_機能名）

---

このガイドに従って、効率的にImGuiドッキング機能を活用してください。