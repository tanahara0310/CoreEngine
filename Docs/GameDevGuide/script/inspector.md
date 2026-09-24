# インスペクタに出す

**公開したメンバ変数は、そのままインスペクタに出ます。** 属性を付けると見た目と扱いを変えられます。

## 出る型

| 型 | インスペクタでの見た目 |
|---|---|
| `bool` | チェック |
| `int` `float` | 数値（ドラッグで増減） |
| `string` | 文字入力 |
| `Vector2` `Vector3` `Vector4` | 数値が 2〜4 個 |
| `array<T>` | 上の型の並び（増減できる） |

`private` と `protected` は出ませんし、保存もされません。

## 属性の一覧

```cpp
[DisplayName("歩く速さ")] [Range(1.0f, 12.0f)] [Tooltip("m/s")]
float walkSpeed = 4.0f;
```

`[A] [B]` と並べても、`[A, B]` とまとめても書けます。

| 属性 | 何をするか |
|---|---|
| `[DisplayName("名前")]` | インスペクタでの表示名 |
| `[Range(最小, 最大)]` | 編集できる範囲。`[Range(最小, 最大, ドラッグ速度)]` も書ける |
| `[Speed(速度)]` | ドラッグしたときの増え方だけ決める |
| `[ReadOnly]` | **編集させない。保存もしない**（実行中の値を見せる用） |
| `[Hidden]` | インスペクタに出さない（**保存はする**） |
| `[Transient]` | 保存しない（**インスペクタには出す**） |
| `[Color]` | `Vector4` を色として編集する |
| `[Tooltip("説明")]` | カーソルを乗せたときの説明 |
| `[Asset("種類")]` | `string` をアセットの参照にする |
| `[ObjectRef]` | シーン内の別オブジェクトへの参照にする |

クラスにも付けられます。

```cpp
[DisplayName("プレイヤー操作")]
class PlayerMove : ScriptComponent
```

## `[Asset]` でファイルを選ばせる

```cpp
[Asset("Prefab")] [Tooltip("出す弾")]
string bulletPrefab = "";

[Asset("Texture")] [Tooltip("アイコン")]
string iconPath = "";

[Asset("Audio")] [Tooltip("当たったときの音")]
string hitSound = "";
```

使える種類は `Texture` `Model` `Shader` `Audio` `Material` `Scene` `Prefab` `Animation` `MaterialLibrary` `Json` `Csv` です。

**GUID とパスの両方で保存する**ので、ファイルの名前を変えても場所を移しても参照が切れません。変数には**プロジェクトの根からのパス**が入ります。

## `[ObjectRef]` で別のオブジェクトと繋ぐ

```cpp
[ObjectRef] [Tooltip("追いかける相手")]
GameObject@ target;

[ObjectRef] [Tooltip("スコアを持っている人")]
ScoreKeeper@ keeper;
```

インスペクタに選ぶ欄が出ます。**ID で保存する**ので、相手の名前を変えても切れません。

クラスのハンドルを書くと、**そのクラスのコンポーネントだけ**が候補になります。

!!! tip "名前で探すより `[ObjectRef]` が確実です"
    `owner.FindObject("Player")` は、名前を変えた瞬間に壊れます。しかもエラーにならず、ただ動かなくなります。`[ObjectRef]` なら**繋ぎ忘れがインスペクタで見えます。**

## `[ReadOnly]` と `[Transient]` の違い

| 属性 | インスペクタ | 保存 | 使いどころ |
|---|---|---|---|
| なし | 編集できる | する | ふつうの設定値 |
| `[ReadOnly]` | **見るだけ** | しない | 実行中の状態を見せる（残り HP など） |
| `[Hidden]` | 出ない | **する** | 内部の値だが引き継ぎたい |
| `[Transient]` | 編集できる | しない | 一時的な値（外から入れる速度など） |

```cpp
[ReadOnly] [Tooltip("集めた数")]
int collected = 0;          // 見えるが触れない・保存しない

[Transient] [Tooltip("速度（撃つ側が入れる）")]
Vector3 velocity;           // 触れるが保存しない
```

## `array<T>` を使う

```cpp
[Tooltip("順に回るカメラ位置")]
array<Vector3> waypoints;

[Tooltip("各段階の色")] [Color]
array<Vector4> stageColors;

void Start()
{
    for (uint i = 0; i < waypoints.length(); i++) {
        Log("地点 " + i + ": " + waypoints[i].x);
    }
}
```

インスペクタで要素を増減できます。

## 書いた初期値の扱い

```cpp
float speed = 5.0f;
```

この `5.0f` は「**シーンにまだ値が無いときの値**」です。一度インスペクタで変えてシーンを保存すると、以後はシーンの値が使われます。**コードの初期値を変えても、保存済みのオブジェクトには反映されません。**

全部を新しい初期値にしたいときは、インスペクタの ⋮ から **既定値へ戻す**を使います。
