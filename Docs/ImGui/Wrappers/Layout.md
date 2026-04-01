# UI:: — レイアウト・表示

`#include "Utility/Debug/ImGui/ImGuiAll.h"`  
名前空間: `CoreEngine::UI`

---

## 概要

テキスト表示・セクション区切り・配置制御など、UI の見た目を整えるためのラッパーです。

---

## テキスト表示

### Label — 通常テキストを表示する

```cpp
UI::Label("ワールド座標:");
```

### Hint — グレーの補足テキストを表示する

補足説明や注意書きに使います。文字列リテラルのみ受け付けます。

```cpp
UI::Hint("(なし)");
UI::Hint("maps が無効のときのみ有効");
```

### HintF — フォーマット付きグレーテキスト

変数を含む補足テキストに使います（`printf` 形式）。

```cpp
UI::HintF("Objects: %zu", objects.size());
UI::HintF("(%d/%d)", undoCount, undoCount + redoCount);
UI::HintF("%s", statusMessage.c_str());
```

### SameLine との組み合わせ

```cpp
UI::Widgets::ToggleSwitch("Enable Lighting", &enabled);
UI::SameLine();
UI::Hint(enabled ? "PBR モード" : "Unlit モード");
```

---

## プログレスバー

### ProgressBar — 進捗バーを表示する

```cpp
// ウィンドウ幅いっぱいに表示
UI::ProgressBar(0.75f);

// サイズとテキストオーバーレイを指定
float progress = particle.age / particle.lifetime;
UI::ProgressBar(progress, ImVec2(-1, 0), "75%");
```

| 引数 | 型 | 説明 |
|---|---|---|
| `fraction` | `float` | 進捗割合（0.0f〜1.0f） |
| `size` | `ImVec2` | バーのサイズ（x=-FLT_MIN で幅いっぱい） |
| `overlay` | `const char*` | バー上に表示するテキスト（nullptr でなし） |

---

## セクション区切り

### SectionHeader — セクション見出し付き水平線

セクションを視覚的に分けるときに使います。

```cpp
UI::SectionHeader("Lighting");
// ...
UI::SectionHeader("PBR Parameters");
// ...
UI::SectionHeader("Effects");
```

**出力イメージ:**

```
──────── Lighting ────────
  ...
──────── PBR Parameters ────────
  ...
```

### Separator — 見出しなし水平線

```cpp
UI::Separator();
```

---

## レイアウト制御

### SameLine — 次のウィジェットを同じ行に配置する

```cpp
UI::Widgets::ToggleSwitch("Enable IBL", &iblEnabled);
UI::SameLine();
UI::Hint("(未設定)");
```

### オフセットを指定する

```cpp
// 行頭から 200px の位置に配置
UI::SameLine(200.0f);
UI::Label("値");
```

### Spacing — 縦方向の余白を追加する

```cpp
UI::Spacing();
UI::SectionHeader("次のセクション");
```

---

## ツールチップ

### Tooltip — ホバー時に説明を表示する

直前のウィジェットにカーソルを合わせると表示されます。

```cpp
UI::SliderFloat("Roughness", roughness, 0.0f, 1.0f);
UI::Tooltip("表面の粗さ。1.0 に近いほど拡散反射が強くなります。");
```

---

## 実用パターン

### 典型的なセクション構造

```cpp
if (auto s = UI::Scope::TreeScope("Material")) {

    UI::SectionHeader("Base");
    UI::ColorEdit("Color", color);

    UI::SectionHeader("PBR Parameters");
    UI::SliderFloat("Metallic",  metallic,  0.0f, 1.0f);
    UI::SliderFloat("Roughness", roughness, 0.0f, 1.0f);

    UI::SectionHeader("Effects");
    UI::Widgets::ToggleSwitch("Dithering", &ditheringEnabled);
}
```

### 無効理由の表示

```cpp
{
    UI::Scope::DisabledScope ds(!iblAvailable);
    UI::Widgets::ToggleSwitch("Enable IBL", &iblEnabled);
}
if (!iblAvailable) {
    UI::SameLine();
    UI::Hint("(Irradiance/Prefiltered/BRDF LUT 未設定)");
}
```

### 字下げしたサブ設定

```cpp
UI::Widgets::ToggleSwitch("Enable Dithering", &ditheringEnabled);
if (ditheringEnabled) {
    UI::Scope::IndentScope is;
    UI::SliderFloat("Scale##Dithering", ditheringScale, 0.1f, 5.0f);
    UI::Tooltip("ディザリングパターンのスケール");
}
```

---

## 関数一覧

| 関数 | ImGui 対応関数 | 説明 |
|---|---|---|
| `Label(text)` | `Text` | 通常テキスト |
| `Hint(text)` | `TextDisabled` | グレーの補足テキスト |
| `SectionHeader(label)` | `SeparatorText` | セクション区切り |
| `Separator()` | `Separator` | 水平線 |
| `SameLine(offset)` | `SameLine` | 同一行に続ける |
| `Spacing()` | `Spacing` | 縦余白 |
| `Tooltip(text)` | `SetItemTooltip` | ホバーツールチップ |
