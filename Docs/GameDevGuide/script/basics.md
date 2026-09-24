# 書き方の基本

## 形

スクリプトは **AngelScript** で書きます。C++ や C# に似た文法です。

```cpp title="Spinner.as"
[DisplayName("回転させる")]
class Spinner : ScriptComponent
{
    [Range(0.0f, 360.0f)] [Tooltip("回る速さ（度/秒）")]
    float speed = 90.0f;

    void Update()
    {
        Vector3 rotation = owner.transform.rotation;
        rotation.y += speed * 0.0174533f * Time::DeltaTime();
        owner.transform.rotation = rotation;
    }
}
```

決まりは 3 つだけです。

1. **`ScriptComponent` を継ぐ**
2. **ファイル名とクラス名を同じにする**
3. **クラス名はプロジェクト全体で一意にする**

これだけで、インスペクタの **＋ コンポーネント追加**の一覧に出ます。

## `#include` は要りません

`Assets/Scripts` の下の `.as` は**全部まとめて 1 つ**としてコンパイルされます。だから、

- **どのファイルのクラスでも、そのまま名前で使えます**
- `#include` を書いても読み飛ばされます
- そのかわり、**クラス名が重なるとコンパイルが全部止まります**

ファイル名とクラス名を揃えておけば、ファイル名は重複しないので衝突しません。

## 呼ばれる順番

<div class="figure" markdown="0">
--8<-- "assets/figures/lifecycle.svg"
</div>

| 関数 | いつ | 何を書くか |
|---|---|---|
| `Awake()` | 付いた直後 | **他のコンポーネントはまだ揃っていません。** 自分だけで済む初期化 |
| `Start()` | 最初の更新の前 | **他を探す初期化はここ。** 全オブジェクトが揃っています |
| `Update()` | 毎フレーム | ふつうの処理 |
| `FixedUpdate()` | 物理の刻みごと | **力を加える処理。** フレームによって 0 回にも複数回にもなります |
| `LateUpdate()` | 全員の `Update` の後 | **カメラの追従。** 対象が動き終わってから |
| `OnDestroy()` | 壊すとき | 後片付け |

!!! warning "`Awake` で他のコンポーネントを探さないこと"
    `Awake` の時点では、自分が最初の 1 個かもしれません。`owner.rigidbody` が `null` になります。**他を触るのは `Start` からです。**

### `Update` と `FixedUpdate` の使い分け

```cpp
// 悪い例：フレームレートで挙動が変わる
void Update()
{
    body.AddForce(Vector3(0.0f, 0.0f, 10.0f));
}

// 良い例：刻みが一定なので、どんな環境でも同じ結果
void FixedUpdate()
{
    body.AddForce(Vector3(0.0f, 0.0f, 10.0f));
}
```

**物理に力を加えるなら `FixedUpdate`**、入力を読むなら `Update` です。1 回分の時間は `Time::FixedDeltaTime()` です。

## `owner` で自分のオブジェクトを触る

```cpp
owner.name                      // 名前
owner.active = false;           // オブジェクトごと無効にする
owner.transform.position        // 位置
owner.Destroy();                // 壊す
owner.FindObject("Player")      // 名前で他を探す
```

## メンバ変数

**公開したメンバ変数はインスペクタに出て、シーンに保存されます。**

```cpp
float speed = 5.0f;          // 出る・保存される
private float timer_ = 0.0f; // 出ない・保存されない
```

出るのは次の型です。

`bool` `int` `float` `string` `Vector2` `Vector3` `Vector4` と、それらの `array<T>`

→ [インスペクタに出す](inspector.md)

## 保存したまま書き換えられる

エディタのあるビルドでは、**`.as` を保存した瞬間に読み直します。** 再生を止める必要はありません。

- インスペクタに出るメンバ変数の値は**持ち越します**
- それ以外のメンバ変数は**書いた初期値に戻ります**
- Tween などエンジンへ渡した関数は呼ばれなくなります

渡し直しが要るものは `OnScriptReloaded()` に書きます。

```cpp
void OnScriptReloaded()
{
    // 読み直しで消えたものを組み直す
    SetupTween();
}
```

!!! success "1 つ壊れても、他は新しいまま動きます"
    書きかけの 1 ファイルがコンパイルできなくても、**そのファイルだけ直前の版に戻して**他は新しい版で動きます。「ちょっと直している間、全部が古い状態に戻る」ということはありません。

    → [エラーの直し方](errors.md)

## 次は

- インスペクタでの見え方を整える → [インスペクタに出す](inspector.md)
- 他のオブジェクトを触る → [他のものを触る](references.md)
