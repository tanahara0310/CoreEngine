# UI::Widgets::ToggleSwitch — トグルスイッチ

`#include "Utility/Debug/ImGui/ImGuiAll.h"`  
名前空間: `CoreEngine::UI::Widgets`

---

## 概要

ImGui 標準の `Checkbox` をスライダー型のトグルスイッチに置き換えたカスタムウィジェットです。  
ON/OFF 切り替え時にノブがスムーズにアニメーションします。

**外観:**
```
OFF: [●        ]  Label
ON:  [        ●]  Label   ← 緑色のトラック
```

---

## 関数シグネチャ

```cpp
bool UI::Widgets::ToggleSwitch(const char* label, bool* v);
```

| 引数 | 型 | 説明 |
|---|---|---|
| `label` | `const char*` | ラベル文字列。`"##"` 始まりの場合は非表示 |
| `v` | `bool*` | トグル状態へのポインタ |
| **戻り値** | `bool` | クリックされた（状態が変化した）場合 `true` |

---

## 基本的な使い方

### シンプルなトグル

```cpp
bool enableLighting = mat->IsLightingEnabled();
if (UI::Widgets::ToggleSwitch("Enable Lighting", &enableLighting)) {
    mat->SetLightingEnabled(enableLighting);
}
```

### 右に補足テキストを表示する

```cpp
bool enabled = mat->IsLightingEnabled();
if (UI::Widgets::ToggleSwitch("Enable Lighting (PBR)", &enabled)) {
    mat->SetLightingEnabled(enabled);
}
UI::SameLine();
UI::Hint(enabled ? "PBR" : "Unlit");
```

### ラベルを非表示にする（`"##"` プレフィックス）

```cpp
// "##active" → ラベルなし、ID は "active"
if (UI::Widgets::ToggleSwitch("##active", &isActive)) {
    SetActive(isActive);
}
ImGui::SameLine();
ImGui::Text("Active");
```

---

## DisabledScope との組み合わせ

`DisabledScope` 内に配置すると、グレーアウトされて操作できなくなります。  
アニメーションも自動的に透過されます。

```cpp
{
    UI::Scope::DisabledScope ds(!hasTexture);   // テクスチャがない場合は無効
    bool val = mat->IsNormalMapEnabled();
    if (UI::Widgets::ToggleSwitch("Normal Map", &val)) {
        mat->SetNormalMapEnabled(val);
    }
}
// DisabledScope の外はグレーアウトされない
if (!hasTexture) {
    UI::SameLine();
    UI::Hint("(なし)");
}
```

---

## アニメーションの仕組み

`ImGui::GetStateStorage()` を使い、ウィジェット ID ごとにアニメーション値 (`anim_t`) をフレームをまたいで保持します。

```
状態: OFF (anim_t = 0.0)  →  ON (anim_t = 1.0)
      ノブ左端                  ノブ右端
      グレーのトラック           緑のトラック
```

毎フレーム次の式で補間が進みます:

```
anim_t += (目標値 - anim_t) × ImSaturate(DeltaTime × 10.0)
```

- **フレームレート非依存**: `DeltaTime` を使うため 30fps でも 144fps でも同じ速さに見えます
- **速度調整**: 係数 `10.0` を変更すると速さが変わります（大きいほど速い）

---

## 視覚仕様

| 要素 | OFF | ON |
|---|---|---|
| トラック色 | `rgb(70, 70, 75)` ダークグレー | `rgb(34, 197, 94)` グリーン |
| ホバー時 | `rgb(95, 95, 100)` | `rgb(52, 215, 105)` |
| ノブ | 白い円 | 白い円（右端） |
| ノブ影 | 1px 下に半透明の黒円 | 同左 |
| 枠線 | 薄いグレー | 薄いグリーン |
| Alpha（Disabled 時） | `style.Alpha` を自動適用 | 同左 |

---

## 注意事項

- `label` が同じウィジェットを複数描画すると ID が衝突します。  
  `"##suffix"` で一意になるよう区別してください。

```cpp
// ❌ ID 衝突（同じラベルを2回使う）
UI::Widgets::ToggleSwitch("Enable", &flagA);
UI::Widgets::ToggleSwitch("Enable", &flagB);

// ✅ ## で一意にする
UI::Widgets::ToggleSwitch("Enable##A", &flagA);
UI::Widgets::ToggleSwitch("Enable##B", &flagB);
```

- `ImGui::PushID` / `PopID` によるスコープ分離も有効です。

```cpp
ImGui::PushID("SectionA");
UI::Widgets::ToggleSwitch("Enable", &flagA);
ImGui::PopID();

ImGui::PushID("SectionB");
UI::Widgets::ToggleSwitch("Enable", &flagB);
ImGui::PopID();
```
