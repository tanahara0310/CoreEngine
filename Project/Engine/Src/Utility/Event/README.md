# EventBus 使用方法

`EventBus` は、**発行側と購読側を互いに知らせずにゲーム内の出来事を伝える**ための仲介役です。

「敵が死んだ」という 1 つの事実に対して、スコア・SE・エフェクト・UI がそれぞれ独立に反応できるようにします。
弾のコードがスコアや UI の存在を知らなくて済むので、**機能を後から足したり消したりしても他が壊れません**。

---

## 1. 準備

### インクルード

```cpp
#include "Utility/Event/EventBus.h"
```

### イベント型を定義する

イベントは **ただの構造体** です。基底クラスの継承もマクロ登録も要りません。

```cpp
// game 側のヘッダ（例: Src/Events/GameEvents.h）にまとめて置くとよい
namespace MyGame
{
    /// @brief 敵が倒された
    struct EnemyDiedEvent {
        std::uint32_t enemyId = 0;
        CoreEngine::Vector3 position{};
        int score = 0;
    };

    /// @brief プレイヤーが被弾した
    struct PlayerDamagedEvent {
        float amount = 0.0f;
        float remainingHp = 0.0f;
    };
}
```

イベント型は **1 か所にまとめる** のが重要です。イベントの一覧＝ゲームの「出来事の一覧」になり、
どの機能がどこに繋がっているかを読み解く唯一の目次になります。

---

## 2. 購読する

```cpp
class ScoreComponent : public CoreEngine::IComponent {
public:
    const char* GetTypeName() const override { return "Score"; }

    void Start() override
    {
        // ★ 戻り値は必ず受けること（捨てると即座に購読解除される）
        subscription_ = CoreEngine::EventBus::GetInstance().Subscribe<MyGame::EnemyDiedEvent>(
            [this](const MyGame::EnemyDiedEvent& e) {
                score_ += e.score;
            });
    }

    int GetScore() const { return score_; }

private:
    CoreEngine::Subscription subscription_; // ← デストラクタで自動解除される
    int score_ = 0;
};
```

### `Subscription` を必ず保持すること

`Subscription` は **RAII ハンドル** です。破棄されると自動的に購読解除されます。

```cpp
// NG: その場で捨てられ、購読は即座に解除される（[[nodiscard]] で警告が出る）
EventBus::GetInstance().Subscribe<EnemyDiedEvent>(...);

// OK: メンバ変数に保持する
subscription_ = EventBus::GetInstance().Subscribe<EnemyDiedEvent>(...);
```

これは面倒に見えますが、**このシステムで唯一クラッシュし得るケース**
（破棄済みオブジェクトを掴んだラムダが呼ばれる）を型で防ぐための仕組みです。
`Subscription` をメンバに置いておけば、`GameObject` が `Destroy()` されたときに
必ず購読も切れます。

### 複数のイベントを購読する

`Subscription` を何本も並べる代わりに `SubscriptionBag` を使えます。

```cpp
class HudComponent : public CoreEngine::IComponent {
    void Start() override
    {
        auto& bus = CoreEngine::EventBus::GetInstance();

        bag_.Add(bus.Subscribe<MyGame::EnemyDiedEvent>(
            [this](const auto& e) { AddCombo(); }));

        bag_.Add(bus.Subscribe<MyGame::PlayerDamagedEvent>(
            [this](const auto& e) { SetHpBar(e.remainingHp); }));
    }

    CoreEngine::SubscriptionBag bag_; // 破棄時にまとめて解除される
};
```

---

## 3. 発行する

### `Publish` — 即座に配信

呼び出し元のスタック上で、その場で全ハンドラが走ります。

```cpp
CoreEngine::EventBus::GetInstance().Publish(
    MyGame::EnemyDiedEvent{ enemyId, position, 100 });
```

「弾が当たった → 敵を消す」のように、**その行を抜けた時点で結果が確定していてほしい** ものに使います。

### `Queue` — フレーム末にまとめて配信

```cpp
CoreEngine::EventBus::GetInstance().Queue(
    MyGame::EnemyDiedEvent{ enemyId, position, 100 });
```

UI 更新・SE・統計など、**今フレーム中に届けば順序はどうでもよい** ものに使います。

配信タイミングは `BaseScene::Update()` の中、**衝突判定の直後・`OnLateUpdate()` の直前**です。
そのため `Queue` されたイベントへの反応は、同じフレームの `LateUpdate` と描画に間に合います。

ハンドラの中から更に `Queue` した分は **次フレーム** に回ります（無限ループ防止のため意図的にそうしています）。

### どちらを使うか

| | `Publish` | `Queue` |
|---|---|---|
| 配信タイミング | 即座 | フレーム末（衝突判定の後） |
| ゲームロジックの結果に影響する | ○ | ×（1 段遅れる） |
| 大量に飛ばしても安全か | 入れ子が深くなり得る | ○ |
| 向いている用途 | ダメージ適用、破壊、状態遷移 | UI 更新、SE、エフェクト、統計 |

迷ったら `Publish` で構いません。`Queue` は「同じフレームに大量に飛ぶ」「反応が重い」場合の逃げ道です。

---

## 4. 実行順を指定する（priority）

同じイベントに複数が反応するとき、順序が問題になることがあります。

```cpp
// score は 10（先に走る）、UI は 0（後に走る）
scoreSub_ = bus.Subscribe<EnemyDiedEvent>([this](const auto& e) { score_ += e.score; }, 10);
hudSub_   = bus.Subscribe<EnemyDiedEvent>([this](const auto& e) { RefreshScoreText(); }, 0);
```

`priority` は **大きいほど先** に呼ばれます。同じ値なら購読した順です。

ただし、順序に頼る設計はそれ自体が結合なので、**基本は既定値（0）のまま**にしてください。
「スコアを更新してから UI がそれを読む」のような、どうしても避けられない依存にだけ使います。

---

## 5. ハンドラの中でやってよいこと

以下はすべて安全です。

```cpp
sub_ = bus.Subscribe<EnemyDiedEvent>([this](const auto& e) {
    bus.Publish(ComboIncreasedEvent{ ... });  // 別のイベントを発行する（OK）
    otherSub_ = bus.Subscribe<XxxEvent>(...); // 購読を追加する（OK。次の配信から有効）
    sub2_.Unsubscribe();                      // 購読を解除する（OK。即座に呼ばれなくなる）
    GetOwner()->Spawn<EffectObject>();        // オブジェクトを生成する（OK）
    GetOwner()->Destroy();                    // 自分を消す（OK。実体の破棄はフレーム末）
});
```

- 配信中に追加された購読は、**その配信には参加せず**次回から有効になります。
- 配信中に解除された購読は、**その瞬間から**呼ばれなくなります。
- ハンドラ同士が `Publish` を呼び合って入れ子が 32 段に達すると、打ち切ってエラーログを出します。

---

## 6. デバッグパネル

**Debug メニュー > Event Bus**（`EnginePanelGroup::Analysis`）で開けます。

- **Channels**: イベント型ごとの購読者数 / 前フレームの発行回数 / 累計発行回数
  - 購読者が **0 なのに発行されている**イベントはオレンジで `0 (!)` と表示されます。配線ミスの目印です。
- **Trace**: 直近 256 件の発行履歴（フレーム番号 / `[direct]` か `[queued]` か / 配信先の数 / 型名）
  - `Pause` で流れを止め、`filter` に文字列を入れて型名で絞り込めます。

疎結合にすると「誰が誰に反応したか」がコードから読めなくなります。
**このパネルがその代償を払うためのものなので、繋がらないときはまずここを見てください。**

---

## 7. 制約

### メインスレッド専用

`Publish` / `Queue` / `Subscribe` / `DispatchQueued` は、すべてゲームループのスレッドから呼んでください。
ロックを持たないのは `Publish` が毎フレーム数百回走るホットパスだからです。
Debug ビルドでは別スレッドからの呼び出しを検出してエラーログを出します。

`ThreadPool` の中で結果を通知したい場合は、結果を自前のキューに積んでおき、
**メインスレッドの `Update` から `Publish` してください。**

### 型名の表示は装飾付きになることがある

デバッグパネルの型名は `typeid(E).name()` 由来なので、MSVC では
`struct MyGame::EnemyDiedEvent` のような形で表示されます。表示専用の情報です。

### シーン遷移で全解除される

`BaseScene::Finalize()` から `EventBus::Clear()` が呼ばれ、全購読と未配信キューが破棄されます。
前のシーンのハンドラが次のシーンへ持ち越されることはありません。

---

## 8. 実用例：ShootingSampleScene に「スコア」を足す

### イベント定義

```cpp
// Src/Scenes/ShootingSampleScene/ShootingEvents.h
namespace ShootingSample
{
    struct EnemyDiedEvent {
        CoreEngine::Vector3 position{};
        int score = 100;
    };
}
```

### 発行側（弾）

`BulletComponent` はスコアも UI も SE も知りません。「敵が死んだ」とだけ言います。

```cpp
GetOwner()->GetColliders().SetOnEnter(
    [this](const CoreEngine::CollisionInfo& info) {
        if (info.other) {
            const auto pos = info.other->GetComponent<CoreEngine::TransformComponent>()->Get().translate;
            CoreEngine::EventBus::GetInstance().Queue(EnemyDiedEvent{ pos, 100 });
            info.other->Destroy();
        }
        GetOwner()->Destroy();
    });
```

### 購読側（スコア）

```cpp
class ScoreComponent : public CoreEngine::IComponent {
public:
    const char* GetTypeName() const override { return "Score"; }

    void Start() override
    {
        sub_ = CoreEngine::EventBus::GetInstance().Subscribe<EnemyDiedEvent>(
            [this](const EnemyDiedEvent& e) { score_ += e.score; });
    }

    int Get() const { return score_; }

private:
    CoreEngine::Subscription sub_;
    int score_ = 0;
};
```

### 購読側（エフェクト）— あとから足す

**既存のコードを 1 行も触らずに** 機能が増えます。ここが EventBus の本題です。

```cpp
class ExplosionSpawnerComponent : public CoreEngine::IComponent {
    void Start() override
    {
        sub_ = CoreEngine::EventBus::GetInstance().Subscribe<EnemyDiedEvent>(
            [this](const EnemyDiedEvent& e) { SpawnExplosionAt(e.position); });
    }
    CoreEngine::Subscription sub_;
};
```

同じように SE・カメラシェイク・コンボ表示・実績を足しても、`BulletComponent` は変わりません。
逆に「コンボ機能をやめる」ときも、そのコンポーネントを 1 つ消すだけで済みます。

---

## まとめ

- イベントは **ただの構造体**。`Subscribe<E>` / `Publish` / `Queue` の 3 つだけ覚えれば使える
- **`Subscription` は必ず変数に受ける**（捨てると即解除、保持すれば自動解除）
- 結果が即座に要るものは `Publish`、UI や演出は `Queue`
- 繋がらないときは **Debug > Event Bus** パネルを見る
- メインスレッド専用
