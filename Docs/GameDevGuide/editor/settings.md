# 設定

設定は置き場所が 3 つに分かれています。**どこに保存されるか**が違います。

| 何を決めるか | どこで | 保存先 |
|---|---|---|
| プロジェクト全体 | **Edit → Project Settings** | `Application/Config/EngineSettings/` |
| 操作の割り当て | **Help → キー操作を見る** | `EngineSettings/InputActions.json` |
| 見た目・描画 | **Engine Settings** | シーンの `_environment.json` ほか |

---

## Project Settings

**Edit → Project Settings** で開きます。

| 項目 | 中身 |
|---|---|
| 起動シーン | 最初に開くシーン |
| レイヤー | 当たり判定の分類（`Layers.json`） |
| 衝突マトリクス | **どのレイヤーとどのレイヤーが当たるか** |

### 衝突マトリクス

レイヤーの組み合わせごとに、当たるかどうかを表で決めます。

「弾（`Bullet`）は敵（`Enemy`）にだけ当たる」「敵同士はすり抜ける」といった制御が、スクリプトを書かずにできます。

スクリプトからも変えられます。

```cpp
Physics::SetLayerCollision(CollisionLayer::Bullet, CollisionLayer::Player, false);
```

---

## キー操作（Key Config）

**Help → キー操作を見る**で開きます。

操作の名前（`InputAction`）ごとに、キー・マウス・パッドの割り当てを変えられます。

!!! info "スクリプトは書き換えません"
    スクリプトは `InputAction::Jump` のように**名前**で書いてあるので、ここで割り当てを変えるだけで反映されます。

各操作には**場面**が決まっています。

| 場面 | いつ効くか |
|---|---|
| `Game` | ふつうに遊んでいる間 |
| `UI` | **UI を選んでいる間**（ボタンにフォーカスがある間） |
| `Editor` | エディタのあるビルドでだけ |

これがあるので、**決定ボタンでボタンを押した瞬間にキャラも跳ぶ**といった二重発火が起きません。

→ [入力アクション](../reference/input-actions.md)

---

## Engine Settings

描画まわりの設定です。左のツリーで分類されています。

| 分類 | 中身 |
|---|---|
| **General** | 衝突マトリクス・グリッド・既定の床 |
| **Rendering** | シェーディング・ポストエフェクト・描画手法 |
| **Editor** | エディタ自体の設定 |

ここで触る値の多くは **CVar** です。名前は `r.Bloom.Intensity` のような形をしています。

### CVar の持ち主は接頭辞で決まる

| 接頭辞 | 誰のものか | 保存先 |
|---|---|---|
| `r.` など | **シーンごと** | そのシーンの `_environment.json` |
| その他 | プロジェクト全体 | `Application/Config/` |

だから、**シーンごとに違う空・違う色味**を持たせられます。

### スクリプトから読み書きする

```cpp
float intensity = CVar::GetFloat("r.Bloom.Intensity", 1.0f);
CVar::SetFloat("r.Bloom.Intensity", 2.0f);
bool on = CVar::GetBool("r.AutoExposure.Enabled", true);
```

!!! warning "再生中に書いた値はプロジェクト設定に残りません"
    再生中の書き込みは、止めると元に戻ります。演出で一時的に変えるぶんには問題ありません。

---

## レイアウト

| 操作 | どこ |
|---|---|
| パネルを出す・隠す | **Window** メニュー |
| 全画面 | <kbd>Alt</kbd>+<kbd>Enter</kbd> |
| エディタ UI を隠す（ゲーム画面だけ見る） | <kbd>F11</kbd> |
| ゲーム画面だけの別ウィンドウ | **Window → ゲーム画面のみのウィンドウ** |
| レイアウトを元に戻す | **Window → Layout → レイアウトを初期化** |

レイアウトは自動で保存され、次に起動したときも同じ配置になります。
