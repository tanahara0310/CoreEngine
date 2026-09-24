# 3. 集めて数える

**この章でやること** — アイテムに触ると消えて、数が増えるようにします。**スクリプト同士でデータをやり取りします。**

**かかる時間** — 15 分ほど

---

## 当たり判定は 2 種類ある

<div class="figure" markdown="0">
--8<-- "assets/figures/collision-branch.svg"
</div>

| 使いたいもの | コライダーの設定 | 受け取る関数 |
|---|---|---|
| アイテム・ゴール・当たり判定エリア | **トリガーを入れる** | `OnTriggerEnter` |
| 壁・床・ぶつかる敵 | トリガーを外す | `OnCollisionEnter` |

## アイテムを作る

`GameObject → 空のオブジェクトを作成` で `Coin` を作り、

| コンポーネント | 設定 |
|---|---|
| `MeshRenderer` | 球や箱のモデル |
| `Material` | 金色（黄色っぽく、メタリック `0.9`） |
| `Collider` | **球**を 1 つ、半径 `0.6`、**トリガーにチェック** |

!!! warning "トリガーのチェックを忘れると押し出されます"
    チェックを入れないと、プレイヤーがコインに乗り上げます。

## 数える側を作る

`GameObject → 空のオブジェクトを作成` で `GameManager` を作り、スクリプト `ScoreKeeper` を付けます。

```cpp title="ScoreKeeper.as"
[DisplayName("スコア")]
class ScoreKeeper : ScriptComponent
{
    [ReadOnly] [Tooltip("集めた数")]
    int collected = 0;

    [Tooltip("全部集めたときに出す文字")]
    string clearMessage = "コンプリート！";

    [Tooltip("集める総数。0 なら開始時に数える")]
    int total = 0;

    void Start()
    {
        collected = 0;
    }

    // コイン側から呼ぶ
    void AddOne()
    {
        collected++;
        Log("集めた: " + collected + " / " + total);
        if (total > 0 && collected >= total) {
            Log(clearMessage);
        }
    }

    void Register()
    {
        total++;
    }
}
```

## コイン側を作る

スクリプト `Coin` を作って `Coin` オブジェクトに付けます。

```cpp title="Coin.as"
[DisplayName("コイン")]
class Coin : ScriptComponent
{
    [Tooltip("回る速さ（度/秒）")]
    float spinSpeed = 90.0f;

    private ScoreKeeper@ keeper_;

    void Start()
    {
        GameObject@ manager = owner.FindObject("GameManager");   // (1)
        if (manager !is null) {
            manager.GetComponent(@keeper_);   // (2)
            if (keeper_ !is null) {
                keeper_.Register();
            }
        }
    }

    void Update()
    {
        // くるくる回して目立たせる
        Vector3 rotation = owner.transform.rotation;
        rotation.y += spinSpeed * 0.0174533f * Time::DeltaTime();   // (3)
        owner.transform.rotation = rotation;
    }

    void OnTriggerEnter(Collision@ other)   // (4)
    {
        if (other.gameObject.name != "Player") { return; }   // (5)

        if (keeper_ !is null) {
            keeper_.AddOne();
        }
        owner.Destroy();
    }
}
```

1. 名前でシーン内のオブジェクトを探します。
2. **これが他のスクリプトを取る書き方です。** `@` を付けたハンドルを渡すと、その型のコンポーネントが入ります。
3. 回転はラジアンです。度から直すため `0.0174533`（π/180）を掛けています。
4. トリガーに入った瞬間に呼ばれます。`other` が相手です。
5. 相手を名前で絞っています。もっと確実にやるならレイヤーで絞ります（後述）。

## 動かす

1. `Coin` を <kbd>Ctrl</kbd>+<kbd>D</kbd> で何個か複製して、散らばらせる
2. ▶ で再生
3. プレイヤーでコインに触れると消えて、Console に「集めた: 1 / 5」と出る

## 名前で絞るよりレイヤーが確実

名前での判定は、オブジェクト名を変えると壊れます。**レイヤー**で絞ると確実です。

```cpp
void OnTriggerEnter(Collision@ other)
{
    if (other.layer != CollisionLayer::Player) { return; }
    // ...
}
```

レイヤーは `Collider` のインスペクタで形ごとに設定します。使えるレイヤーは `Application/Config/EngineSettings/Layers.json` で決まります。

**レイヤー同士がぶつかるかどうか**も設定できます（Project Settings の衝突マトリクス）。「弾は敵にだけ当たる」といった制御がここでできます。

## スクリプト同士をつなぐ 3 つの方法

| 方法 | 書き方 | いつ使うか |
|---|---|---|
| **名前で探す** | `owner.FindObject("名前")` | 1 個しかないもの（マネージャなど） |
| **インスペクタで繋ぐ** | `[ObjectRef] GameObject@ target;` | **繋ぎ先が決まっているとき（おすすめ）** |
| **当たった相手から** | `other.gameObject` | 触れたものに反応するとき |

`[ObjectRef]` を付けると、インスペクタに繋ぎ先を選ぶ欄が出ます。名前に依存しないので壊れにくく、繋ぎ忘れも見て分かります。

```cpp
[ObjectRef] [Tooltip("スコアを持っているオブジェクト")]
GameObject@ manager;
```

クラスのハンドルを直接繋ぐこともできます。

```cpp
[ObjectRef] [Tooltip("スコア")]
ScoreKeeper@ keeper;
```

---

## この章のまとめ

- **すり抜けて反応させたいなら `Collider` のトリガーにチェック**して `OnTriggerEnter`
- 他のスクリプトは **`GetComponent(@変数)`** で取る
- 相手を絞るのは名前よりも**レイヤー**
- 繋ぎ先が決まっているなら **`[ObjectRef]` でインスペクタから繋ぐ**

次は、集めた数を画面に出します。 → [4. 画面に出す](04-ui.md)
