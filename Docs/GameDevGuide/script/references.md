# 他のものを触る

## 自分に付いているコンポーネント

**型名の先頭を小文字にした名前**で引けます。

```cpp
Rigidbody@ body = owner.rigidbody;
CharacterController@ controller = owner.characterController;
MeshRenderer@ mesh = owner.meshRenderer;
UIText@ text = owner.uiText;
AudioSource@ sound = owner.audioSource;
```

`UISlider` のように大文字が続く型は `owner.uiSlider` になります。

!!! warning "取れないことがあります"
    そのコンポーネントが付いていなければ `null` です。**必ず確かめてください。**

    ```cpp
    Rigidbody@ body = owner.rigidbody;
    if (body is null) { return; }
    ```

引くのには少し手間がかかるので、**`Start` で控えて使い回します。**

```cpp
private Rigidbody@ body_;

void Start()
{
    @body_ = owner.rigidbody;   // (1)
}

void FixedUpdate()
{
    if (body_ is null) { return; }
    body_.AddForce(Vector3(0.0f, 0.0f, 10.0f));
}
```

1. ハンドルへの代入は `@` を付けます。`body_ = ...` と書くと**中身のコピー**になってしまいます。

## 自分のスクリプト

同じオブジェクトに付いた別のスクリプトは `GetComponent` で取ります。

```cpp
private Health@ health_;

void Start()
{
    owner.GetComponent(@health_);
}
```

## 他のオブジェクト

### 1. インスペクタで繋ぐ（おすすめ）

```cpp
[ObjectRef] [Tooltip("追いかける相手")]
GameObject@ target;

void Update()
{
    if (target is null) { return; }
    Vector3 to = target.transform.position - owner.transform.position;
}
```

**名前に依存しません。繋ぎ忘れもインスペクタで見えます。**

### 2. 名前で探す

```cpp
GameObject@ player = owner.FindObject("Player");
```

1 個しかないマネージャのようなものに向きます。**名前を変えると壊れるので、多用しないでください。**

### 3. 当たった相手から

```cpp
void OnTriggerEnter(Collision@ other)
{
    GameObject@ hit = other.gameObject;
}
```

## 他のオブジェクトのスクリプト

```cpp
private ScoreKeeper@ keeper_;

void Start()
{
    GameObject@ manager = owner.FindObject("GameManager");
    if (manager !is null) {
        manager.GetComponent(@keeper_);
    }
}

void Collect()
{
    if (keeper_ !is null) {
        keeper_.AddOne();
    }
}
```

インスペクタで直接繋ぐこともできます。

```cpp
[ObjectRef] [Tooltip("スコア")]
ScoreKeeper@ keeper;
```

## 親子

```cpp
// 親にする
owner.transform.SetParent(target.transform);

// 親から外す
owner.transform.SetParent(null);

// 見た目の位置（親の分を含んだ位置）
Vector3 world = owner.transform.worldPosition;
```

| もの | 意味 |
|---|---|
| `position` | **親からの相対**位置 |
| `worldPosition` | **世界の中での**位置（読むだけ） |

親が無ければ 2 つは同じです。

## オブジェクトを作る・消す

```cpp
// プレハブから作る
GameObject@ made = owner.InstantiatePrefab(prefabPath, "Bullet");

// UI を作る
UIText@ label = owner.SpawnUIText("", "スコア: 0", "ScoreLabel");
UIImage@ image = owner.SpawnUIImage(texturePath, "Icon");

// 消す
owner.Destroy();        // 自分ごと
made.Destroy();         // 他のもの
```

!!! warning "消した後のハンドルは触らないこと"
    `Destroy()` を呼んだ後は `isAlive` が `false` になります。持ち続けるなら確かめてください。

    ```cpp
    if (target !is null && target.isAlive) {
        target.transform.position = want;
    }
    ```

## ありがちな間違い

```cpp
// 悪い：Awake では他がまだ揃っていない
void Awake()
{
    @body_ = owner.rigidbody;   // null になることがある
}

// 良い
void Start()
{
    @body_ = owner.rigidbody;
}
```

```cpp
// 悪い：毎フレーム引き直す
void Update()
{
    owner.rigidbody.AddForce(force);
}

// 良い：Start で控える
void Start() { @body_ = owner.rigidbody; }
void FixedUpdate() { if (body_ !is null) { body_.AddForce(force); } }
```

```cpp
// 悪い：@ を忘れると中身のコピーになる
body_ = owner.rigidbody;

// 良い
@body_ = owner.rigidbody;
```
