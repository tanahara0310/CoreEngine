# ポストプロセス層 リファクタリング計画

作成日: 2026-08-11
ブランチ: future/water
対象: `Engine/Src/Graphics/PostEffect/` 一式 + `Graphics/Render/Pass/PostEffectPass` + `RenderPipeline::AppendPostEffectPasses`
目的: **エフェクト層（ローカル露出 / モーションブラー / DoF / 本物のBloom / グレイン / ディザ）を投入できる土台を先に作る**

---

## 1. 背景

現行のポストプロセス層は「RenderGraph 化」「CVar による有効/無効の単一ソース化」「シェーダーリフレクションによるルートシグネチャ自動生成」まで到達しており、**土台の方向性は正しい**。

しかし抽象化が `Dispatch(inputSrv, outputUav, width, height)` という **1入力→1出力・同解像度・単一パス**に固定されているため、これから入れたいエフェクトの大半が表現できない。

| 入れたい機能 | 必要な形 | 現行の枠 |
|---|---|---|
| Bloom（ミップチェーン） | ダウン6段 + アップ6段 | ✗ |
| ローカル露出 | 輝度ピラミッド生成 + 合成（2〜3パス） | ✗ |
| 被写界深度 | CoC → 近/遠レイヤ分離 → 合成（4パス以上） | ✗ |
| モーションブラー | Velocity バッファを追加入力 | ✗ |
| フィルムグレイン | 1入力1出力 | ✓ |
| 出力ディザ | トーンマッパ内蔵 | ✓ |

**エフェクトを足す前に土台を直す**のが本計画の趣旨である。後から直すと 17 エフェクト全部を書き直すことになる。

---

## 2. 現状の構造

```
PostEffectManager                  RenderPipeline                 RenderGraph
  ├ effects_ (name → instance)       AppendPostEffectPasses()  →    AddPass × N
  ├ effectChain_ (name の配列) ────→    ping-pong 中間RT を           ↓
  └ effectPtrCache_ (有効のみ) ──→      連結して割り当て          PostEffectPass::Execute
                                                                    └ effect->Dispatch()
```

| 要素 | 場所 |
|---|---|
| エフェクト基底 | `PostEffect/Effect/PostEffectBase.h` |
| CS/PS 派生基底 | `PostEffectComputeBase` / `PostEffectGraphicsBase` |
| 登録とチェーン定義 | `PostEffectManager.cpp:57-103` |
| チェーン→ノード分解 | `RenderPipeline.cpp:681-728` |
| 実行 | `PostEffectPass.cpp:81-169` |
| 中間RT確保 | `RenderTargetManager.cpp:86-117` |
| 論理リソース名 | `FrameBlackboard.h:27-52` |

---

## 3. 問題一覧（レビュー結果）

| # | 重大度 | 問題 | 証拠 |
|---|---|---|---|
| P1 | **高** | `Dispatch` の署名が単一パスを強制。Bloom は内部に中間リソースを一切持たず、実質「半径制限つき1パスブラー」になっている | `PostEffectBase.h:42` / `Bloom.h:35-64` |
| P2 | **高** | 追加リソース（Depth/Velocity/履歴）を受け取る汎用の口が無い → フレームワーク側にエフェクト固有の分岐が生えている（開放閉鎖原則違反） | `PostEffectPass.cpp:101-108`（Outline 専用 dynamic_cast）/ `RenderPipeline.cpp:90-101`（LensFlare 専用関数） |
| P3 | **高** | HDR段 / LDR段が型で守られておらず、**ColorGrading・ChromaticAberration・Vignette がトーンマップ後にある**。UE とは逆で、S字カーブ通過後の色をいじっている | `PostEffectManager.cpp:82-100` |
| P4 | 中 | 中間RT が `R16G16B16A16_FLOAT` 固定 + `needsDepthStencil=true` 既定。**使わない D32 が1枚ごとに付く**（1080p で 8.3MB/枚） | `RenderTargetDescriptor.h:22,25` / `RenderTargetManager.cpp:94` |
| P5 | 中 | 中間RT が「有効エフェクト数 − 1」枚。作るだけで破棄しないので高水位のまま。5個有効で約125MB、17個で約400MB | `RenderPipeline.cpp:459` / `RenderTargetManager.cpp:86` |
| P6 | 低 | 同じエフェクトを2回チェーンに置けない（名前→インスタンス 1対1）。パラメータが全てグローバル CVar でシーン別ルック不可 | `PostEffectManager.cpp:105-117` |
| P7 | 低 | `ExecuteEffect` / `ExecuteEffectToBackBuffer`（名前で直接Draw）と RenderGraph 経由の二系統が併存 | `PostEffectManager.cpp:142-156` |
| P8 | 低 | `Update` がチェーン外エフェクトを探すのに毎フレーム線形探索（O(n×m)） | `PostEffectManager.cpp:200-217` |
| P9 | 低 | `ToneMapping.h` のコメント「チェーンの**最終段**で適用する」が実装と食い違い（実際は3番目で後ろに14個ある） | `ToneMapping.h:14` |

---

## 4. 目標アーキテクチャ

### 4.1 導入する4つの概念

| 概念 | 解決する問題 | 一言 |
|---|---|---|
| **Stage（段）** | P3 | エフェクトが「HDR段 / トーンマッパ / LDR段」のどれかを申告し、チェーン順を機械的に検証する |
| **FrameContext** | P2, P8 | ビュー行列・deltaTime・サービスを全エフェクトへ一律に配る。フレームワーク側の特殊分岐を根絶する |
| **ExtraInputs** | P2 | エフェクトが必要な論理リソース名（Depth / Velocity 等）を宣言し、Blackboard から自動解決させる |
| **GraphBuilder** | P1, P5 | エフェクトが**自分のパス群と一時リソースを RenderGraph へ積める**ようにする。本丸 |

### 4.2 レイヤ図（目標）

```
PostEffectManager        … 登録・検索のみ（責務縮小）
      │
PostEffectChain          … 順序・段の検証・Prepare/Build の駆動   ← 新規
      │
PostEffectBase           … GetStage / DeclareExtraInputs / PrepareFrame / BuildPasses
      │
PostEffectGraphBuilder   … CreateTransient / AddComputePass / AddGraphicsPass  ← 新規
      │
PostEffectTransientPool  … (w,h,format) キーの一時RTプール                     ← 新規
      │
RenderGraph              … 既存（バリア自動導出）
```

### 4.3 API 定義

#### Stage

```cpp
// PostEffect/Effect/PostEffectStage.h（新規）
/// @brief ポストエフェクトが属するパイプライン段
/// @details チェーンは SceneHDR → Tonemap → PostTonemap の順に単調でなければならない。
///          この順序は PostEffectChain::Validate() が検証する。
enum class PostEffectStage : uint8_t {
    SceneHDR = 0,   ///< トーンマップ前。光学現象・露出・グレーディング
    Tonemap,        ///< トーンマッパ本体。チェーン中ちょうど 1 つ
    PostTonemap,    ///< トーンマップ後。記録（グレイン）・出力・演出
};
```

#### フレーム文脈

```cpp
// PostEffect/Effect/PostEffectFrameContext.h（新規）
/// @brief 毎フレーム全エフェクトへ配られる文脈
/// @details エフェクト固有の情報注入をフレームワーク側に書かないための唯一の入口。
struct PostEffectFrameContext {
    const ViewInfo*       view        = nullptr; ///< 実際に描画に使われたビュー
    const FrameBlackboard* blackboard = nullptr;
    EngineSystem*         services    = nullptr; ///< GetService<T>() で大気などへ到達する
    float                 deltaTime   = 0.0f;
    uint32_t              width       = 0;
    uint32_t              height      = 0;
};
```

#### 追加入力の宣言

```cpp
/// @brief エフェクトが要求する追加入力 1 件
struct PostEffectInputBinding {
    const char* slot;              ///< シェーダー側リソース名（例 "gSceneDepth"）
    const char* logicalName;       ///< Blackboard 論理名（FrameBlackboard::SceneDepth 等）
    bool        required = true;   ///< 解決できない場合にエフェクトごとスキップするか
};
```

#### 基底クラスへの追加

```cpp
class PostEffectBase {
public:
    /// @brief 所属する段を返す（既定は演出系を想定して PostTonemap）
    virtual PostEffectStage GetStage() const { return PostEffectStage::PostTonemap; }

    /// @brief 追加で読みたい論理リソースを申告する
    virtual void DeclareExtraInputs(std::vector<PostEffectInputBinding>& /*out*/) const {}

    /// @brief フレーム開始時に呼ばれる。行列・時間・外部システムの取り込みはここで行う
    /// @note 旧 Update(float) はこれに統合する
    virtual void PrepareFrame(const PostEffectFrameContext& /*ctx*/) {}

    /// @brief 自分のパス群を RenderGraph へ積む
    /// @details 既定実装は「入力1・出力1の単一パス」を積み、従来の Dispatch/Draw を呼ぶ。
    ///          既存エフェクトはこれを上書きしないかぎり無改修で動く。
    virtual void BuildPasses(PostEffectGraphBuilder& builder);
};
```

#### GraphBuilder（本丸）

```cpp
// PostEffect/Graph/PostEffectGraphBuilder.h（新規）

/// @brief グラフ内リソースへの軽量参照（実体は Blackboard 論理名）
class PostEffectResourceRef {
public:
    const std::string& Name() const { return name_; }
    bool IsValid() const { return !name_.empty(); }
private:
    friend class PostEffectGraphBuilder;
    std::string name_;
};

/// @brief パス記録ラムダへ渡る実行時文脈
struct PostEffectPassRecordContext {
    ID3D12GraphicsCommandList*  cmdList = nullptr;
    const ViewInfo*             view    = nullptr;
    std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> reads;    ///< 宣言順に解決済み
    D3D12_GPU_DESCRIPTOR_HANDLE outputUav{};           ///< CS パスのみ
    uint32_t width = 0;
    uint32_t height = 0;
};

using PostEffectRecordFn = std::function<void(const PostEffectPassRecordContext&)>;

class PostEffectGraphBuilder {
public:
    /// @brief チェーンの現在の色（前段の出力）
    PostEffectResourceRef Input() const;

    /// @brief Blackboard の既存論理リソースを読む（DeclareExtraInputs で申告済みのもの）
    PostEffectResourceRef Read(const char* logicalName);

    /// @brief 一時リソースを確保する
    /// @param debugName ノードエディタ表示用の短い名前
    /// @param resolutionScale 1.0 = フル解像度、0.5 = 1/2
    /// @param format DXGI_FORMAT_UNKNOWN のとき段の既定フォーマット
    /// @note 同一フレーム内でのみ有効。次フレームには持ち越せない
    PostEffectResourceRef CreateTransient(
        const char* debugName,
        float resolutionScale = 1.0f,
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

    /// @brief コンピュートパスを積む
    void AddComputePass(
        std::string_view name,
        std::initializer_list<PostEffectResourceRef> reads,
        PostEffectResourceRef write,
        PostEffectRecordFn record);

    /// @brief グラフィクスパスを積む
    void AddGraphicsPass(
        std::string_view name,
        std::initializer_list<PostEffectResourceRef> reads,
        PostEffectResourceRef write,
        PostEffectRecordFn record);

    /// @brief このエフェクトの最終出力を宣言する（省略時は最後の write）
    void SetOutput(PostEffectResourceRef ref);
};
```

#### 利用例（ミップチェーン Bloom）

```cpp
void Bloom::BuildPasses(PostEffectGraphBuilder& b)
{
    constexpr int kMipCount = 6;
    PostEffectResourceRef mips[kMipCount];
    for (int i = 0; i < kMipCount; ++i) {
        mips[i] = b.CreateTransient("Mip", 1.0f / static_cast<float>(1 << (i + 1)));
    }

    PostEffectResourceRef src = b.Input();
    for (int i = 0; i < kMipCount; ++i) {                       // ダウンサンプル
        b.AddComputePass("Bloom.Down" + std::to_string(i), { src }, mips[i],
            [this](const PostEffectPassRecordContext& c) { RecordDownsample(c); });
        src = mips[i];
    }
    for (int i = kMipCount - 2; i >= 0; --i) {                  // アップサンプル合成
        b.AddComputePass("Bloom.Up" + std::to_string(i), { src, mips[i] }, mips[i],
            [this](const PostEffectPassRecordContext& c) { RecordUpsample(c); });
        src = mips[i];
    }
    b.AddComputePass("Bloom.Composite", { b.Input(), src }, b.CreateTransient("Result"),
        [this](const PostEffectPassRecordContext& c) { RecordComposite(c); });
}
```

#### チェーン

```cpp
// PostEffect/Effect/PostEffectChain.h（新規・PostEffectManager から分離）
class PostEffectChain {
public:
    void SetOrder(std::vector<std::string> order);

    /// @brief 有効エフェクトを解決し PrepareFrame を配る
    void Prepare(const PostEffectFrameContext& ctx);

    /// @brief 有効エフェクトの BuildPasses を順に呼び、入出力を連結する
    void Build(PostEffectGraphBuilder& builder);

    /// @brief 段の単調性と Tonemapper の単一性を検証する
    /// @return 違反があれば false（outError に理由）
    bool Validate(std::string* outError) const;
};
```

---

## 5. 段の再配置（Phase 1 で行う変更）

| エフェクト | 現在の位置 | 新しい段 | 理由 |
|---|---|---|---|
| Bloom | HDR（3番目より前） | `SceneHDR` | 変更なし |
| LensFlare | HDR | `SceneHDR` | 変更なし |
| **ColorGrading** | **ToneMapping の後** | **`SceneHDR`（ToneMapping 直前）** | S字カーブ通過前の色をいじるべき。3D LUT 焼き込みの前提でもある |
| **ChromaticAberration** | ToneMapping の後 | **`SceneHDR`** | レンズの光学現象なので減光・分散はトーンマップ前 |
| **Vignette** | ToneMapping の後 | **`SceneHDR`** | 同上（口径食は露光量の減衰） |
| ToneMapping | 3番目 | `Tonemap` | 段の境界。常時有効・チェーン中1つ |
| FadeEffect / Shockwave / Blur / RadialBlur / Random / RasterScroll / Sepia / Invert / GrayScale / Dissolve | LDR | `PostTonemap` | 演出系。表示色に対して効くのが正しい |
| Outline | LDR | `PostTonemap` | 深度を追加入力として要求する実証対象 |
| FullScreen | チェーン外 | — | 表示専用 |

新しいチェーン:

```
Bloom → LensFlare → ChromaticAberration → Vignette → ColorGrading → [ToneMapping]
      → FadeEffect → Shockwave → Blur → Random → RadialBlur → RasterScroll
      → Sepia → Invert → GrayScale → Outline → Dissolve
```

> **注意**: ColorGrading を HDR 側へ移すと**見た目が変わる**。`contrast` / `gamma` / `exposure` が 0〜1 に収まらない値へ効くようになるため、CVar 既定値の再調整が必要。これが本リファクタリング唯一のユーザー体感変化である。

### 5.1 シェーダー側の HDR 適合性（実地調査結果 2026-08-11）

段を移す 3 本のシェーダーを確認した結果、**ColorGrading だけが HDR 非対応**だった。

| シェーダー | HDR 安全か | 根拠 |
|---|---|---|
| `Vignette.CS.hlsl` | **安全** | `color.rgb *= vignette` の純粋な乗算のみ。範囲仮定なし |
| `ChromaticAberration.CS.hlsl` | **安全** | RGB を別座標から `Load` するだけの再サンプル。範囲仮定なし |
| `ColorGrading.CS.hlsl` | **不可（要改修）** | 下記3点 |

ColorGrading の要改修点:

| 行 | 問題 | 対処 |
|---|---|---|
| 127 | `col = saturate(col)` — **HDR レンジを丸ごと 1.0 へ潰す**。このまま SceneHDR 段へ移すとトーンマッパへ入る前に全ハイライトが消滅する | `col = max(col, 0.0f)` へ置換（負値のみ除去） |
| 120 | `col = (col - 0.5f) * contrast + 0.5f` — ピボット 0.5 は LDR 前提 | 中間グレー `0.18` をピボットにする |
| 88-89 | `smoothstep(0.0, 0.5, lum)` / `smoothstep(0.5, 1.0, lum)` — HDR では輝度が容易に 1 を超えるため**全画素がハイライト扱い**になる | `lumN = lum / (lum + 1.0)` で単調に 0〜1 へ正規化してから閾値判定 |

HSV 経路（113-117行）は `hsv.z` が 1 を超えても破綻しないため改修不要。

---

## 6. フェーズ計画

### Phase 0: 現状固定（回帰基準の作成）

| 項目 | 内容 |
|---|---|
| 目的 | Phase 1 の見た目変化を「意図した変化」と「事故」に切り分けられるようにする |
| 作業 | ① 代表構図3点のスクリーンショットを `Docs/Engine/Graphics/PostProcess/baseline/` に保存（昼・夕・夜）<br>② `CVars.json` の現在値を控える<br>③ GPU タイミングの PostProcess カテゴリ合計を記録 |
| 受け入れ条件 | 3構図とタイミング値が残っている |

### Phase 1: 段（Stage）の導入とチェーン再配置 — 小

| 項目 | 内容 |
|---|---|
| 新規 | `PostEffectStage.h` |
| 変更 | 全17エフェクトに `GetStage()` を実装 / `PostEffectManager.cpp` のチェーン順を §5 の通りに変更 / `ToneMapping.h:14` の誤コメント修正 |
| 追加 | `PostEffectChain::Validate()`（この時点では Manager 内の関数でよい）。違反時は `assert` + ログ1行 |
| 受け入れ条件 | ① 起動ログに `[PostEffect] chain validated: SceneHDR=5 Tonemap=1 PostTonemap=11` が出る<br>② チェーン配列をわざと壊すと assert で落ちる<br>③ Phase 0 の3構図と比較し、**変化しているのが ColorGrading 由来の色だけ**であること |
| リスク | ColorGrading の既定値調整が要る。ここで絵を作り込まないこと（Phase 完了の判定が曖昧になる） |

### Phase 2: フレーム文脈の一本化 — 小

| 項目 | 内容 |
|---|---|
| 新規 | `PostEffectFrameContext.h` |
| 変更 | `PostEffectBase::PrepareFrame` 追加 / `Update(float)` を PrepareFrame へ統合 / `Outline` の深度クリップ面設定を `PrepareFrame` へ移動 / `LensFlare` の太陽投影を `PrepareFrame` へ移動 |
| 削除 | `PostEffectPass.cpp:101-108` の Outline 分岐 / `RenderPipeline.cpp:90-101` の `UpdateLensFlareSunPosition` / `PostEffectManager::Update` の二重ループ |
| 受け入れ条件 | **`PostEffectPass.cpp` と `RenderPipeline.cpp` からエフェクト具象クラスの `#include` が消えていること**（`Outline.h` / `LensFlare.h`）。これが客観的なゴール判定になる |

### Phase 3: 追加入力の宣言API — 中

| 項目 | 内容 |
|---|---|
| 新規 | `PostEffectInputBinding` |
| 変更 | `PostEffectPass::DeclareResources` が `DeclareExtraInputs` の申告分も `builder.Read()` する / `Execute` が Blackboard から解決して `reads[]` として渡す |
| 方針 | `required=true` の入力が解決できないエフェクトは**その場でスキップし、警告を1回だけ出す**（クラッシュさせない） |
| 受け入れ条件 | ① Outline が `FrameBlackboard::SceneDepth` を宣言経由で受け取れている<br>② 存在しない論理名を要求するテストエフェクトを足しても落ちず、警告が1回だけ出る<br>③ ノードエディタに Outline → SceneDepth の依存エッジが現れる |
| これで可能になるもの | **モーションブラー**（`GBufferMotionVector`）、**DoF**（`SceneDepth`） |

### Phase 4: マルチパス化 — 大（本丸）

| 項目 | 内容 |
|---|---|
| 新規 | `PostEffectGraphBuilder` / `PostEffectTransientPool` / `PostEffectChain`（Manager から分離） |
| 変更 | `PostEffectBase::BuildPasses` 追加。**既定実装が現行の単一パスを積む**ので既存エフェクトは無改修 / `RenderPipeline::AppendPostEffectPasses` が Chain へ委譲 / `PostEffectPass` をサブパスプールから供給（毎フレーム `make_unique` を廃止） |
| 実証 | Bloom をミップチェーン化（Down×6 → Up×6 → Composite）し、閾値を撤廃 |
| 受け入れ条件 | ① ノードエディタに `Bloom.Down0..5` / `Bloom.Up0..4` / `Bloom.Composite` が並び、バリアが自動導出されている<br>② **改修していない16エフェクトが従来どおり動く**<br>③ Bloom の広がりが Phase 0 のスクショより明確に大きい |
| リスク | `PostEffectBase` に仮想関数を追加するので **vtable レイアウトが変わる**。過去にヒープ破損（`c0000374`）を起こした ODR 事故と同型なので、**このフェーズは必ずクリーンビルドする**（`build-system-gotchas` 参照） |
| これで可能になるもの | **ローカル露出**、**DoF**、**本物の Bloom** |

### Phase 5: リソース最適化 — 小

| 項目 | 内容 |
|---|---|
| 変更 | 中間RT の記述子を `needsDepthStencil = false` に / ping-pong を 2枚固定 + transient プール化 / 段ごとの既定フォーマット |
| 段別フォーマット | `SceneHDR` = `R16G16B16A16_FLOAT`（据え置き）<br>`PostTonemap` = `R11G11B10_FLOAT`（**`R8G8B8A8_UNORM` 化は出力ディザ実装後**。先に落とすとバンディングが出る） |
| 受け入れ条件 | エフェクト5個有効時の RT 使用量が約125MB → 約50MB 以下（`RenderTargetManager` にログを1行足して実測） |

### Phase 6: API一本化と文書更新 — 小

| 項目 | 内容 |
|---|---|
| 変更 | `ExecuteEffect` / `ExecuteEffectToBackBuffer` を撤去し、`GameOutputWindow` は `FullScreen` を直接保持する形へ |
| 文書 | 本計画書 §9 に実施結果を追記 / `Docs/Engine/Editor/CVar_Design.md` から参照を張る |

---

## 7. 不変条件

リファクタリング後に**壊してはいけない**規約。

1. **段の単調性**: チェーンは `SceneHDR` → `Tonemap` → `PostTonemap` の順にのみ並ぶ。逆行は `Validate()` が拒否する
2. **トーンマッパはちょうど1つ**、かつ常時有効（`IsAlwaysEnabled()`）
3. **transient は同一フレーム限り**。次フレームへ持ち越すものは Blackboard の名前付きリソースにする（TAA 履歴と同じ扱い）
4. **`BuildPasses` の中で GPU コマンドを積んではいけない**。記録は必ず `record` ラムダの中で行う（グラフのバリア導出より前に走るため）
5. **必須入力が解決できないエフェクトはスキップする**。クラッシュも黒画面も禁止
6. **CVar 名は変更しない**。`CVars.json` の互換を保つ（`r.<Effect>.Enabled` と `r.<Effect>.*`）
7. **PostEffectPass / RenderPipeline は具象エフェクトを知らない**。エフェクト固有の分岐を書きたくなったら、それは `PrepareFrame` か `DeclareExtraInputs` の設計漏れである

---

## 8. スコープ外（今回やらないこと）

| 項目 | 理由 |
|---|---|
| Post Process Volume 相当（シーン/エリア別ルック） | パラメータの所有者を CVar からインスタンスへ移す大改修になる。必要になってから |
| 同一エフェクトの複数インスタンス | 上と同根 |
| OCIO / ワーキングカラースペース（ACEScg） | 段が整理された後なら後付けできる |
| エフェクトのデータドリブン定義（JSON からチェーン構築） | `SetEffectChain` が既にあるので必要時に接続するだけ |

---

## 9. エフェクト層との対応

本計画の完了後に投入する順序。

| エフェクト | 必要フェーズ | 備考 |
|---|---|---|
| 出力ディザ | Phase 1 | トーンマッパ内蔵。数十行 |
| フィルムグレイン（輝度依存） | Phase 1 | 1入力1出力なので現行枠でも可 |
| モーションブラー | Phase 3 | `GBufferMotionVector` を追加入力 |
| ローカル露出 | Phase 4 | 輝度ピラミッド + 合成 |
| Bloom ミップチェーン化 | Phase 4 | Phase 4 の実証対象そのもの |
| 被写界深度 | Phase 4 | CoC + レイヤ分離 |

---

## 9.1 エフェクト層の実装状況（リファクタリング完了後）

| # | エフェクト | 状態 | 段 | 既定 |
|---|---|---|---|---|
| 1 | 出力ディザ | **完了**（2026-08-12） | トーンマッパ内蔵 | 常時・CVar なし |
| 2 | フィルムグレイン | **完了**（2026-08-12） | PostTonemap | **有効**（`r.FilmGrain.Enabled = true`） |
| 3 | モーションブラー | 未着手 | SceneHDR | — |
| 4 | ローカル露出 | 未着手 | SceneHDR | — |
| 5 | 被写界深度 | 未着手 | SceneHDR | — |

### 出力ディザ

`ToneMapping.CS.hlsl` の出力直前で、sRGB 空間に寄せてから 1LSB 未満のノイズを足す。

**適用位置の教訓**: 当初 `FullScreen.PS`（8bit 書き込みの直前）へ入れたが、**エディタの Game ビューは `PostEffectFinal` を `ImGui::Image` で直接表示するためそこを通らない**。`FullScreen.PS` はバックバッファ経路（`BackBufferPass` / `GameOutputWindow`）専用である。**全表示経路に効かせる処理はチェーン内に置くこと。**

**色空間の教訓**: 丸めが起きるのは sRGB エンコード後。リニア空間で 1/255 を足すと sRGB カーブが暗部を引き伸ばすため、暗部だけ 1LSB を大幅に超えてノイズが見える。

**検証の限界（記録）**: 1LSB 未満の効果は、このシーン・このカメラでは画像から検証できなかった。
- 画角に大きな滑らかグラデーション（空）が無い → バンディング指標が測れない
- ゲーム画面に静止領域が無い（草・水面が常時動く）→ 画素単位の前後比較が成立しない（対照群でも一致率 21%）

そこで振幅を一時的に 32 倍にして経路だけを切り分けた（ゲーム画面は一致 21.4%→6.5%、UI 領域は 100% 一致のまま）。**実際にバンディングが減ったかは未検証。** 今後この種の微細な効果を検証するには、**空を大きく映す検証用カメラ位置**が要る。

### フィルムグレイン

演出用ノイズ（`Random`）とは別エフェクトとして新設。`Random` も輝度依存グレインの実装を持つが、寿命も調整軸も違うため共有すると片方の既定値変更でもう片方が壊れる。

UE と同じ **3 帯モデル**（シャドウ / 中間調 / ハイライトで強度を変える）。`Random` の線形な `1-luminance` より銀塩の挙動に近い。

| CVar | 既定 | 根拠 |
|---|---|---|
| `r.FilmGrain.Intensity` | 0.030 | 255 階調で ±7.6 相当。平坦な面の質感には十分で模様には埋もれる |
| `IntensityShadows` / `Midtones` / `Highlights` | 1.0 / 0.7 / 0.2 | 実フィルムは飽和側で粒が目立たない |
| `GrainSize` | 1.0 px | |
| `ChromaAmount` | 0.10 | 色が強いとフィルムでなくデジタルのセンサーノイズに見える |

チェーン順はセピア・モノクロより**後**。先に乗せると粒まで脱色される。

**検証**: PNG サイズが強度に単調増加（OFF 1808KB → 既定 1918KB → 強調 2016KB。ノイズは圧縮しにくい）。強調版で輝度依存が目視で確認でき、木陰は粒が強く明るい砂と水面はほぼクリーン。UI 領域は全構成で 100% 一致。

**未確認**: TAA との相性。現在 `r.TAA.Enabled = false` のため影響を受けていないが、**有効にする際は粒が履歴で平均化されて消えないか確認が要る**（過去に「TAA で水面の泡が溶ける」同型の問題あり）。

---

## 10. 実施結果

### Phase 0（2026-08-11 完了）

`Docs/Engine/Graphics/PostProcess/baseline/` に 3 構図と `CVars.baseline.json` を保存した。

> **注意（2026-08-11）**: baseline の PNG は Phase 2 作業中に**意図的に削除された**（リポジトリを画像で膨らませないため）。数値の比較結果は下表に残してあるので結論は失われていない。**検証用の一時画像は Docs 配下に置かず、作業用の一時ディレクトリへ出すこと。**

**検証環境で判明した事実（今後の全フェーズで効く）**

| # | 事実 | 影響 |
|---|---|---|
| V1 | **実行時の設定・ログは exe と同じ場所（`generated/CoreEngine/outputs/<Config>/`）配下を読み書きする。ソースツリー側の `Application/Config/...` ではない** | ソースツリーの CVars.json を書き換えても実行結果は変わらない。3構図の太陽方向差し替えがこれで無効化され、baseline 3枚は**同一の光条件**になった |
| V2 | 実行時ログは `outputs/<Config>/Cache/logs/<Category>/<Category>_YYYYMMDD_HHMMSS.log` に出る | 検証は「起動→WM_CLOSE→新規ログを grep」の形にする |
| V3 | **spdlog は正常終了時にしかフラッシュされない**。`Stop-Process -Force` で落とすとログが丸ごと消える | 検証スクリプトは必ず `PostMessage(hwnd, WM_CLOSE)` で終了させる |
| V4 | **Bloom / ColorGrading / Vignette / ChromaticAberration / LensFlare は CVar 既定値が全て `false`** | 既定構成では ToneMapping しか走らない。Phase 1 の段の移動は**既定構成では見た目に一切影響しない**ため、回帰確認は「ベースラインと一致すること」で行える |

V4 により Phase 1 の検証は 2 本立てになる。
- **A: 回帰** … 既定構成でベースラインと一致すること（移動が何も壊していない証明）
- **B: HDR 正当性** … SceneHDR 段の 4 エフェクトを全て ON にして白飛び・平坦化が起きないこと（HDR 対応が効いている証明）

### Phase 1（2026-08-11 完了）

**変更ファイル**: 新規 1（`PostEffectStage.h`）+ 変更 11（基底 / 6エフェクトのヘッダ / Manager / ColorGrading.CS.hlsl / vcxproj）、128 行追加。

**検証結果**

| # | 検証 | 手段 | 結果 |
|---|---|---|---|
| 1 | 段の集計が意図どおり | 起動ログ | **PASS** `[PostEffect] chain validated: SceneHDR=5 Tonemap=1 PostTonemap=11`（17 個の内訳が完全一致） |
| 2 | 回帰（既定構成で絵が変わらない） | 砂の固定領域 200×100px の平均 RGB をベースラインと比較 | **PASS** 差 **0.10 / 0.06 / 0.11（255 中）= 0.04%**。※画面全体だと −12.3 出るが、これは水面と泡のアニメーションによる構図差で、固定領域比較で切り分け済み |
| 3 | HDR 正当性（SceneHDR 段を全 ON） | 白飛び率の実測 | **PASS** `WhitePct = 0.000%`。旧コードのままなら `saturate()` で全画素が 1.0 に張り付き、ACES 通過後に一様なグレーになるはずだった |
| 4 | 誤配置を検出できる | ToneMapping をチェーン先頭へ故意に挿入してログ確認 | **PASS** 逆行と重複の**両方**を検出<br>`[error] 段が逆行しています: Bloom は SceneHDR ですが直前の ToneMapping は Tonemap でした`<br>`[error] Tonemap 段はちょうど 1 つでなければなりません（現在 2 個）` |

**Phase 1 で判明した追加事項**

| # | 内容 |
|---|---|
| V5 | **Development 構成は `NDEBUG` のため `assert` が無効**。段の違反はログに出るだけでプロセスは継続する。落として気付かせたい場合は Debug 構成で走らせること |
| V6 | **Vignette の既定 `intensity = 0.8` は旧配置（トーンマップ後）向けの値**。SceneHDR 段ではリニア値への乗算がトーンカーブを通るため効きが強く、4 エフェクト全 ON で画面の **2.88% が純黒に潰れる**。既定 OFF なので出荷時の絵には影響しないが、ヴィネットを使うときは既定値の見直しが要る |

### Phase 2（2026-08-11 完了）

**新設**: `PostEffectFrameContext.h`（view / sunDirection / deltaTime）。
**基底**: `PostEffectBase::Update(float)` を廃止し `PrepareFrame(const PostEffectFrameContext&)` に統一。

**設計判断**: 文脈には**サービス（Manager のポインタ）ではなくデータを載せる**。`AtmosphereManager*` を渡すとポストエフェクト層が大気システムへ依存するため、必要なのが太陽の向きなら向きだけを渡す。

**呼び出し点の移動**: `EngineSystem::BeginFrame` → `EngineSystem::ExecuteRenderPipeline` のビュー確定直後（View ループの手前）。`RenderPipeline::PrepareFrame` は View ごとに呼ばれるため、そこへ置くと補助ビューの行列でエフェクトの状態が上書きされる。

**削除したもの**

| 対象 | 内容 |
|---|---|
| `PostEffectPass.cpp` | Outline 専用の `dynamic_cast` 分岐（8行）＋ `Outline.h` / `PostEffectNames.h` / `ViewInfo.h` / `PostEffectManager.h` の include |
| `PostEffectPass::Execute` | 使われていない `context.postEffectManager` の null チェック |
| `RenderPipeline.cpp` | `UpdateLensFlareSunPosition`（45行）＋ `LensFlare.h` の include |
| `PostEffectManager::Update` | チェーン外エフェクトを毎フレーム線形探索する O(n×m) の二重ループ |
| `PostEffectManager::ExecuteEffect` | 呼び出し元ゼロの死にコード |

**汎用化・最適化**

| 対象 | 内容 |
|---|---|
| `prepareCache_` 新設 | 有効エフェクト（チェーン内＋外）を CVar 変更時にだけ作り直し、毎フレームの探索と map ルックアップをゼロにした |
| `"PostEffectIntermediate"` の三重定義 | `PostEffectPass.cpp` のローカル定数と `FrameBlackboard.cpp` の文字列リテラルを廃し、`RenderTargetNames::PostEffectIntermediatePrefix` に一本化。**接頭辞を変えると名前解決だけが静かに壊れる状態**だった |
| `BackBufferPass.cpp` | `"FullScreen"` の直書きを `PostEffectNames::FullScreen` へ |
| 4 エフェクト | FadeEffect / Shockwave / RasterScroll / Random の `Update(float)` を `PrepareFrame` へ移行（時間累積の入口も 1 本化） |

**受け入れ条件の確認**

| # | 検証 | 結果 |
|---|---|---|
| 1 | `PostEffectPass.cpp` / `RenderPipeline.cpp` からエフェクト具象クラスの include が消えること | **PASS** `Outline.h` / `LensFlare.h` とも参照ゼロ（残る `PostEffectManager.h` は汎用 API の利用） |
| 2 | ビルドが通ること | **PASS** exit 0・警告なし |
| 3 | 段の検証ログが Phase 1 と同じであること | **PASS** `chain validated: SceneHDR=5 Tonemap=1 PostTonemap=11` |
| 4 | 実機で正常に描画されること | **PASS**（定性）空・水面・地形が正しい露出で描画。`PrepareFrame` の呼び出し点を移した後も自動露出の順応が効いている（deltaTime が届かなければ順応が初期輝度で止まり露出が破綻する） |
| 5 | 画素比較による回帰確認 | **未実施**。参照画像を消失し、かつ再撮影時にエディタカメラが別構図になっていたため数値比較ができなかった |

**次の汎用化候補（Phase 4 と同時に実施することに決定）**

`ScreenParams`（`screenWidth` / `screenHeight` / `pad[2]`）と `screenParamsCB_` / `mappedScreenParams_` / `UpdateScreenConstantBuffer()` が **15 エフェクトに完全同一のまま重複**している（ToneMapping だけ `exposureEV` を足した別レイアウト）。`PostEffectComputeBase` へ引き上げれば約 250 行が消える。**Phase 4 で基底クラスを `BuildPasses` 対応へ作り替えるため、同じ 30 ファイルを二度触らないよう Phase 4 と同時に行う。**

### Phase 3（2026-08-11 完了）

**追加**: `PostEffectInputBinding`（`slot` / `logicalName` / `required`）と `PostEffectBase::DeclareExtraInputs()`。
エフェクトが「自分に必要なリソース」を宣言し、`PostEffectPass` がそれを RenderGraph へ `Read` として登録したうえで Blackboard から解決して渡す。

**解消した隠れ依存**

`Outline` は深度を `directXCommon_->GetDepthStencilSRV()` から**直接**読んでいた。RenderGraph に宣言が無いため**状態遷移も実行順も保証されておらず**、たまたま動いていただけの状態だった。これを宣言経由へ移行。ポストエフェクト層を全数調査した結果、engine 側のリソースを直接読んでいたのは **Outline のみ**で、他 16 エフェクトに同種の問題は無かった。

**実装中に作り込んで直したバグ**

当初は「必須入力が解決できないとき `PostEffectPass::Execute` で早期 return する」実装にした。これは **出力リソースが書かれないまま次段がそれを読む**ため、画面が丸ごと消える（実測で Game ビューが空になった）。不変条件「クラッシュも黒画面も出さない」に反する。

対処として判定を**パス生成時**へ移した。`RenderPipeline::AppendPostEffectPasses` が必須入力の有無を Blackboard に問い合わせ、揃わないエフェクトは**チェーンそのものから外す**。前段の出力が次段の入力へ直結するため画像が途切れない。`Execute` 側のチェックは「グラフ構築後に Blackboard が変わった」異常検知として残し、ログを Warning から **Error** へ変更した。

**検証結果**

| # | 検証 | 結果 |
|---|---|---|
| 1 | Outline が宣言経由で深度を受け取る | **PASS** 岩・草・幹・波打ち際に輪郭線を確認（未解決なら丸ごとスキップされるので、線が出ること自体が解決の証明になる） |
| 2 | 解決できない入力で警告が 1 回だけ出る | **PASS** 25 秒間の全フレームで 1 行のみ |
| 3 | 解決できない入力でクラッシュしない | **PASS** 正常終了 |
| 4 | 解決できない入力で画面が壊れない | **当初 FAIL（96KB の空画像）→ チェーン構築時除外へ変更して PASS（1856KB・シーンは正常で輪郭線だけ消える）** |
| 5 | 段の検証ログが維持されている | **PASS** `chain validated: SceneHDR=5 Tonemap=1 PostTonemap=11` |

**これで可能になったもの**: モーションブラー（`GBufferMotionVector`）と DoF（`SceneDepth`）が、フレームワークを一切触らずに追加できる。

### Phase 4（2026-08-12 完了）

3 段階に分けて実施した。

#### 4a: グラフ基盤

**新設**: `PostEffectGraphBuilder`（`CreateTransient` / `AddComputePass` / `AddGraphicsPass` / `ChainOutput`）、`PostEffectTransientPool`、`PostEffectStep`。

- `PostEffectBase::BuildPasses` の**既定実装が従来の単一パスを積む**ので、上書きしない 16 エフェクトは 1 行も変えずに動く
- `PostEffectPass` は「どのエフェクトか」を知らず、`PostEffectStep` を 1 つ実行するだけのノードになった。**ステップはコピーして保持する**（`postEffectSubpasses_` の再確保でポインタがダングリングするため）
- 出力先の解決から接頭辞の文字列パースが消えた（論理名＝登録名なので `GetRenderTarget(名前)` で引ける）
- 一時ターゲットは `needsDepthStencil = false`。中間 1 枚ごとに付いていた未使用の D32（1080p で 8.3MB/枚）が無くなった

#### 4b: ScreenParams の基底への引き上げ

15 エフェクトに完全同一で重複していた `ScreenParams` 構造体・`screenParamsCB_`・`mappedScreenParams_`・`UpdateScreenConstantBuffer()` を `PostEffectComputeBase` へ集約（`ScreenSizeConstants` / `UpdateScreenSizeConstants` / `GetScreenSizeCbAddress`）。**約 345 行削減**。ToneMapping だけは `exposureEV` を持つ別レイアウトなので対象外。

> **踏んだ罠（重要）**: 機械的置換のスクリプトで**正規表現の非貪欲マッチが閉じ括弧を取り違え、30 ファイルを壊した**。
> - `struct ScreenParams \{.*?\};` は `float pad[2] = { 0.0f, 0.0f };` の**行末 `};`** で止まる
> - `\{.*?^[ \t]*\}` は **if 文の閉じ括弧**で止まる
>
> 対処は「閉じ括弧を**開き括弧と同じインデントの行頭**で捉える」こと（`^([ \t]*)struct ... \{.*?^\1\};`）。
> さらにスクリプトへ**波括弧の収支チェック**を入れた。消すのは必ず対になったブロックなので `{` と `}` の差分は不変であるはずで、ずれたら書き込みを中止する。この検査があれば事故は起きなかった。**C++ をスクリプトで一括編集するときは必ず入れること。**

#### 4c: Bloom のミップチェーン化

**新設シェーダー 3 本**（すべて `Load` のみ・サンプラー不使用＝既存の流儀）: `BloomDownsample` / `BloomUpsample` / `BloomComposite`。

```
Input ─┬─→ Down0(1/2) → Down1(1/4) → … → Down5(1/64)
       │                                    ↓
       │      Up0(1/2) ← Up1(1/4) ← … ← Up4(1/32)
       │        ↓
       └────→ Composite → ChainOutput
```

| 判断 | 理由 |
|---|---|
| ダウンサンプルは 2x2 でなく **4x4 ボックス** | 1 テクセルだけ極端に明るい画素が残るとカメラ移動時にちらつく |
| 閾値処理は**最初の 1 回だけ** | 各段で掛けるとぼかすほど二重に削られる |
| 上りは下りとは**別のリソース**へ書く | 同じリソースを SRV と UAV に同時に割り当てられない。一時ターゲットは計 11 枚（いずれも縮小サイズで合計 11MB 程度） |
| 定数バッファは**パスごとに別実体** | GPU が読むのは記録より後なので、1 本を使い回すと全段が最後のパスの値で実行される |
| `r.Bloom.BlurRadius` を廃止 | 広がりはミップ段数が決めるため、半径のつまみが意味を失った。旧 `Bloom.CS.hlsl` も参照ゼロになったので削除 |

**検証結果**

| # | 検証 | 結果 |
|---|---|---|
| 1 | ビルド | **PASS** exit 0（`PostEffectPassContext` の include 漏れで 1 回失敗 → 修正） |
| 2 | 未改修エフェクトが動く | **PASS** Vignette / ChromaticAberration / Outline を同時有効にして正常描画 |
| 3 | ミップチェーンが効く | **PASS** 閾値 0.1・強度 2.0 で平均 RGB が **+84.9 / +77.1 / +76.5（255 中）**、にじみが画面規模に拡大。旧実装（半径 5 テクセル固定）では原理的に不可能な広がり |
| 4 | 全段が初期化されている | **PASS** ノイズ・黒帯なし（途中段が走っていなければ未初期化のまま加算され現れる） |
| 5 | 段の検証ログ | **PASS** `chain validated: SceneHDR=5 Tonemap=1 PostTonemap=11` |

**残課題**: 12 パスが個別にグラフへ並んでいることは画像からは確認できない。ノードエディタで `Bloom.Down0..5` / `Bloom.Up4..0` / `Bloom.Composite` の 12 ノードを目視すること。

**未移行**: `LensFlare` は内部で 4 パスを自前ディスパッチし中間ターゲット 4 枚を自前で持ったまま。`BuildPasses` の既定実装で動き続けているので支障はないが、transient プールへ載せれば VRAM が減りノードエディタからも見えるようになる。絞り羽根・ゴーストの見た目検証が要るため別途。
