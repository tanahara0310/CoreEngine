# 物理と当たり判定

## 3 つの動かし方

| 動かし方 | コンポーネント | いつ使うか |
|---|---|---|
| **物理に任せる** | `Rigidbody`（動的） | 落ちる・転がる・積み上がるもの |
| **自分で動かす（当たりは効く）** | `CharacterController` | 操作キャラ・敵 |
| **自分で動かす（当たりは無視）** | `Transform` だけ | 演出・カメラ・UI |

!!! warning "`Rigidbody` と `CharacterController` を同時に付けないこと"
    どちらも位置を動かすので、取り合いになります。

## Rigidbody

| 項目 | 意味 |
|---|---|
| 種別 | **動的**（重力と力で動く）/ **キネマティック**（速度を指定して動かす）/ **静的**（動かない） |
| 質量 | kg。キネマティックと静的では無限として扱う |
| 重力を受ける | 落ちるか |
| 抗力 | 大きいほど速度が早く落ちる（0 で減らさない） |
| 回転の抗力 | 大きいほど回転が早く止まる（転がり続けるのを防ぐ） |
| 回転を止める | 接触で回らなくなる（向きは自分で指定する） |

```cpp
private Rigidbody@ body_;

void Start()
{
    @body_ = owner.rigidbody;
}

void FixedUpdate()   // (1)
{
    if (body_ is null) { return; }
    body_.AddForce(Vector3(0.0f, 0.0f, 20.0f));
}

void Boost()
{
    body_.AddImpulse(Vector3(0.0f, 8.0f, 0.0f));   // (2)
}
```

1. **力は `FixedUpdate` で加えます。** `Update` だとフレームレートで結果が変わります。
2. ジャンプのように一瞬だけ加えるものは `AddImpulse` です。

| 操作 | 意味 |
|---|---|
| `AddForce` | 力を加える（**押し続ける**イメージ。毎フレーム呼ぶ） |
| `AddImpulse` | 撃力を加える（**一発叩く**イメージ。1 回だけ呼ぶ） |
| `AddTorque` | 回す力を加える |
| `WakeUp` | 眠っているものを起こす |

!!! info "止まったものは眠ります"
    ほとんど動かなくなった物体は計算から外れます（**眠る**）。他の物がぶつかれば自動で起きますが、スクリプトから力を加えるときは `WakeUp()` が要ることがあります。

    眠っている数は `Physics::GetSleepingCount()` で見られます。

## 動かない床・壁

**`Rigidbody` を付けません。** `Collider` の**静的**にチェックを入れるだけです。

付けてしまうと、床が落ちていきます。

## CharacterController

```cpp
private CharacterController@ controller_;

void Start()
{
    @controller_ = owner.characterController;
}

void Update()
{
    Vector2 stick = Input::GetAxis2D(
        InputAction::MoveLeft, InputAction::MoveRight,
        InputAction::MoveBack, InputAction::MoveForward);

    controller_.SimpleMove(Vector3(stick.x, 0.0f, stick.y) * speed);   // (1)

    if (controller_.grounded && Input::IsActionTriggered(InputAction::Jump)) {
        controller_.Jump(jumpSpeed);
    }
}
```

1. `SimpleMove` は**重力を自動で足します**。自分で扱いたいときは `Move` を使ってください。

| 項目 | 意味 |
|---|---|
| 登れる坂 | 度。これより急な坂では滑り落ちる |
| 越えられる段差 | m。これ以下の段差は歩いたまま上がる |
| 表面の余白 | m。壁との間に残す隙間 |
| `grounded` | 接地しているか（読むだけ） |
| `velocity` | 今の速度 |

## 当たり判定

### トリガーか、押し出すか

<div class="figure" markdown="0">
--8<-- "assets/figures/collision-branch.svg"
</div>

```cpp
// アイテム・ゴール・判定エリア
void OnTriggerEnter(Collision@ other)
{
    if (other.layer != CollisionLayer::Player) { return; }
    owner.Destroy();
}

// ぶつかる
void OnCollisionEnter(Collision@ collision)
{
    if (collision.impulse > 5.0f) {   // (1)
        PlayHitSound();
    }
}
```

1. **`impulse` はぶつかった強さ（N・s）です。** 強く当たったときだけ音を鳴らす、といった出し分けに使います。

### `Collision` から取れるもの

| もの | 意味 |
|---|---|
| `gameObject` | 相手のオブジェクト |
| `layer` | 相手のレイヤー |
| `selfLayer` | 自分のレイヤー |
| `normal` | ぶつかった面の向き |
| `point` | ぶつかった位置 |
| `impulse` | ぶつかった強さ |
| `depth` | めり込んだ深さ |
| `isTrigger` | トリガーだったか |

### レイヤーで絞る

名前で判定すると、名前を変えた瞬間に壊れます。**レイヤーで絞ってください。**

```cpp
if (other.layer == CollisionLayer::Enemy) { … }
```

レイヤー同士が当たるかどうかは、**Project Settings の衝突マトリクス**で決めます。「弾は敵にだけ当たる」を設定しておけば、スクリプトで判定する必要すらなくなります。

## 光線を飛ばす（Raycast）

```cpp
// 足元に床があるか
RaycastHit@ hit = Physics::Raycast(
    owner.transform.position,
    Vector3(0.0f, -1.0f, 0.0f),
    2.0f,
    Physics::LayerMask(CollisionLayer::Default));   // (1)

if (hit !is null) {
    Log("床まで " + hit.distance + " m");
}
```

1. レイヤーマスクを省くと全部に当たります。

| 関数 | 何をするか |
|---|---|
| `Raycast` | いちばん手前に当たったもの 1 つ |
| `RaycastAll` | 当たったもの全部 |
| `OverlapSphere` | 球の中にあるもの全部 |
| `OverlapBox` | 箱の中にあるもの全部 |

```cpp
// 爆発：半径 5m の中の敵にダメージ
GameObject@[]@ hits = Physics::OverlapSphere(
    owner.transform.position, 5.0f, Physics::LayerMask(CollisionLayer::Enemy));

for (uint i = 0; i < hits.length(); i++) {
    Health@ health;
    if (hits[i].GetComponent(@health)) {
        health.Damage(50);
    }
}
```

## 重力を変える

```cpp
Physics::gravity = Vector3(0.0f, -4.9f, 0.0f);   // 月面
```

## 当たらないときに見るところ

1. **ツールバーの `Collider` を押す。** コライダーの形が線で出ます。見た目とコライダーは別物です
2. **トリガーのチェック**を確かめる。トリガー同士・トリガーと非トリガーで呼ばれる関数が変わります
3. **衝突マトリクス**を確かめる。そのレイヤー同士が切られているかもしれません
4. **コンポーネントが無効になっていないか**確かめる。無効なスクリプトには通知が届きません
5. **どちらも `静的` になっていないか**確かめる。静的なものは押し出されないので、通知は来ても動きません
