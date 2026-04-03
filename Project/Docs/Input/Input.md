# 入力システム

CoreEngine の入力システムはキーボード、マウス、ゲームパッドに対応しています。

## ヘッダ

```cpp
#include "Input/KeyboardInput.h"
#include "Input/MouseInput.h"
#include "Input/GamePadInput.h"
```

---

## KeyboardInput

DirectInput を使用したキーボード入力クラスです。

### 取得方法

```cpp
auto keyboard = engine_->GetComponent<CoreEngine::KeyboardInput>();
```

### 入力チェック

```cpp
// キーが押され続けているか
if (keyboard->IsKeyPressed(DIK_W)) {
    // W キーが押されている間、毎フレーム true
}

// キーが押された瞬間（トリガー）
if (keyboard->IsKeyTriggered(DIK_SPACE)) {
    // SPACE キーが押された最初のフレームだけ true
}

// キーが離された瞬間
if (keyboard->IsKeyReleased(DIK_ESCAPE)) {
    // ESCAPE キーが離されたフレームだけ true
}
```

### よく使うキー定数

| 定数 | キー | | 定数 | キー |
|------|-----|-|------|------|
| `DIK_W` | W | | `DIK_SPACE` | Space |
| `DIK_A` | A | | `DIK_RETURN` | Enter |
| `DIK_S` | S | | `DIK_ESCAPE` | Escape |
| `DIK_D` | D | | `DIK_LSHIFT` | 左Shift |
| `DIK_UP` | ↑ | | `DIK_LCONTROL` | 左Ctrl |
| `DIK_DOWN` | ↓ | | `DIK_TAB` | Tab |
| `DIK_LEFT` | ← | | `DIK_1` ～ `DIK_0` | 数字キー |
| `DIK_RIGHT` | → | | `DIK_F1` ～ `DIK_F12` | ファンクションキー |

### メソッド一覧

| メソッド | 説明 |
|---------|------|
| `IsKeyPressed(keyNumber)` | キーが押されているか |
| `IsKeyTriggered(keyNumber)` | キーが押された瞬間か |
| `IsKeyReleased(keyNumber)` | キーが離された瞬間か |

---

## MouseInput

DirectInput を使用したマウス入力クラスです。

### 取得方法

```cpp
auto mouse = engine_->GetComponent<CoreEngine::MouseInput>();
```

### ボタン入力

```cpp
using CoreEngine::MouseButton;

// ボタンが押されている
if (mouse->IsButtonPressed(MouseButton::Left)) { /* ... */ }

// ボタンが押された瞬間
if (mouse->IsButtonTriggered(MouseButton::Right)) { /* ... */ }

// ボタンが離された瞬間
if (mouse->IsButtonReleased(MouseButton::Middle)) { /* ... */ }
```

### マウスの移動とホイール

```cpp
// マウスの移動量（ドラッグ量）
int dx = mouse->GetDragX();
int dy = mouse->GetDragY();

// ホイール回転量
int wheel = mouse->GetWheelDelta();

// カーソル位置（スクリーン座標）
POINT pos = mouse->GetCursorPosition();
```

### MouseButton 列挙型

| 値 | 説明 |
|-----|------|
| `MouseButton::Left` | 左ボタン |
| `MouseButton::Right` | 右ボタン |
| `MouseButton::Middle` | 中央ボタン |
| `MouseButton::XButton1` | X ボタン 1 |
| `MouseButton::XButton2` | X ボタン 2 |

### メソッド一覧

| メソッド | 説明 |
|---------|------|
| `IsButtonPressed(button)` | ボタンが押されているか |
| `IsButtonTriggered(button)` | ボタンが押された瞬間か |
| `IsButtonReleased(button)` | ボタンが離された瞬間か |
| `GetWheelDelta()` | ホイール回転量 |
| `GetDragX()` | X 方向のドラッグ量 |
| `GetDragY()` | Y 方向のドラッグ量 |
| `GetCursorPosition()` | カーソルのスクリーン座標 |

---

## 使用例：キャラクター移動

```cpp
void PlayerObject::OnUpdate() {
    auto keyboard = GetEngineSystem()->GetComponent<CoreEngine::KeyboardInput>();
    if (!keyboard) return;

    float speed = 5.0f;
    CoreEngine::Vector3 move = { 0.0f, 0.0f, 0.0f };

    if (keyboard->IsKeyPressed(DIK_W)) move.z += speed;
    if (keyboard->IsKeyPressed(DIK_S)) move.z -= speed;
    if (keyboard->IsKeyPressed(DIK_A)) move.x -= speed;
    if (keyboard->IsKeyPressed(DIK_D)) move.x += speed;

    transform_.translate = transform_.translate + move * deltaTime;
}
```
