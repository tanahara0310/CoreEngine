# 入力システム

CoreEngine の入力システムはキーボード・マウス・ゲームパッドに対応しています。  
すべての入力は `InputManager` が一元管理し、`InputQuery` を通じてアクセスします。

---

## アーキテクチャ概要

```
InputManager  ←  GetComponent<InputManager>() で取得
  └── InputQuery  ←  GetQuery() でアクセス
        ├── アクションベース API   IsActionPressed / Triggered / Released / GetAxisValue
        ├── 低レベル直接アクセス   IsKeyPressed / IsMouseButtonPressed / GetWheelDelta ...
        └── キーコンフィグ         GetConfig() / DetectAnyInput()
```

デバイスの詳細（`KeyboardInput` / `MouseInput` / `GamepadInput`）は `InputManager` が内部で管理します。  
ゲームロジックから直接デバイスクラスを取得する必要はありません。

---

## 基本的な取得方法

```cpp
#include "Input/InputManager.h"
#include "Input/InputAction.h"

auto* inputManager = engine_->GetComponent<CoreEngine::InputManager>();
auto& input = inputManager->GetQuery();
```

---

## アクションベース API（推奨）

ゲームロジックはアクション名で入力を問い合わせます。  
どのキーが割り当てられているかはキーコンフィグで変更できます。

### 入力チェック

```cpp
// アクションに対応するいずれかの入力が押されている間 true
if (input.IsActionPressed(InputAction::MoveForward)) { /* ... */ }

// 押した瞬間だけ true
if (input.IsActionTriggered(InputAction::Jump)) { /* ... */ }

// 離した瞬間だけ true
if (input.IsActionReleased(InputAction::Attack)) { /* ... */ }

// アナログ値（0.0 〜 1.0）
float speed = input.GetAxisValue(InputAction::MoveForward);
```

### InputAction 一覧

| アクション | デフォルトバインディング | 用途 |
|-----------|------------------------|------|
| `MoveForward` | W / ↑ / 左スティックY+ / 十字上 | 前進 |
| `MoveBack` | S / ↓ / 左スティックY- / 十字下 | 後退 |
| `MoveLeft` | A / ← / 左スティックX- / 十字左 | 左移動 |
| `MoveRight` | D / → / 左スティックX+ / 十字右 | 右移動 |
| `Jump` | Space / ゲームパッドA | ジャンプ |
| `Attack` | マウス左 / ゲームパッドX | 攻撃 |
| `Interact` | E / ゲームパッドB | インタラクト |
| `UIConfirm` | Enter / ゲームパッドA | UI決定 |
| `UICancel` | Escape / ゲームパッドB | UIキャンセル |
| `EditorGizmoTranslate` | W | エディタ：移動ギズモ |
| `EditorGizmoRotate` | E | エディタ：回転ギズモ |
| `EditorGizmoScale` | R | エディタ：拡縮ギズモ |

---

## 低レベル直接アクセス

エディタカメラ操作など、アクションに抽象化しにくい処理に使います。

### キーボード

```cpp
if (input.IsKeyPressed(DIK_LSHIFT))   { /* Shift 押し続け */ }
if (input.IsKeyTriggered(DIK_F1))     { /* F1 押した瞬間 */ }
if (input.IsKeyReleased(DIK_ESCAPE))  { /* Escape 離した瞬間 */ }
```

#### よく使うキー定数

| 定数 | キー | | 定数 | キー |
|------|-----|-|------|------|
| `DIK_W` | W | | `DIK_SPACE` | Space |
| `DIK_A` | A | | `DIK_RETURN` | Enter |
| `DIK_S` | S | | `DIK_ESCAPE` | Escape |
| `DIK_D` | D | | `DIK_LSHIFT` | 左Shift |
| `DIK_UP` | ↑ | | `DIK_LCONTROL` | 左Ctrl |
| `DIK_DOWN` | ↓ | | `DIK_TAB` | Tab |
| `DIK_LEFT` | ← | | `DIK_F1` 〜 `DIK_F12` | Fキー |
| `DIK_RIGHT` | → | | `DIK_1` 〜 `DIK_0` | 数字キー |

### マウス

```cpp
using CoreEngine::MouseButton;

// ボタン入力
if (input.IsMouseButtonPressed(MouseButton::Middle))   { /* 中ボタン押し続け */ }
if (input.IsMouseButtonTriggered(MouseButton::Left))   { /* 左ボタン押した瞬間 */ }
if (input.IsMouseButtonReleased(MouseButton::Right))   { /* 右ボタン離した瞬間 */ }

// 移動量・ホイール
int dx    = input.GetMouseDragX();    // 前フレームからの X 移動量
int dy    = input.GetMouseDragY();    // 前フレームからの Y 移動量
int wheel = input.GetWheelDelta();    // ホイール回転量

// カーソル位置（クライアント座標）
POINT pos = input.GetCursorPosition();
```

#### MouseButton 列挙型

| 値 | 説明 |
|-----|------|
| `MouseButton::Left` | 左ボタン |
| `MouseButton::Right` | 右ボタン |
| `MouseButton::Middle` | 中央ボタン |
| `MouseButton::XButton1` | X ボタン 1 |
| `MouseButton::XButton2` | X ボタン 2 |

### ゲームパッド

```cpp
if (input.IsGamepadConnected()) {
    CoreEngine::Stick ls = input.GetLeftStick();   // x, y : -1.0 〜 1.0
    CoreEngine::Stick rs = input.GetRightStick();
    float lt = input.GetLeftTrigger();             // 0.0 〜 1.0
    float rt = input.GetRightTrigger();
}
```

---

## キーコンフィグ

### ランタイム変更

```cpp
auto& config = input.GetConfig();

// バインディングを一括で上書き
config.SetBindings(InputAction::Jump, {
    InputBinding::FromKey(DIK_LSHIFT),
    InputBinding::FromGamepadButton(GamepadButton::A),
});

// 1件追加
config.AddBinding(InputAction::Jump, InputBinding::FromMouseButton(MouseButton::Right));

// 特定アクションのバインディングをすべて削除
config.ClearBindings(InputAction::Jump);

// デフォルトに戻す
config.ResetToDefault();
```

### JSON 保存・読み込み

```cpp
// 保存（Application/Assets/Config/ フォルダに keybindings.json を出力）
config.SaveToFile("Application/Assets/Config/keybindings.json");

// 読み込み（ファイルが存在しない場合は何もしない）
config.LoadFromFile("Application/Assets/Config/keybindings.json");
```

保存される JSON の形式:

```json
{
    "bindings": {
        "MoveForward": ["Key:W", "Key:Up", "Axis:LeftStickY+", "Gamepad:DPadUp"],
        "Jump":        ["Key:Space", "Gamepad:A"],
        "Attack":      ["Mouse:Left", "Gamepad:X"]
    }
}
```

### InputBinding ファクトリ関数

| 関数 | 説明 | 例 |
|------|------|----|
| `InputBinding::FromKey(dikCode)` | DIK_* キー | `FromKey(DIK_W)` |
| `InputBinding::FromMouseButton(button)` | マウスボタン | `FromMouseButton(MouseButton::Left)` |
| `InputBinding::FromGamepadButton(button)` | パッドボタン | `FromGamepadButton(GamepadButton::A)` |
| `InputBinding::FromGamepadAxis(axis, positive)` | アナログ軸 | `FromGamepadAxis(GamepadAxis::LeftStickY, true)` |

#### GamepadAxis 列挙型

| 値 | 説明 |
|----|------|
| `GamepadAxis::LeftStickX` | 左スティック X 軸 |
| `GamepadAxis::LeftStickY` | 左スティック Y 軸 |
| `GamepadAxis::RightStickX` | 右スティック X 軸 |
| `GamepadAxis::RightStickY` | 右スティック Y 軸 |
| `GamepadAxis::LeftTrigger` | 左トリガー |
| `GamepadAxis::RightTrigger` | 右トリガー |

### エディタ上でのキーコンフィグ

デバッグビルド（`USE_IMGUI` 有効時）では  
**Engine メニュー → Key Config** からキーコンフィグウィンドウを開けます。

- バインディングボタンをクリック → 入力待ち状態になる → 任意のキー / ボタンで再バインド
- `+` ボタン：バインディング追加 / `-` ボタン：末尾のバインディング削除
- `Save` / `Load` ：設定ファイルの保存・読み込み
- `Reset to Default` ：デフォルトバインディングに戻す

---

## 使用例

### キャラクター移動

```cpp
#include "Input/InputManager.h"
#include "Input/InputAction.h"

void PlayerObject::OnUpdate() {
    auto* inputManager = engine_->GetComponent<CoreEngine::InputManager>();
    if (!inputManager) return;

    auto& input = inputManager->GetQuery();
    const float speed = 5.0f * deltaTime;
    CoreEngine::Vector3 move = { 0.0f, 0.0f, 0.0f };

    move.z += input.GetAxisValue(InputAction::MoveForward) * speed;
    move.z -= input.GetAxisValue(InputAction::MoveBack)    * speed;
    move.x -= input.GetAxisValue(InputAction::MoveLeft)    * speed;
    move.x += input.GetAxisValue(InputAction::MoveRight)   * speed;

    transform_.translate = transform_.translate + move;

    if (input.IsActionTriggered(InputAction::Jump)) {
        Jump();
    }
}
```

### 起動時にキーコンフィグを読み込む

```cpp
void MyScene::OnInitialize() {
    auto* inputManager = engine_->GetComponent<CoreEngine::InputManager>();
    if (!inputManager) return;

    auto& config = inputManager->GetQuery().GetConfig();

    // ファイルがなければデフォルト設定のまま
    config.LoadFromFile("Application/Assets/Config/keybindings.json");
}
```

