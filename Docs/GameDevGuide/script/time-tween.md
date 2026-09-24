# 時間・動き・乱数

## 時間

```cpp
float dt = Time::DeltaTime();            // 前フレームからの秒数
float unscaled = Time::UnscaledDeltaTime();
float since = Time::TimeSinceStartup();
uint64 frame = Time::FrameCount();
float fixed = Time::FixedDeltaTime();    // 物理 1 刻み分
```

!!! warning "必ず `DeltaTime` を掛けてください"
    ```cpp
    // 悪い：フレームレートで速さが変わる
    position.x += 0.1f;

    // 良い：どの環境でも 5 m/s
    position.x += 5.0f * Time::DeltaTime();
    ```

### 遅くする・止める

```cpp
Time::SetTimeScale(0.2f);   // スローモーション
Time::SetTimeScale(0.0f);   // 完全に止める（ポーズ）
Time::SetTimeScale(1.0f);   // 戻す

if (Time::IsPaused()) { … }
```

`DeltaTime` はこの倍率が掛かった値、`UnscaledDeltaTime` は**掛かっていない**値です。

**ポーズ中でも動かしたいもの**（メニューのアニメーション、ポーズ画面の演出）は `UnscaledDeltaTime` を使ってください。

```cpp
void Update()
{
    // ポーズ中でもメニューは動く
    menuTimer_ += Time::UnscaledDeltaTime();
}
```

---

## Tween（なめらかに動かす）

値を A から B へ、時間をかけて変えます。

```cpp
// 2 秒かけて上へ
Tween::MoveTo(owner, Vector3(0.0f, 5.0f, 0.0f), 2.0f);

// 大きさ・回転も
Tween::ScaleTo(owner, Vector3(2.0f, 2.0f, 2.0f), 0.3f);
Tween::RotateTo(owner, Vector3(0.0f, 3.14f, 0.0f), 1.0f);
```

### 動き方を変える

```cpp
Tween::MoveTo(owner, target, 1.0f)
    .SetEase(EaseType::EaseOutBack)   // (1)
    .SetDelay(0.2f)
    .SetLink(owner);                  // (2)
```

1. 動き方。`EaseOutBack` は行き過ぎて戻るので、UI が出るときに気持ちよく見えます。
2. **このオブジェクトが消えたら Tween も止まります。** 付けておくと安全です。

| よく使う `EaseType` | 見た目 |
|---|---|
| `Linear` | 一定速度 |
| `EaseOutQuad` `EaseOutCubic` | 勢いよく始まってゆっくり止まる（**いちばん無難**） |
| `EaseInQuad` | ゆっくり始まって加速 |
| `EaseInOutQuad` | 両端がゆっくり |
| `EaseOutBack` | 行き過ぎて戻る |
| `EaseOutBounce` | 跳ねて止まる |
| `EaseOutElastic` | ぷるぷる揺れて止まる |

`In` / `Out` / `InOut` と `Quad` `Cubic` `Quart` `Quint` `Sine` `Expo` `Circ` `Back` `Elastic` `Bounce` の組み合わせで、全部で 31 種類あります。

### 好きな値を動かす

```cpp
// 透明度をフェードイン
Tween::To(0.0f, 1.0f, 0.5f, function(float value) {
    Rendering::SetFadeAlpha(1.0f - value);
});
```

### つなげる

```cpp
TweenSequence sequence = Tween::Sequence();
sequence.Append(Tween::MoveTo(owner, up, 0.3f));       // 上がって
sequence.AppendInterval(0.2f);                          // 少し待って
sequence.Append(Tween::ScaleTo(owner, big, 0.2f));      // 大きくなって
sequence.AppendCallback(function() { Log("終わり"); });
sequence.SetLink(owner);
```

`Append` は順番に、`Join` は同時に実行します。

### 止める

```cpp
TweenHandle handle = Tween::MoveTo(owner, target, 1.0f).SetId("move");
handle.Kill();

Tween::KillById("move");
Tween::KillByLink(owner);   // このオブジェクトの Tween を全部
```

!!! warning "スクリプトを保存し直すと Tween は切れます"
    エディタでスクリプトを読み直すと、エンジンへ渡した関数は呼ばれなくなります。**渡し直しは `OnScriptReloaded()` に書いてください。**

---

## カメラを揺らす

```cpp
CameraShake::PlayPreset("Hit");
CameraShake::PlayPreset("Explosion", 1.5f);   // 強さ 1.5 倍
```

使えるプリセットは `Hit` `HeavyHit` `Explosion` `Landing` `Recoil` `Earthquake` `Handheld` `Rumble` です。

自分で作ることもできます。

```cpp
CameraShakeParams params = CameraShakePresets::Explosion();
params.duration = 0.8f;
uint handle = CameraShake::Play(params);
CameraShake::Stop(handle, 0.2f);
```

`AddTrauma` を使うと、**連続してぶつかるほど強く揺れます**。

```cpp
void OnCollisionEnter(Collision@ collision)
{
    CameraShake::AddTrauma(collision.impulse * 0.02f);
}
```

---

## 乱数

```cpp
float f = Random::Range(0.0f, 1.0f);   // 0.0 以上 1.0 以下
int i = Random::Range(0, 10);          // 0 以上 10 未満
bool hit = Random::Chance(0.3f);       // 30% で true
```

```cpp
// 敵をばらまく
for (int i = 0; i < 10; i++) {
    GameObject@ enemy = owner.InstantiatePrefab(enemyPrefab, "Enemy");
    enemy.transform.position = Vector3(
        Random::Range(-20.0f, 20.0f), 0.0f, Random::Range(-20.0f, 20.0f));
}
```

毎回同じ並びにしたいときは `RandomStream` を使います（同じ種から同じ並びが出ます）。
