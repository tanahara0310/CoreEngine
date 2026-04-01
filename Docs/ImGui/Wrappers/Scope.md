# UI::Scope — RAII スコープ

`#include "Utility/Debug/ImGui/ImGuiAll.h"`  
名前空間: `CoreEngine::UI::Scope`

---

## 概要

ImGui には `Begin/End` のペアが多く存在し、`End` の呼び忘れがクラッシュや UI の破壊につながります。  
RAII スコープはこの問題をコンパイル時に解決します。

**原則**: C++ のブロック `{ }` を抜けると自動的に `End` が呼ばれます。

---

## TreeScope（フラグ対応）

ツリーノードの開閉を管理します。`ImGuiTreeNodeFlags` を渡せます。

```cpp
// 基本
if (auto s = UI::Scope::TreeScope("Transform")) {
    UI::DragVec3("Position", pos);
}

// デフォルトで開いた状態にする
if (auto s = UI::Scope::TreeScope("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
    UI::Label("パーティクル数: 100");
}
```

---

## ChildScope

スクロール可能な子ウィンドウを管理します。  
`EndChild()` は常に呼ぶ必要があるため、デストラクタで無条件に呼び出します。

```cpp
// シンプルなスクロール領域
if (auto child = UI::Scope::ChildScope("ScrollArea", ImVec2(0, 200))) {
    for (const auto& item : items) { ImGui::Text("%s", item.c_str()); }
}

// 枠線付き
if (auto child = UI::Scope::ChildScope("List", ImVec2(0, 150), ImGuiChildFlags_Border)) {
    // ...
}

// 横スクロールバー付き
if (auto child = UI::Scope::ChildScope("Log", ImVec2(0, -footerH),
    0, ImGuiWindowFlags_HorizontalScrollbar)) {
    // ...
}
```

| 引数 | 型 | 説明 |
|---|---|---|
| `str_id` | `const char*` | 子ウィンドウ ID |
| `size` | `ImVec2` | サイズ（0 = 自動） |
| `child_flags` | `ImGuiChildFlags` | `ImGuiChildFlags_Border` で枠線表示 |
| `window_flags` | `ImGuiWindowFlags` | スクロールバーなどのオプション |

---

## ListBoxScope

リストボックスを管理します。

```cpp
if (auto lb = UI::Scope::ListBoxScope("シーケンス一覧", ImVec2(-1.0f, 120.0f))) {
    for (int i = 0; i < count; ++i) {
        if (ImGui::Selectable(items[i].c_str(), selected == i)) { selected = i; }
    }
}
```

---

## ModalScope

モーダルダイアログを管理します。  
事前に `ImGui::OpenPopup(name)` で開く必要があります。

```cpp
if (ImGui::Button("削除")) { ImGui::OpenPopup("確認"); }

if (auto m = UI::Scope::ModalScope("確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    UI::Label("本当に削除しますか？");
    if (ImGui::Button("はい")) { DoDelete(); ImGui::CloseCurrentPopup(); }
    UI::SameLine();
    if (ImGui::Button("いいえ")) { ImGui::CloseCurrentPopup(); }
}
```

---

## GroupScope

`BeginGroup/EndGroup` を RAII で管理します。  
グループ内の複数ウィジェットを 1 つの項目として扱います（ツールチップ・カーソル位置計算に有効）。

```cpp
{
    UI::Scope::GroupScope g;
    UI::DragFloat("X", pos.x, 0.1f);
    UI::DragFloat("Y", pos.y, 0.1f);
}
// グループ全体にツールチップ設定
UI::Tooltip("2D 座標");
```

---

## CollapsingScope

折りたたみヘッダーを管理します。TreeScope と同様のパターンで使います。

```cpp
if (auto s = UI::Scope::CollapsingScope("Advanced Settings")) {
    // ヘッダーが開いている間だけ実行される
}

// フラグを指定する場合
if (auto s = UI::Scope::CollapsingScope("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
    // デフォルトで開いた状態にする
}
```

---

## DisabledScope

ウィジェットをグレーアウト（操作不能）にします。

```cpp
// テクスチャがない場合はグレーアウト
{
    UI::Scope::DisabledScope ds(!hasTexture);
    UI::Widgets::ToggleSwitch("Normal Map", &normalMapEnabled);
    // ← ブロック終了時に自動で EndDisabled() が呼ばれる
}

// ブロック外はグレーアウトされない
UI::SameLine();
UI::Hint("(なし)");
```

### 常に無効にする

```cpp
{
    UI::Scope::DisabledScope ds;  // 引数なし = 常に無効
    // ...
}
```

---

## IndentScope

テキストを字下げします。

```cpp
if (enableDithering) {
    UI::Scope::IndentScope is;
    UI::SliderFloat("Scale", scale, 0.1f, 5.0f);
    // ← ブロック終了時に自動で Unindent() が呼ばれる
}
```

### 字下げ量を指定する

```cpp
UI::Scope::IndentScope is(16.0f);  // 16px 字下げ
```

---

## WindowScope

ImGui ウィンドウを管理します。

> ⚠️ `Begin()` が false を返した場合（ウィンドウが折りたたまれている等）でも  
> `End()` は必ず呼ぶ必要があります。WindowScope はこれを自動で行います。

```cpp
if (auto w = UI::Scope::WindowScope("Inspector")) {
    // ウィンドウが表示されている場合のみ内容を描画
}
// ← ここで常に End() が呼ばれる
```

### 閉じるボタンを付ける

```cpp
bool isOpen = true;
if (auto w = UI::Scope::WindowScope("Settings", &isOpen)) {
    // ...
}
if (!isOpen) {
    // ウィンドウが閉じられた
}
```

---

## TabBarScope / TabItemScope

タブバーとタブ項目を管理します。

```cpp
if (auto tabBar = UI::Scope::TabBarScope("MainTabs")) {
    if (auto tab = UI::Scope::TabItemScope("General")) {
        // General タブの内容
    }
    if (auto tab = UI::Scope::TabItemScope("Advanced")) {
        // Advanced タブの内容
    }
}
```

---

## PopupScope

ポップアップを管理します。

```cpp
// ポップアップを開く
if (ImGui::Button("Open")) {
    ImGui::OpenPopup("MyPopup");
}

// ポップアップの内容
if (auto popup = UI::Scope::PopupScope("MyPopup")) {
    UI::Label("ポップアップの内容");
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }
}
```

---

## スコープ一覧

| クラス | Begin | End | 備考 |
|---|---|---|---|
| `TreeScope` | `TreeNodeEx` | `TreePop` | flags 対応。open が false なら End しない |
| `CollapsingScope` | `CollapsingHeader` | なし | bool のみ返す |
| `DisabledScope` | `BeginDisabled` | `EndDisabled` | 常に End する |
| `IndentScope` | `Indent` | `Unindent` | 常に End する |
| `WindowScope` | `Begin` | `End` | open に関わらず常に End する |
| `ChildScope` | `BeginChild` | `EndChild` | open に関わらず常に End する |
| `ListBoxScope` | `BeginListBox` | `EndListBox` | open が false なら End しない |
| `ModalScope` | `BeginPopupModal` | `EndPopup` | open が false なら End しない |
| `GroupScope` | `BeginGroup` | `EndGroup` | 常に End する |
| `TabBarScope` | `BeginTabBar` | `EndTabBar` | open が false なら End しない |
| `TabItemScope` | `BeginTabItem` | `EndTabItem` | open が false なら End しない |
| `PopupScope` | `BeginPopup` | `EndPopup` | open が false なら End しない |
