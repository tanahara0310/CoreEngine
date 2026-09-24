# 2. キャラを動かす

**この章でやること** — キーボードとゲームパッドでキャラを歩かせ、跳ばせます。**初めてスクリプトを書きます。**

**かかる時間** — 15 分ほど

---

## キャラを置く

`GameObject → 空のオブジェクトを作成` で `Player` を作り、次を足します。

| コンポーネント | 設定 |
|---|---|
| `MeshRenderer` | カプセルか球のモデル |
| `Material` | 好きな色 |
| `Collider` | **球**を 1 つ、半径 `0.5` |
| `CharacterController` | 既定のまま |

!!! note "CharacterController と Rigidbody は一緒に使いません"
    `Rigidbody` は物理に任せて動かすもの、`CharacterController` は**自分で動かす**ものです。操作キャラには `CharacterController` を使います。

`CharacterController` の項目はこうなっています。

| 項目 | 既定 | 意味 |
|---|---|---|
| 登れる坂 | 45 度 | これより急な坂では滑り落ちる |
| 越えられる段差 | 0.3 m | これ以下の段差は歩いたまま上がる |
| 表面の余白 | 0.02 m | 壁との間に残す隙間 |
| 重力を受ける | ✓ | 接地していないと落ちる |

## スクリプトを作る

Project ビューで `Assets/Scripts` を開き、**＋ 作成 → スクリプトを作成…**。

- 雛形は **Basic**
- 名前は `PlayerMove`

!!! warning "ファイル名とクラス名は同じにしてください"
    `PlayerMove.as` の中のクラスは `PlayerMove` です。ずれていると警告が出ます。また**クラス名はプロジェクト全体で一意**である必要があります。

できたファイルを、こう書き換えます。

```cpp title="PlayerMove.as"
[DisplayName("プレイヤー操作")]
class PlayerMove : ScriptComponent
{
    [Range(1.0f, 12.0f)] [Tooltip("歩く速さ m/s")]
    float walkSpeed = 4.0f;

    [Range(2.0f, 20.0f)] [Tooltip("走る速さ m/s")]
    float runSpeed = 8.0f;

    [Range(2.0f, 15.0f)] [Tooltip("跳び上がる速さ m/s")]
    float jumpSpeed = 5.5f;

    private CharacterController@ controller_;

    void Start()
    {
        @controller_ = owner.characterController;   // (1)
    }

    void Update()
    {
        if (controller_ is null) { return; }

        // 前後左右をまとめて -1〜1 の値で受け取る
        Vector2 stick = Input::GetAxis2D(
            InputAction::MoveLeft, InputAction::MoveRight,
            InputAction::MoveBack, InputAction::MoveForward);   // (2)

        float speed = Input::IsActionPressed(InputAction::Sprint) ? runSpeed : walkSpeed;
        Vector3 move = Vector3(stick.x, 0.0f, stick.y) * speed;

        controller_.SimpleMove(move);   // (3)

        if (controller_.grounded && Input::IsActionTriggered(InputAction::Jump)) {
            controller_.Jump(jumpSpeed);   // (4)
        }
    }
}
```

1. 同じオブジェクトに付いている `CharacterController` を取ります。型名の先頭を小文字にした名前で引けます。
2. `GetAxis2D` は 4 つの操作から `Vector2` を作ります。キーボードでもスティックでも同じ値になります。
3. `SimpleMove` は重力を自動で足してくれます。自分で重力を扱いたいときは `Move` を使います。
4. `grounded` は接地しているかどうかです。空中で跳べないようにしています。

## 付けて動かす

1. `Player` を選ぶ
2. **＋ コンポーネント追加** → `PlayerMove`（作ったクラスが一覧に出ます）
3. ▶ で再生

<kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> で歩き、<kbd>Shift</kbd> で走り、<kbd>Space</kbd> で跳びます。**ゲームパッドでもそのまま動きます。**

## なぜキーを直接書かないのか

`Input::IsKeyPressed(Key::W)` と書くこともできますが、この書き方だと

- ゲームパッドが効かない
- キーを変えたくなったら**全部のスクリプトを直す**ことになる
- UI を操作している最中にもキャラが動いてしまう

**操作の名前（`InputAction`）で書く**と、これが全部解決します。

<div class="figure" markdown="0">
--8<-- "assets/figures/input-flow.svg"
</div>

割り当ては **Help → キー操作を見る**（Key Config）で変えられます。スクリプトは書き換えません。

## 使える操作の名前

| 名前 | 表示 | 既定の割り当て |
|---|---|---|
| `MoveForward` | 前進 | <kbd>W</kbd> <kbd>↑</kbd> 左スティック上 十字上 |
| `MoveBack` | 後退 | <kbd>S</kbd> <kbd>↓</kbd> 左スティック下 十字下 |
| `MoveLeft` | 左移動 | <kbd>A</kbd> <kbd>←</kbd> 左スティック左 十字左 |
| `MoveRight` | 右移動 | <kbd>D</kbd> <kbd>→</kbd> 左スティック右 十字右 |
| `Jump` | ジャンプ | <kbd>Space</kbd> A ボタン |
| `Sprint` | ダッシュ | <kbd>Shift</kbd> 左スティック押し込み |
| `Attack` | 攻撃 | マウス左 X ボタン |
| `Interact` | インタラクト | <kbd>E</kbd> B ボタン |

足りなければ Key Config で増やせます。→ [入力アクション](../reference/input-actions.md)

## 3 つの聞き方の違い

| 関数 | いつ true か | 使いどころ |
|---|---|---|
| `IsActionPressed` | **押している間ずっと** | 移動・ダッシュ |
| `IsActionTriggered` | **押した瞬間だけ** | ジャンプ・攻撃・決定 |
| `IsActionReleased` | **離した瞬間だけ** | 溜め撃ちを放つ |

ジャンプに `IsActionPressed` を使うと、押しっぱなしで跳び続けてしまいます。

---

## この章のまとめ

- 操作キャラは **`CharacterController`**（`Rigidbody` ではない）
- `SimpleMove` が重力を面倒みてくれる。`grounded` で接地判定
- **キーではなく操作の名前で書く**。パッドが自動で効き、後から割り当てを変えられる
- **押している間 / 押した瞬間 / 離した瞬間**を使い分ける

次は、アイテムを集めます。 → [3. 集めて数える](03-collect.md)
