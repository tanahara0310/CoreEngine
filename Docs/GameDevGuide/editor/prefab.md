# プレハブ

**プレハブは「オブジェクトの設計図」です。** 一度作れば、シーンにいくつでも置けます。

## 作る

ヒエラルキーのオブジェクトを **Project ビューへドラッグ**します。`.prefab` ファイルができます。

## 置く

Project ビューのプレハブを **Game ビューへドラッグ**します。

## 直す

置いたものを変えた後、インスペクタの **⋮** から選べます。

| 項目 | 何をするか |
|---|---|
| **プレハブへ適用** | この場での変更を**元のプレハブに書き戻す**（他の置いたものにも反映される） |
| **プレハブとのつながりを外す** | 以後は独立したオブジェクトになる |
| **このオブジェクトだけ保存** | シーンの中のこの 1 個だけを保存する |

## スクリプトから出す

```cpp
[Asset("Prefab")] [Tooltip("出す弾")]
string bulletPrefab = "";

void Fire()
{
    GameObject@ bullet = owner.InstantiatePrefab(bulletPrefab, "Bullet");   // (1)
    if (bullet is null) { return; }

    bullet.transform.position = owner.transform.position;

    ShooterBullet@ script;
    if (bullet.GetComponent(@script)) {   // (2)
        script.velocity = Vector3(0.0f, 0.0f, 20.0f);
    }
}
```

1. 第 2 引数は、できたオブジェクトに付ける名前です。
2. 出した直後に値を入れたいときは、こうしてスクリプトを取ります。

`[Asset("Prefab")]` を付けておくと、インスペクタでプレハブを選べます。パスを手で打たずに済みます。

## 中身

プレハブは JSON です。コンポーネントの並びがそのまま入っています。

```json
{
    "components": [
        { "type": "Transform",  "parameters": { "translate": [0.0, 5.0, 0.0] } },
        { "type": "MeshRenderer", "parameters": { "model": { "guid": "…", "path": "…" } } },
        { "type": "Material",   "parameters": { "color": [0.3, 0.6, 0.95, 1.0] } },
        { "type": "Collider",   "parameters": { "shapes": [ { "type": "Sphere", "radius": 1.0 } ] } },
        { "type": "Rigidbody",  "parameters": { "mass": 1.0, "useGravity": true } }
    ]
}
```

テキストなので、**差分が読めますし、マージもできます。** 複数人で作っていても衝突を解決できます。

## 見本

`Application/Assets/Prefabs/Physics/` に物理のプレハブが並んでいます。

| プレハブ | 中身 |
|---|---|
| `PhysicsBall` | 球 + 球コライダー + 剛体 + 物理マテリアル |
| `PhysicsBox` | 箱 + 箱コライダー + 剛体 |
| `PhysicsCapsule` | カプセル |
| `PhysicsPlatform` | 板（静的） |
| `Character` | 当たりと操作を持つキャラ |
| `HumanModel` | 見た目の人型 |

作りたいものに近いものを開いて、中身を真似るのが早道です。

## 弾を撃つときの注意

毎フレーム `InstantiatePrefab` を呼ぶと、オブジェクトがどんどん増えます。**必ず消す側も書いてください。**

```cpp
private float age_ = 0.0f;

void Update()
{
    age_ += Time::DeltaTime();
    if (age_ >= lifetime) {
        owner.Destroy();   // 時間で消す
    }
}

void OnTriggerEnter(Collision@ other)
{
    if (other.layer == CollisionLayer::Enemy) {
        owner.Destroy();   // 当たって消す
    }
}
```

**「当たって消す」だけでは、外れた弾が永遠に飛び続けます。** 必ず時間でも消してください。
