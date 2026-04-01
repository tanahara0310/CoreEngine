#pragma once

#ifdef _DEBUG

// ── ラッパー群 ──────────────────────────────────────────────────────────
#include "Wrappers/ImGuiScope.h"    // RAII スコープ（TreeScope, DisabledScope, ...）
#include "Wrappers/ImGuiInput.h"    // 入力ウィジェット（DragVec3, SliderFloat, ...）
#include "Wrappers/ImGuiLayout.h"   // レイアウト・表示（SectionHeader, Hint, ...）

// ── カスタムウィジェット群 ────────────────────────────────────────────────
#include "Widgets/ImGuiWidgets.h"   // ToggleSwitch など

// ── 名前空間エイリアス ──────────────────────────────────────────────────
// 各ラッパーは CoreEngine::UI 名前空間に属する。
// 使用側ファイルが namespace CoreEngine 内にあれば省略して以下のように書ける:
//
//   UI::DragVec3("Pos", pos);
//   UI::Scope::TreeScope ts("Transform");
//   UI::Widgets::ToggleSwitch("Active", &flag);
//

#endif // _DEBUG
