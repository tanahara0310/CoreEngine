# 音

鳴らし方は 2 つあります。

| 方法 | いつ使うか |
|---|---|
| **`AudioSource` コンポーネント** | **ふつうはこちら。** 設定だけで鳴る |
| `Audio::` の関数 | 音のファイルがその場で決まるとき |

---

## AudioSource（おすすめ）

オブジェクトに `AudioSource` を足し、音を指すだけです。

| 項目 | 意味 |
|---|---|
| 音 | 鳴らすファイル |
| 出力先 | **BGM / 効果音 / ボイス**（音量設定の単位） |
| 開始時に鳴らす | 再生が始まったら自動で鳴らす |
| 繰り返す | ループするか |
| 音量 / 高さ / 消音 | そのまま |
| フェードイン | 音量 0 からここまで上げる秒数 |
| 距離で変える | **0 = どこでも同じ音量、1 = 距離と左右を反映** |
| 減衰の始まり / 聞こえる限界 | この距離までは下がらない / この距離で 0 |

### BGM

`AudioSource` を足して、

- 出力先 = **BGM**
- 開始時に鳴らす = ✓
- 繰り返す = ✓
- フェードイン = `1.5`

**これだけで鳴ります。スクリプトは要りません。**

### 効果音

```cpp
private AudioSource@ sound_;

void Start()
{
    @sound_ = owner.audioSource;
}

void Hit()
{
    if (sound_ !is null) { sound_.PlayOneShot(); }   // (1)
}
```

1. **`PlayOneShot` は前の音を止めずに重ねます。** 連打しても切れません。`Play()` だと鳴らし直しになります。

| 操作 | 意味 |
|---|---|
| `Play()` | 先頭から鳴らす（鳴っていれば鳴らし直す） |
| `Stop()` | 止める |
| `Pause()` / `UnPause()` | 位置を保って止める / 再開 |
| `PlayOneShot()` | **重ねて鳴らす**（止められない） |
| `FadeOut(秒)` | 下げてから止める |
| `isPlaying` | 鳴っているか |

---

## 距離で音量を変える（3D 音）

1. **カメラに `AudioListener` を付ける**（シーンに 1 つ）
2. 音を出す側の `AudioSource` で **距離で変える**を `1` にする
3. **減衰の始まり**と**聞こえる限界**を決める

これで、離れるほど小さくなり、右にあるものは右から鳴ります。

!!! warning "`AudioListener` が無いと効きません"
    聞き手がいないと距離を測れないので、警告を 1 回出して距離を無視します。**カメラに付けてください。**

!!! info "既定は 2D です"
    **距離で変える**の既定は `0` です。UI の音や BGM は 0 のままにしてください。

---

## バス（音量のまとまり）

音は 3 つのバスのどれかへ流れます。

<div class="figure" markdown="0">
--8<-- "assets/figures/audio-bus.svg"
</div>

```cpp
Audio::SetMasterVolume(0.8f);
Audio::SetBusVolume(AudioBus::BGM, 0.5f);
Audio::SetBusVolume(AudioBus::SE, 0.9f);

float now = Audio::GetBusVolume(AudioBus::BGM);
Audio::StopAll();
```

**鳴っている音の本数に関係なく、まとめて効きます。** 設定画面のスライダーはこれを動かします。

→ [5. タイトルと設定](../tutorial/05-title.md)

---

## `Audio::` の関数で鳴らす

鳴らすファイルが実行時に決まるときに使います。

```cpp
// 撃ちっぱなしの効果音
PlayParams params;
params.bus = AudioBus::SE;
params.volume = 0.8f;
Audio::PlayOneShot("Application/Assets/Sounds/hit.wav", params);

// 制御したい音（止める・音量を変える）
PlayParams bgmParams;
bgmParams.bus = AudioBus::BGM;
bgmParams.loop = true;
bgmParams.fadeInTime = 1.5f;
Sound@ bgm = Audio::PlayScoped("Application/Assets/Sounds/bgm.mp3", bgmParams);
```

`PlayScoped` が返す `Sound` は、**持っている間だけ鳴ります。** 変数を手放すと止まります。BGM のようにシーンが持つ音に向きます。

```cpp
bgm.volume = 0.5f;
bgm.FadeOut(2.0f);
bgm.Stop();
if (bgm.isPlaying) { … }
```

| `PlayParams` の項目 | 意味 |
|---|---|
| `bus` | 出力先（既定は `SE`。**BGM は必ず指定してください**） |
| `loop` | 繰り返すか |
| `volume` | 音量 |
| `pitch` | 再生速度の倍率（上げると速く高くなる） |
| `fadeInTime` | フェードインの秒数 |

---

## 音が鳴らないときに見るところ

| 症状 | 原因 |
|---|---|
| まったく鳴らない | **停止中です。** ▶ を押してください |
| ファイルを指していない | インスペクタの**音**の欄が空だと、警告を出して鳴りません |
| 小さすぎる | バス音量かマスター音量が下がっています |
| 遠くで鳴らない | **距離で変える**が 1 で、**聞こえる限界**より遠いかもしれません |
| 連打で切れる | `Play()` ではなく `PlayOneShot()` を使ってください |
| BGM が効果音の音量で変わる | **出力先**が `SE` のままです。`BGM` にしてください |

対応している形式は WAV と、Media Foundation が読めるもの（MP3 など）です。
