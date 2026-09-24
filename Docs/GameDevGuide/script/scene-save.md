# シーンとセーブ

## シーンを切り替える

```cpp
Scene::ChangeScene("Result");
string now = Scene::GetCurrentName();
```

切り替えると、**シーンのオブジェクトは全部作り直されます。** 変数の中身は残りません。

## 持ち越す（Session）

```cpp
// ゲーム側
Session::SetInt("score", score);
Session::SetBool("cleared", true);
Scene::ChangeScene("Result");

// リザルト側
int score = Session::GetInt("score", 0);
bool cleared = Session::GetBool("cleared", false);
```

| 関数 | 型 |
|---|---|
| `SetInt` / `GetInt` | 整数 |
| `SetFloat` / `GetFloat` | 小数 |
| `SetBool` / `GetBool` | 真偽 |
| `SetString` / `GetString` | 文字列 |
| `Has` / `Remove` | あるか / 消す |

`Get` の 2 つ目は、**まだ入っていないときの値**です。

!!! warning "アプリを閉じると消えます"
    `Session` は実行中だけの入れ物です。ハイスコアのように**次回も残したい**ものには使えません。

## 残す（CVar）

次回の起動でも残したい設定は `CVar` に入れます。プロジェクトの設定ファイルへ保存されます。

```cpp
CVar::SetFloat("game.volume.bgm", 0.5f);
float bgm = CVar::GetFloat("game.volume.bgm", 0.8f);

CVar::SetInt("game.highscore", score);
int best = CVar::GetInt("game.highscore", 0);
```

| 関数 | 型 |
|---|---|
| `GetBool` / `SetBool` | 真偽 |
| `GetInt` / `SetInt` | 整数 |
| `GetFloat` / `SetFloat` | 小数 |
| `GetVector2` / `GetVector3` / `GetColor` | ベクトル・色 |
| `Exists` / `Reset` | あるか / 既定値へ戻す |

!!! warning "再生中の書き込みは残りません"
    エディタで再生している間に書いた値は、止めると元に戻ります。**ゲーム（Release）として動かしているときは保存されます。**

## 3 つの使い分け

<div class="figure" markdown="0">
--8<-- "assets/figures/value-storage.svg"
</div>

| 入れ物 | 生きる範囲 | 例 |
|---|---|---|
| メンバ変数 | そのオブジェクトが生きている間 | 残り HP・弾数 |
| `Session` | アプリを閉じるまで | ステージ間のスコア・選んだキャラ |
| `CVar` | **次回の起動でも** | 音量設定・ハイスコア・難易度 |

## 画面を暗くしてから切り替える

いきなり切り替えると目が痛いので、フェードを挟みます。

```cpp
private float fade_ = 0.0f;
private bool changing_ = false;

void GoToResult()
{
    changing_ = true;
}

void Update()
{
    if (!changing_) { return; }

    fade_ += Time::UnscaledDeltaTime() / 0.5f;   // (1)
    Rendering::SetFadeAlpha(fade_);

    if (fade_ >= 1.0f) {
        Scene::ChangeScene("Result");
    }
}
```

1. **`UnscaledDeltaTime` を使います。** ポーズ中（`TimeScale = 0`）でも暗転が進むようにするためです。

Tween を使うともっと短く書けます。

```cpp
Tween::To(0.0f, 1.0f, 0.5f, function(float value) {
    Rendering::SetFadeAlpha(value);
}).OnComplete(function() {
    Scene::ChangeScene("Result");
});
```

## ゲーム全体の流れを作る

<div class="figure" markdown="0">
--8<-- "assets/figures/game-flow.svg"
</div>

シーンを 4 つ作り、`Scene::ChangeScene` で行き来します。持ち越す値は `Session` に入れます。

起動シーンは `Application/Config/EngineSettings/Project.json` の `initialScene` で決めます。
