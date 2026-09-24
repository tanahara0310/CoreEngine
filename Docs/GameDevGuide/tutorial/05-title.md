# 5. タイトルと設定

**この章でやること** — タイトル画面から始めて、設定画面で音量を変えられるようにします。

**かかる時間** — 20 分ほど

---

## タイトルシーンを作る

**File → 新しいシーン…** で `Title` を作ります。

### ボタンを置く

**GameObject → UI → 画像** で作り、`UIButton` を足すとボタンになります。名前は `StartButton`。

| コンポーネント | 設定 |
|---|---|
| `RectTransform` | アンカー `中央`、位置 `(0, 80)`、大きさ `(320, 96)` |
| `UIImage` | 好きなテクスチャ（無くても白い四角になります） |
| `UIButton` | 通常・乗せたとき・押したとき・押せないときの 4 色 |

同じ手順で `ExitButton`（位置 `(0, -40)`）も作ります。

ボタンの上に文字を置きたいときは、別オブジェクトで `UIText` を作り、同じ位置に置いて**描画順**を 1 つ大きくします。

### 押されたら動く

```cpp title="TitleMenu.as"
[DisplayName("タイトル")]
class TitleMenu : ScriptComponent
{
    [ObjectRef] [Tooltip("はじめる")]
    GameObject@ startButton;

    [ObjectRef] [Tooltip("やめる")]
    GameObject@ exitButton;

    [Asset("Scene")] [Tooltip("はじめたときに行くシーン")]
    string nextScene = "";

    void Start()
    {
        // キーとパッドでも操作できるよう、最初のボタンを選んでおく
        if (startButton !is null) {
            UIButton@ button = startButton.uiButton;
            if (button !is null) { button.Focus(); }   // (1)
        }
    }

    void Update()
    {
        if (WasClicked(startButton) && nextScene != "") {
            Scene::ChangeScene(nextScene);   // (2)
        }
        if (WasClicked(exitButton)) {
            Log("やめる が押されました");
        }
    }

    private bool WasClicked(GameObject@ object)
    {
        if (object is null) { return false; }
        UIButton@ button = object.uiButton;
        return button !is null && button.wasClicked;   // (3)
    }
}
```

1. これを呼ばないと、キーボードとパッドで選ぶ対象が決まりません。**タイトル画面では必ず 1 つ選んでおきます。**
2. シーンの名前で切り替えます。
3. `wasClicked` は**押された次のフレームだけ** true です。`Update` で見れば取りこぼしません。

!!! info "マウスでもキーでもパッドでも押せます"
    ボタンは 3 つとも同じ扱いです。**方向キー・十字キー・左スティックで選び、<kbd>Enter</kbd> か A ボタンで押します。** 選ばれているボタンは「乗せたときの色」になります。

## 設定画面を作る

### 音量のスライダー

スライダーは **3 つのオブジェクト**で作ります。

<div class="figure" markdown="0">
--8<-- "assets/figures/slider-parts.svg"
</div>

1. `VolumeFill`（`UIImage`、明るい色、大きさ `(0, 24)`）
2. `VolumeHandle`（`UIImage`、白、大きさ `(32, 48)`）
3. `VolumeTrack`（`UIImage`、暗い色、大きさ `(600, 24)`、`UISlider` を足す）

`VolumeTrack` の `UISlider` で、

| 項目 | 入れる値 |
|---|---|
| 向き | 左から右 |
| 最小値 / 最大値 | `0` / `1` |
| 値 | `0.8` |
| 伸びる帯 | `VolumeFill` を選ぶ |
| つまみ | `VolumeHandle` を選ぶ |

!!! tip "帯とつまみは繋がなくても値は動きます"
    見た目が要らないなら繋がなくて構いません。繋ぐと、値に合わせて**アンカーを揃えて**自動で置き直します。

### 入り切りのトグル

`UIImage` に `UIToggle` を足します。**入りのときに出す印**に、小さい `UIImage` を繋ぐとチェックマークになります。

同じ **グループ**の綴りを入れたトグル同士は、**1 つだけが入り**になります。画質の「低 / 中 / 高」のような選択に使います。

### 繋いで動かす

```cpp title="SettingsMenu.as"
[DisplayName("設定")]
class SettingsMenu : ScriptComponent
{
    [ObjectRef] [Tooltip("全体の音量")]
    GameObject@ masterSlider;

    [ObjectRef] [Tooltip("BGM の音量")]
    GameObject@ bgmSlider;

    [ObjectRef] [Tooltip("効果音の音量")]
    GameObject@ seSlider;

    [ObjectRef] [Tooltip("戻るボタン")]
    GameObject@ backButton;

    [Tooltip("戻る先のシーン")]
    string titleScene = "Title";

    void Start()
    {
        // 前回の設定をつまみへ戻す
        Apply(masterSlider, Session::GetFloat("vol.master", 1.0f));
        Apply(bgmSlider, Session::GetFloat("vol.bgm", 0.8f));
        Apply(seSlider, Session::GetFloat("vol.se", 0.8f));
    }

    void Update()
    {
        float master = Read(masterSlider, 1.0f);
        float bgm = Read(bgmSlider, 0.8f);
        float se = Read(seSlider, 0.8f);

        Audio::SetMasterVolume(master);              // (1)
        Audio::SetBusVolume(AudioBus::BGM, bgm);
        Audio::SetBusVolume(AudioBus::SE, se);

        Session::SetFloat("vol.master", master);     // (2)
        Session::SetFloat("vol.bgm", bgm);
        Session::SetFloat("vol.se", se);

        if (backButton !is null) {
            UIButton@ button = backButton.uiButton;
            if (button !is null && button.wasClicked) {
                Scene::ChangeScene(titleScene);
            }
        }
    }

    private float Read(GameObject@ object, float fallback)
    {
        if (object is null) { return fallback; }
        UISlider@ slider = object.uiSlider;
        return slider is null ? fallback : slider.value;
    }

    private void Apply(GameObject@ object, float value)
    {
        if (object is null) { return; }
        UISlider@ slider = object.uiSlider;
        if (slider !is null) { slider.value = value; }
    }
}
```

1. 毎フレーム同じ値を入れても害はありません。バス音量はただの倍率です。
2. `Session` はシーンをまたいで残る入れ物です。ゲームを閉じると消えます。

!!! note "終了しても残したいなら"
    `Session` はアプリを閉じると消えます。次回も残したい設定は `CVar` に入れると、プロジェクトの設定ファイルへ保存されます。

## 音を鳴らす

### BGM

**GameObject → 空のオブジェクトを作成** で `Bgm` を作り、`AudioSource` を足します。

| 項目 | 入れる値 |
|---|---|
| 音 | BGM のファイル |
| 出力先 | BGM |
| 開始時に鳴らす | ✓ |
| 繰り返す | ✓ |
| 音量 | `0.8` |
| フェードイン | `1.5`（秒） |

これだけで、シーンを開いた瞬間から鳴り始めます。**スクリプトは要りません。**

### ボタンの効果音

ボタンのオブジェクトに `AudioSource` を足し（出力先は **効果音**、開始時に鳴らすは**外す**）、押されたときに鳴らします。

```cpp
if (button.wasClicked) {
    AudioSource@ sound = startButton.audioSource;
    if (sound !is null) { sound.PlayOneShot(); }   // (1)
}
```

1. `PlayOneShot` は前の音を止めずに重ねて鳴らします。連打しても切れません。

## 起動シーンを変える

`Application/Config/EngineSettings/Project.json` の `initialScene` を `Title` にすると、次からタイトルで始まります。

```json
{
    "initialScene": "Title",
```

---

## この章のまとめ

- ボタンは **`UIImage` + `UIButton`**。`wasClicked` を `Update` で見る
- **キーとパッドで操作させるには `Focus()` を 1 つ呼んでおく**
- スライダーは **溝・伸びる帯・つまみ**の 3 つ。値は `value`
- トグルは `isOn`。**同じグループなら 1 つだけが入り**
- BGM は **`AudioSource` の「開始時に鳴らす」だけ**で鳴る。スクリプト不要
- シーンの切り替えは `Scene::ChangeScene("名前")`

---

## ここまでで作れるもの

5 章を通すと、**タイトル → ゲーム → 設定**の形ができています。ここから先は、

- 敵を出す → [物理と当たり判定](../script/physics.md)
- 演出を付ける → [時間・動き・乱数](../script/time-tween.md)
- 使えるコンポーネントを見る → [コンポーネント一覧](../reference/components.md)
