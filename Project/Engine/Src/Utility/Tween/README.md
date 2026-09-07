# Tween 使用方法

`Tween` は、**値を時間で動かす**ための仕組みです。

UI のスライドイン、被弾フラッシュ、扉の開閉、スコアのカウントアップ、カメラのズーム——
画面上の動きのほとんどはこれで書けます。

`GameTimer` と `EasingUtil` の上に載っているので、イージングの種類はすべてそのまま使えます。

---

## 1. 準備

```cpp
#include "Utility/Tween/Tween.h"
```

更新はエンジンが行います（`BaseScene::Update()` が毎フレーム進めます）。利用側で `Update()` を呼ぶ必要はありません。

---

## 2. いちばん基本の形

```cpp
// value を 0.3 秒かけて 1.0 にする
Tween::To(&value, 1.0f, 0.3f);
```

`float` / `Vector2` / `Vector3` / `Vector4` / `Quaternion` が対象にできます
（`Quaternion` だけは自動的に球面線形補間になります）。

### イージングを付ける

```cpp
Tween::To(&value, 1.0f, 0.3f).SetEase(EasingUtil::Type::EaseOutCubic);
```

### ★ SetLink を忘れないこと

```cpp
Tween::To(&transform->Get().translate, target, 0.5f)
    .SetLink(GetOwner());   // ← 対象が破棄されたら自動でキルされる
```

ポインタを書き換えるトゥイーンで **`SetLink()` を付けないと、対象の `GameObject` が
`Destroy()` されたあとも書き込み続けて解放済みメモリを踏みます。**
このシステムで唯一クラッシュし得る経路がここなので、必ず付けてください。

Debug > Tween パネルでは、link の無いトゥイーンが `none (!)` と警告色で表示されます。

### セッター経由（値を直接持てない相手）

`MaterialComponent::SetColor()` のように getter が無い相手は、開始値を明示します。

```cpp
Tween::To<Vector4>(baseColor, hitColor, 0.05f,
    [material](const Vector4& c) { material->SetColor(c); })
    .SetLink(GetOwner());
```

### Transform の近道

`GameObject` を渡すだけで、`SetLink` まで自動で付きます。

```cpp
Tween::MoveTo (object, { 0.0f, 3.0f, 0.0f }, 0.5f);  // 位置
Tween::ScaleTo(object, { 1.2f, 1.2f, 1.2f }, 0.2f);  // スケール
Tween::RotateTo(object, { 0.0f, 3.14f, 0.0f }, 1.0f); // オイラー角（ラジアン）
```

### 遅延実行

手書きのカウントダウン変数を置き換えます。

```cpp
Tween::Delay(0.5f, [this] { Explode(); }).SetLink(GetOwner());
```

---

## 3. 設定できること

すべて生成直後に繋げて書けます。

```cpp
Tween::To(&value, 1.0f, 0.3f)
    .SetEase(EasingUtil::Type::EaseOutBack)
    .SetDelay(0.1f)                        // 0.1 秒待ってから動き出す
    .SetLoops(3, TweenLoop::Yoyo)          // 3 回、往復で
    .SetUpdateType(TweenUpdate::Unscaled)  // ポーズ中も動く
    .SetLink(GetOwner())
    .SetId("hud")                          // 一括キル・デバッグ表示用
    .OnComplete([this] { OnFinished(); })
    .OnStepComplete([] { /* 1 ループ終わるたび */ })
    .OnUpdate([](float t) { /* 毎フレーム。t は進捗 0..1 */ });
```

| 設定 | 意味 |
|---|---|
| `SetEase` | イージング（既定は `Linear`） |
| `SetDelay` | 開始を遅らせる秒数 |
| `SetLoops(n, type)` | `n` 回ループ。`-1` で無限。`Restart` は毎回頭から、`Yoyo` は往復 |
| `SetUpdateType` | `Scaled`（既定・ヒットストップの影響を受ける）／`Unscaled`（ポーズ中も動く） |
| `SetLink` | 生存を紐づける `GameObject` |
| `SetId` | `Tween::KillById()` とデバッグ表示に使う名前 |

### Scaled と Unscaled の使い分け

`Time::SetTimeScale(0.0f)` でポーズしたとき、

- **ゲーム内の演出** → `Scaled`（既定）。一緒に止まってほしい
- **ポーズメニューの開閉アニメ** → `Unscaled`。止まると操作できなくなる

---

## 4. TweenSequence

複数のトゥイーンを順番／同時に流します。

```cpp
TweenSequence()
    .Append(Tween::To(&alpha, 1.0f, 0.5f))       // まずフェードイン
    .Join  (Tween::To(&scale, 1.2f, 0.5f))       // ↑と同時に拡大
    .AppendInterval(1.0f)                        // 1 秒待つ
    .AppendCallback([] { Sound::Play("close"); })// SE を鳴らす
    .Append(Tween::To(&alpha, 0.0f, 0.3f))       // フェードアウト
    .SetLink(GetOwner())
    .OnComplete([this] { GetOwner()->Destroy(); });
```

- `Append` … 前のステップが終わってから始まる
- `Join` … 直前のステップと**同時**に走る
- `AppendInterval` … 待ち時間
- `AppendCallback` … その場で 1 回だけ呼ばれる

### 注意

- `Append` / `Join` に渡したトゥイーンは、**シーケンスの持ち物になります**（単独では再生されなくなります）。
- **シーケンスは 1 つの式の中で組み立ててください。** メンバに持って複数フレームにまたがって
  `Append` すると、その前に空のまま完了してしまいます。
- シーケンスに `SetLoops(n, Yoyo)` を指定しても、逆再生には対応していないため `Restart` と同じ挙動になります。
- 子の `SetUpdateType` はシーケンス側の設定に従います（親が `Unscaled` なら子も `Unscaled`）。

---

## 5. 止める・進捗を見る

```cpp
TweenHandle handle = Tween::To(&value, 1.0f, 1.0f).SetLink(GetOwner());

handle.IsActive();     // まだ再生中か
handle.Progress();     // 進捗 0..1
handle.Kill();         // 止める（値はその場に残る）
handle.Kill(true);     // 最終値まで飛ばして止める（= Complete()）
handle.Complete();     // 同上
```

### ★ ハンドルは保持しなくてよい

`Subscription`（EventBus）と違い、**`TweenHandle` は破棄しても再生は止まりません。**

```cpp
// これで最後まで再生される。ハンドルを受ける必要は無い
Tween::To(&value, 1.0f, 0.3f).SetLink(GetOwner());
```

トゥイーンは投げっぱなしで使うのが普通なので、そういう設計にしてあります。
途中で止めたいときだけハンドルを保持してください。

完了済み・キル済みのハンドルに対する操作はすべて安全な空振りになります。
世代番号で判別しているので、スロットが再利用されても**古いハンドルが別のトゥイーンを誤って殺すことはありません**。

### 一括で止める

```cpp
Tween::KillById("hud");            // SetId("hud") を付けたものを全部
Tween::KillByLink(GetOwner());     // この GameObject に紐づくものを全部
```

---

## 6. デバッグパネル

**Debug メニュー > Tween**（`EnginePanelGroup::Analysis`）で開けます。

- 再生中の本数 / スロット数 / 空きスロット数
- 一覧：種別（Tween / Sequence / Interval / Callback）、`SetId` の名前、進捗バー、ループ回数、link 先の名前
- `Kill All` ボタン、id / 種別でのフィルタ

**link が `none (!)` と表示されている行は、対象が破棄されたときに落ちる可能性があります。**
そこを見つけるためのパネルなので、演出を作ったらまず一度覗いてください。

---

## 7. 制約

- **メインスレッド専用**です（GameObject の値を書き換えるため）。
- シーン遷移時に `BaseScene::Finalize()` から全トゥイーンが破棄されます。前のシーンの演出は持ち越されません。
- トゥイーンの更新は `GameObjectManager::UpdateAll()` の**直前**に行われます。
  そのためコンポーネントの `Update()` は、同じフレームでトゥイーン後の値を読めます。

---

## 8. 実用例

### 被弾フラッシュ

手書きだと「メンバ変数 + Update 内の分岐 + 専用メソッド」が要りますが、1 文で済みます。

```cpp
void OnDamaged()
{
    TweenSequence()
        .Append(Tween::To<Vector4>(kBaseColor, kHitColor, 0.05f, SetColorFn()))
        .Append(Tween::To<Vector4>(kHitColor, kBaseColor, 0.20f, SetColorFn())
            .SetEase(EasingUtil::Type::EaseOutCubic))
        .SetLink(GetOwner())
        .SetId("damageFlash");
}
```

同じ演出が重なると色が競合するので、開始前に `Tween::KillById("damageFlash")` を呼んでおくと安全です。

### メニューのスライドイン

```cpp
menu->SetAnchoredPosition({ -400.0f, 0.0f });

Tween::To(&menuPosition_, Vector2{ 0.0f, 0.0f }, 0.35f)
    .SetEase(EasingUtil::Type::EaseOutCubic)
    .SetUpdateType(TweenUpdate::Unscaled)  // ポーズ中でも動く
    .SetLink(menu);
```

### スコアのカウントアップ

```cpp
const int from = displayScore_;
const int to = actualScore_;

Tween::To<float>(static_cast<float>(from), static_cast<float>(to), 0.6f,
    [this](float v) { displayScore_ = static_cast<int>(v); })
    .SetEase(EasingUtil::Type::EaseOutQuart)
    .SetLink(GetOwner());
```

### 敵の登場演出（EventBus と組み合わせる）

```cpp
void Start() override
{
    // 出現時に小さく潰れた状態から膨らむ
    auto* transform = Sibling<TransformComponent>();
    transform->Get().scale = { 0.1f, 0.1f, 0.1f };

    Tween::ScaleTo(GetOwner(), { 1.0f, 1.0f, 1.0f }, 0.3f)
        .SetEase(EasingUtil::Type::EaseOutBack);

    // 上下にゆっくり揺れ続ける
    Tween::MoveTo(GetOwner(), basePos + Vector3{ 0.0f, 0.3f, 0.0f }, 1.2f)
        .SetEase(EasingUtil::Type::EaseInOutSine)
        .SetLoops(-1, TweenLoop::Yoyo);
}
```

---

## まとめ

- `Tween::To(&値, 目標, 秒数)` が基本形。ハンドルは受けなくてよい
- **ポインタを渡すときは `SetLink()` を必ず付ける**
- 連続・同時の演出は `TweenSequence()` で組む（1 つの式の中で）
- ポーズ中も動かしたいものは `SetUpdateType(TweenUpdate::Unscaled)`
- 動かないときは **Debug > Tween** パネルを見る
