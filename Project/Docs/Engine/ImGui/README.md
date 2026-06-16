# ImGui ラッパーシステム

ImGui の関数を直接呼び出す代わりに、エンジン独自のラッパーを通して使用するためのシステムです。  
英語の引数名を意識せずに使えること・開始/終了の呼び忘れを防ぐことを目的としています。

---

## ファイル構成

```
Engine/Src/Utility/Debug/ImGui/
│
├── ImGuiAll.h                   ← ★ここだけインクルードすれば全て使える
│
├── Wrappers/
│   ├── ImGuiScope.h             ← RAII スコープ（TreeScope, DisabledScope, ...）
│   ├── ImGuiInput.h             ← 入力ウィジェット（DragVec3, SliderFloat, ...）
│   └── ImGuiLayout.h            ← レイアウト・表示（SectionHeader, Hint, ...）
│
└── Widgets/
    └── ImGuiWidgets.h           ← カスタムウィジェット（ToggleSwitch, ...）
```

---

## 名前空間マップ

| 名前空間 | 内容 | ヘッダ |
|---|---|---|
| `CoreEngine::UI::` | 入力・レイアウト全般 | `ImGuiInput.h` / `ImGuiLayout.h` |
| `CoreEngine::UI::Scope::` | RAII スコープ群 | `ImGuiScope.h` |
| `CoreEngine::UI::Widgets::` | カスタムウィジェット | `Widgets/ImGuiWidgets.h` |

---

## 基本的な使い方

### インクルード

```cpp
#include "Utility/Debug/ImGui/ImGuiAll.h"
```

`ImGuiAll.h` 1 つをインクルードするだけで全ラッパーが使えます。

### 名前空間の省略

各 `.cpp` ファイルが `namespace CoreEngine` 内にある場合、`CoreEngine::` を省略できます。

```cpp
namespace CoreEngine {
    // UI::DragVec3(...)          → CoreEngine::UI::DragVec3(...)
    // UI::Scope::TreeScope(...)  → CoreEngine::UI::Scope::TreeScope(...)
    // UI::Widgets::ToggleSwitch  → CoreEngine::UI::Widgets::ToggleSwitch(...)
}
```

---

## Before / After 比較

### ツリーノード

```cpp
// ❌ Before: TreePop を手動で呼ぶ必要がある
if (ImGui::TreeNode("Transform")) {
    // ...
    ImGui::TreePop();  // 忘れるとクラッシュ
}

// ✅ After: スコープを抜けると自動で TreePop される
if (auto s = UI::Scope::TreeScope("Transform")) {
    // ...
}
```

### グレーアウト

```cpp
// ❌ Before
ImGui::BeginDisabled(!hasTexture);
// ...
ImGui::EndDisabled();  // 忘れるとUIが壊れる

// ✅ After: ブロックを抜けると自動で EndDisabled される
{
    UI::Scope::DisabledScope ds(!hasTexture);
    // ...
}
```

### Vector3 入力

```cpp
// ❌ Before: &pos.x というポインタ渡しが必要
ImGui::DragFloat3("Position", &pos.x, 0.1f);

// ✅ After: Vector3 をそのまま渡せる
UI::DragVec3("Position", pos, 0.1f);
```

### カラー入力

```cpp
// ❌ Before
ImGui::ColorEdit4("Color", &color.x);

// ✅ After: Vector4 をそのまま渡せる
UI::ColorEdit("Color", color);
```

---

## ドキュメント一覧

| ドキュメント | 内容 |
|---|---|
| [Wrappers/Scope.md](Wrappers/Scope.md) | RAII スコープの詳細 |
| [Wrappers/Input.md](Wrappers/Input.md) | 入力ウィジェットの詳細 |
| [Wrappers/Layout.md](Wrappers/Layout.md) | レイアウト・表示の詳細 |
| [Widgets/ToggleSwitch.md](Widgets/ToggleSwitch.md) | トグルスイッチウィジェットの詳細 |
