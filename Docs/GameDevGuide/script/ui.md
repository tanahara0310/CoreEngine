# UI

## 部品は 5 つ

| コンポーネント | 何をするか |
|---|---|
| `RectTransform` | **どこに・どの大きさで**（他の UI と必ず組で使う） |
| `UIImage` | 画像や単色の四角を出す |
| `UIText` | 文字を出す |
| `UIButton` | 押せるようにする |
| `UISlider` | 値をつまみで決める |
| `UIToggle` | 入り切りを覚える |

`UIImage` や `UIText` を足すと `RectTransform` は自動で付きます。

<div class="figure" markdown="0">
--8<-- "assets/figures/ui-parts.svg"
</div>

## RectTransform

| 項目 | 意味 |
|---|---|
| アンカー | **画面のどこを基準にするか**（9 か所） |
| 位置 | 基準からのずらし（px） |
| 基準点 | 自分のどこを位置に合わせるか（`(0,0)` = 左上、`(0.5,0.5)` = 中央） |
| 大きさ | px |
| 回転 | ラジアン |
| 描画順 | **大きいほど手前** |

```cpp
RectTransform@ rect = owner.rectTransform;
rect.anchoredPosition = Vector2(40.0f, 40.0f);
rect.size = Vector2(400.0f, 24.0f);
rect.anchor = UIAnchor::TopLeft;
```

アンカーは `TopLeft` `TopCenter` `TopRight` `MiddleLeft` `Center` `MiddleRight` `BottomLeft` `BottomCenter` `BottomRight` です。

## 文字

```cpp
UIText@ text = owner.uiText;
text.text = "スコア: " + score;
text.fontSize = 36.0f;
text.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
text.SetOutline(Vector4(0.0f, 0.0f, 0.0f, 1.0f), 0.15f);   // (1)
text.SetAlign(TextAlignH::Center, TextAlignV::Middle);
```

1. **縁取りを少し付けると、どんな背景でも読めます。** 明るい空に白い文字を置くと読めません。

!!! warning "毎フレーム `text` に入れ直さないこと"
    文字列の組み立てとフォントの処理は重めです。**値が変わったときだけ**書き換えてください。

    ```cpp
    if (score != shownScore_) {
        text_.text = "スコア: " + score;
        shownScore_ = score;
    }
    ```

## ボタン

```cpp
UIButton@ button = owner.uiButton;

void Update()
{
    if (button.wasClicked) {   // (1)
        Start();
    }
}
```

1. **押された次のフレームだけ** true です。`Update` で見れば取りこぼしません。

| もの | 意味 |
|---|---|
| `wasClicked` | 押されたか（1 フレームだけ） |
| `hovered` | ポインタが乗っているか |
| `focused` | キー・パッドで選ばれているか |
| `pressed` | 押されている最中か |
| `interactable` | 押せるか（`false` で灰色になる） |
| `Focus()` | **このボタンを選んだ状態にする** |

!!! tip "メニューを開いたら `Focus()` を 1 つ呼ぶ"
    これを呼ばないと、キーボードとパッドで選ぶ対象が決まりません。**タイトル画面・ポーズ画面では必ず 1 つ選んでおきます。**

    ```cpp
    void Start()
    {
        UIButton@ first = startButton.uiButton;
        if (first !is null) { first.Focus(); }
    }
    ```

選ばれているボタンは「乗せたときの色」になります。方向キー・十字キー・左スティックで隣へ移り、<kbd>Enter</kbd> か A ボタンで押せます。

## スライダー

**3 つのオブジェクト**で作ります。

| 役 | 付けるもの |
|---|---|
| 溝（当たり判定） | `RectTransform` + `UIImage` + **`UISlider`** |
| 伸びる帯 | `RectTransform` + `UIImage` |
| つまみ | `RectTransform` + `UIImage` |

`UISlider` の **伸びる帯**と**つまみ**の欄で、後の 2 つを繋ぎます。

```cpp
UISlider@ slider = owner.uiSlider;
slider.minValue = 0.0f;
slider.maxValue = 1.0f;
slider.value = 0.8f;
slider.wholeNumbers = false;   // true にすると整数に丸める
```

| 項目 | 意味 |
|---|---|
| `value` | 今の値 |
| `minValue` / `maxValue` | 範囲（外の値を入れると端で止まります） |
| `wholeNumbers` | 整数に丸めるか |
| `direction` | 値が増える向き（0 = 左から右、1 = 右から左、2 = 上から下、3 = 下から上） |
| `navigationStep` | キーで 1 回に動く量（0 なら範囲の 1/10） |

**帯のどこを押してもその位置の値になります。** フォーカス中は左右キーで動き、上下キーではフォーカスが隣へ移ります。

```cpp
void Update()
{
    // 毎フレーム入れて構いません（ただの倍率です）
    Audio::SetBusVolume(AudioBus::BGM, slider.value);
}
```

## トグル

```cpp
UIToggle@ toggle = owner.uiToggle;
toggle.isOn = true;
```

| 項目 | 意味 |
|---|---|
| `isOn` | 入っているか |
| `group` | **同じ綴りのトグル同士で 1 つだけが入る** |
| `interactable` | 押せるか |

**入りのときに出す印**に `UIImage` を繋ぐと、入りのときだけ出ます。

グループを使うと「低 / 中 / 高」のような択一の選択になります。グループに入っているトグルは、**入っているものを押しても切れません**（全部切りにならないように）。

## 画像

```cpp
UIImage@ image = owner.uiImage;
image.color = Vector4(1.0f, 0.2f, 0.2f, 1.0f);
```

テクスチャを指していなければ**白い四角**になります。色を変えるだけで単色のバーや枠が作れます。

## フォーカスを外す

ゲーム中に UI が選ばれたままだと、**ゲームの操作が止まります**（場面が `UI` になるため）。メニューを閉じるときは外してください。

```cpp
UI::ClearFocus();

if (UI::HasFocus()) { … }
```

## スクリプトから UI を作る

```cpp
UIText@ label = owner.SpawnUIText("", "スコア: 0", "ScoreLabel");
UIImage@ icon = owner.SpawnUIImage(texturePath, "Icon");
```

置き場所が決まっている UI は、**エディタで作ってインスペクタで繋ぐ**ほうが確実です。動的に増えるもの（ダメージ表示など）だけスクリプトで作ってください。
