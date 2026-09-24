# シーン

## シーンはフォルダ 1 つ

`Application/Assets/Scenes/` の下に、**シーン名のフォルダが 1 つ**あります。

```
Scenes/
└── MyGame/
    ├── _scene.json        どのオブジェクトを読むか
    ├── _environment.json  空・雲・霧・ポストエフェクトの設定
    ├── MainCamera.json    オブジェクト 1 個
    ├── Player.json
    └── Coin.json
```

**オブジェクト 1 個が JSON 1 つ**です。これは意図的な作りで、

- **どのオブジェクトを誰が変えたか、差分で分かります**
- 2 人が別のオブジェクトを同時に変えても、マージで衝突しません
- 1 つのオブジェクトをコピーして別のシーンへ持っていけます

## `_scene.json`

```json
{
    "objects": [
        "MainCamera",
        "Player",
        "Coin"
    ],
    "defaultGround": false
}
```

| 項目 | 意味 |
|---|---|
| `objects` | **読む順番**。上から順に作られます |
| `defaultGround` | 自動の床を出すか（省略すると出ます） |
| `features` | シーン独自の機能（水面など。省略可） |

!!! tip "並び順が読む順番です"
    他のオブジェクトを `Start` で探すスクリプトは、探される側より**後ろ**に置いてください。ただし `Start` は全オブジェクトが作られた後に呼ばれるので、ふつうは気にしなくて構いません。

## `_environment.json`

空・雲・霧・ポストエフェクトの設定が入ります。中身は **CVar（設定値）の名前と値**の一覧です。

```json
{
    "r.Atmosphere.SkyAmbientScale": 0.51,
    "r.AutoExposure.Enabled": true,
    "r.Bloom.Intensity": 0.95,
    "r.Cloud.DensityScale": 0.007
}
```

**シーンごとに違う空と色味**を持たせられます。夜のステージと昼のステージで別の設定、といった使い方です。

編集は **Engine Settings**（→ [設定](settings.md)）から行い、シーンを保存すると書き込まれます。

!!! note "環境のコンポーネントは「置き場所」です"
    `HeightFog` `VolumetricCloud` `PostProcess` をシーンに置けますが、**中身の値はここ（CVar）が持っています。** コンポーネント側は「このシーンに霧がある」という印と、有効・無効だけです。

## 作る・開く・保存する

| 操作 | やり方 |
|---|---|
| 新しいシーン | **File → 新しいシーン…** |
| 開く | **File → シーンを開く** |
| 保存 | <kbd>Ctrl</kbd>+<kbd>S</kbd> |
| 読み直す（保存前に戻す） | **File → シーンを再読み込み** |

右下に、今のシーン名と「保存済み / 未保存」が出ます。

## 起動シーンを決める

`Application/Config/EngineSettings/Project.json` の `initialScene` です。

```json
{
    "initialScene": "PhysicsTestScene",
```

## スクリプトから切り替える

```cpp
Scene::ChangeScene("Result");
string now = Scene::GetCurrentName();
```

切り替えるとシーンのオブジェクトは全部作り直されます。**持ち越したい値は `Session` に入れてください。**

```cpp
// ゲーム側
Session::SetInt("score", score);
Scene::ChangeScene("Result");

// リザルト側
int score = Session::GetInt("score", 0);
```

→ [シーンとセーブ](../script/scene-save.md)

## 複数人で作るときの決まり

- **1 人 1 オブジェクトずつ触る。** 同じ `Player.json` を 2 人で同時に変えなければ衝突しません
- **`_scene.json` は最後に合わせる。** オブジェクトの追加・削除でここだけは触ります
- **`_environment.json` は担当を決める。** 1 ファイルに全部入っているので、同時に触ると衝突します
