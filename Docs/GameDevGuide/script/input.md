# 入力

## 操作の名前で書く

```cpp
if (Input::IsActionTriggered(InputAction::Jump)) {
    Jump();
}
```

**キーコードを直接書かないでください。** 名前で書くと、

- キーボードもパッドも**同時に効きます**
- 割り当ては **Help → キー操作を見る**から変えられます（スクリプトは触りません）
- **UI を操作している間はゲームの操作が止まります**（二重発火が起きません）

<div class="figure" markdown="0">
--8<-- "assets/figures/input-flow.svg"
</div>

## 3 つの聞き方

| 関数 | いつ true | 使いどころ |
|---|---|---|
| `IsActionPressed` | 押している間ずっと | 移動・ダッシュ・溜め |
| `IsActionTriggered` | **押した瞬間だけ** | ジャンプ・攻撃・決定 |
| `IsActionReleased` | **離した瞬間だけ** | 溜め撃ちを放つ |

```cpp
// 移動は「押している間」
if (Input::IsActionPressed(InputAction::MoveForward)) { … }

// ジャンプは「押した瞬間」。Pressed にすると跳び続けます
if (Input::IsActionTriggered(InputAction::Jump)) { … }

// 溜め撃ち
if (Input::IsActionPressed(InputAction::Attack)) { charge_ += Time::DeltaTime(); }
if (Input::IsActionReleased(InputAction::Attack)) { Fire(charge_); charge_ = 0.0f; }
```

## 軸でまとめて取る

反対向きの 2 つ（または 4 つ）をまとめて、`-1〜1` の値で取れます。**キーでもスティックでも同じ値**になります。

```cpp
// 左右だけ
float x = Input::GetAxis(InputAction::MoveLeft, InputAction::MoveRight);

// 前後左右をまとめて
Vector2 stick = Input::GetAxis2D(
    InputAction::MoveLeft, InputAction::MoveRight,
    InputAction::MoveBack, InputAction::MoveForward);

Vector3 move = Vector3(stick.x, 0.0f, stick.y) * speed;
```

スティックは倒した量に応じて途中の値になります。キーは `-1` か `0` か `1` です。

## ゲームパッド

**最大 4 台**まで見ます。プレイヤー番号は `0`〜`3` です。

```cpp
// 誰が押しても反応する（既定）
Input::IsActionPressed(InputAction::Jump);

// 1 人目だけ
Input::IsActionPressed(InputAction::Jump, 0);
```

!!! info "プレイヤーを指定すると、キーボードとマウスは混ざりません"
    `player` を省くと「誰か 1 人でも押していれば」になります。**番号を指定すると、そのパッドだけ**を見ます。2 人プレイを作るときはこうします。

### スティックとトリガーを直接読む

```cpp
Vector2 left  = Input::GetLeftStick(0);
Vector2 right = Input::GetRightStick(0);
float trigger = Input::GetRightTrigger(0);

if (Input::IsGamepadConnected(0)) { … }
int count = Input::GetConnectedGamepadCount();
```

### 振動させる

```cpp
Input::SetVibration(0.6f, 0.6f, 0);   // 左, 右, プレイヤー番号
```

止めるのは自分の仕事です。時間を数えて `0, 0` を入れてください。

```cpp
private float shakeTimer_ = 0.0f;

void Hit()
{
    Input::SetVibration(0.6f, 0.6f, 0);
    shakeTimer_ = 0.2f;
}

void Update()
{
    if (shakeTimer_ > 0.0f) {
        shakeTimer_ -= Time::DeltaTime();
        if (shakeTimer_ <= 0.0f) {
            Input::SetVibration(0.0f, 0.0f, 0);
        }
    }
}
```

## マウス

```cpp
if (Input::IsMouseButtonTriggered(MouseButton::Left)) { … }

Vector2 delta = Input::GetMouseDelta();   // 前フレームからの動き
float wheel = Input::GetWheelDelta();

if (Input::IsPointerOverGame()) { … }     // ゲーム画面の上にいるか
```

視点操作に使うなら `GetMouseDelta` です。画面の端で止まらないので回し続けられます。

```cpp
void Update()
{
    Vector2 look = Input::GetMouseDelta();
    yaw_ += look.x * sensitivity;
    pitch_ += look.y * sensitivity;
}
```

## キーを直接読む

キーコンフィグの対象にしたくないデバッグ用の操作などに使います。

```cpp
if (Input::IsKeyTriggered(Key::F1)) { debugMode_ = !debugMode_; }
```

!!! warning "ゲームの操作にこれを使わないでください"
    パッドが効かなくなり、キー割り当ても変えられなくなります。

## 動かないときに見るところ

| 症状 | 原因 |
|---|---|
| 何も反応しない | **停止中です。** ▶ を押してください |
| ウィンドウを切り替えたら動きっぱなしになった | 前面でないと入力は止まります（押しっぱなし扱いにはなりません） |
| ボタンを押したらキャラも動いた | UI にフォーカスが残っています。`UI::ClearFocus()` を呼んでください |
| パッドが効かない | 接続を `Input::IsGamepadConnected(0)` で確かめてください |
| ジャンプが連続する | `IsActionPressed` を `IsActionTriggered` に変えてください |

→ [入力アクション](../reference/input-actions.md)
