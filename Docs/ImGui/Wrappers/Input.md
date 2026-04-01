# UI:: — 入力ウィジェット

`#include "Utility/Debug/ImGui/ImGuiAll.h"`  
名前空間: `CoreEngine::UI`

---

## 概要

ImGui の入力系関数をラップしています。  
エンジン固有の型（`Vector2` / `Vector3` / `Vector4`）をそのまま渡せることと、  
引数の意味をドキュメントで明確にすることが目的です。

**戻り値**: 値が変更された場合に `true` を返します。

---

## ドラッグ入力

マウスドラッグで値を変更するウィジェットです。

### DragVec3 — Vector3 を編集する

```cpp
Vector3& pos = transform_.translate;
if (UI::DragVec3("Position", pos, 0.1f)) {
    // pos が変更された
}
```

| 引数 | 型 | 説明 |
|---|---|---|
| `label` | `const char*` | 表示ラベル |
| `v` | `Vector3&` | 編集する Vector3 |
| `speed` | `float` | ドラッグ感度（デフォルト: 1.0） |
| `min` | `float` | 最小値（0.0f = 制限なし） |
| `max` | `float` | 最大値（0.0f = 制限なし） |

### DragVec2 — Vector2 を編集する

```cpp
Vector2& uv = spriteOffset;
UI::DragVec2("UV Offset", uv, 0.01f);
```

### DragFloat — float を編集する

```cpp
float radius = 1.0f;
UI::DragFloat("Radius", radius, 0.05f, 0.0f, 100.0f);
```

### DragInt — int を編集する

```cpp
int count = 10;
UI::DragInt("Count", count, 1.0f, 0, 100);
```

---

## スライダー入力

最小値〜最大値の範囲をバーで表現するウィジェットです。  
ドラッグ入力と異なり**範囲が視覚的に見える**ため、0〜1 の正規化された値に向いています。

### SliderFloat — float のスライダー

```cpp
float metallic = mat->GetMetallic();
if (UI::SliderFloat("Metallic", metallic, 0.0f, 1.0f)) {
    mat->SetMetallic(metallic);
}
```

| 引数 | 型 | 説明 |
|---|---|---|
| `label` | `const char*` | 表示ラベル |
| `v` | `float&` | 編集する float |
| `min` | `float` | 最小値（必須） |
| `max` | `float` | 最大値（必須） |

### SliderInt — int のスライダー

```cpp
int quality = 4;
UI::SliderInt("Quality", quality, 1, 8);
```

---

## カラー入力

### ColorEdit — Vector4（RGBA）を編集する

```cpp
Vector4 color = mat->GetColor();
if (UI::ColorEdit("Color", color)) {
    mat->SetColor(color);
}
```

| 引数 | 型 | 説明 |
|---|---|---|
| `label` | `const char*` | 表示ラベル |
| `color` | `Vector4&` | 編集する RGBA カラー（各成分 0.0〜1.0） |
| `flags` | `ImGuiColorEditFlags` | オプション（省略可） |

### ColorEdit3 — Vector3（RGB）を編集する

`Vector3` でカラーを管理する場合（軌跡色・マーカー色など）に使います。

```cpp
Vector3& trajColor = viewportTrajectoryColor_;
if (UI::ColorEdit3("軌跡色", trajColor)) {
    // 変更された
}
```

| 引数 | 型 | 説明 |
|---|---|---|
| `label` | `const char*` | 表示ラベル |
| `color` | `Vector3&` | 編集する RGB カラー（各成分 0.0〜1.0） |
| `flags` | `ImGuiColorEditFlags` | オプション（省略可） |

---

## テキスト入力

### InputText — 文字列入力ボックス

```cpp
char buffer[256]{};
if (UI::InputText("ファイル名", buffer, sizeof(buffer))) {
    // テキストが変更された
}
```

| 引数 | 型 | 説明 |
|---|---|---|
| `label` | `const char*` | 表示ラベル |
| `buf` | `char*` | 入力バッファ（`char` 配列） |
| `buf_size` | `size_t` | バッファサイズ |
| `flags` | `ImGuiInputTextFlags` | `EnterReturnsTrue` などのオプション |

### InputTextWithHint — プレースホルダー付き入力ボックス

```cpp
char search[256]{};
UI::InputTextWithHint("##Search", "シーンを検索...", search, sizeof(search));
```

---

## 実用パターン

### 変更フラグのまとめ方

```cpp
bool changed = false;
changed |= UI::DragVec3("Position", pos, 0.1f);
changed |= UI::DragVec3("Rotation", rot, 0.01f);
changed |= UI::DragVec3("Scale",    scl, 0.01f);

if (changed) {
    // いずれかが変更されたときだけ処理
}
```

### Undo/Redo との組み合わせ

```cpp
changed |= UI::DragVec3("Position", pos, 0.1f);

// 編集開始時にスナップショットを保存
if (ImGui::IsItemActivated()) {
    snapPos = pos;
}
// 編集確定時に Undo 履歴に登録
if (ImGui::IsItemDeactivatedAfterEdit()) {
    undoHistory.Push(snapPos, pos);
}
```

### DisabledScope との組み合わせ

```cpp
{
    // マップが有効な場合はスライダーを無効化
    UI::Scope::DisabledScope ds(mat->IsMetallicMapEnabled());
    UI::SliderFloat("Metallic", metallic, 0.0f, 1.0f);
}
```
