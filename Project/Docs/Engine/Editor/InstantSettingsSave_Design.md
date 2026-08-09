# 設定の瞬間保存（イベント駆動化）設計書

作成日: 2026-08-09
ステータス: **Phase 1〜5 全実装済み（2026-08-10）**
関連設計書: [CVar_Design.md](CVar_Design.md) / [EditorSettingsAutoSave_Design.md](EditorSettingsAutoSave_Design.md)

## 目的

Unity / Unreal と同じ「**パラメータを調整した瞬間に保存される**」体感を実現する。

現状は 1 秒間隔のポーリング差分保存であり、以下の問題がある。

| 問題 | 原因 |
|---|---|
| 最大 1 秒の未保存窓（クラッシュで調整が消える） | 保存トリガーがポーリング |
| CVars.json を手で直すと上書きされて消える | ポーリングが「エンジン内の値」を常に正とみなす |
| 毎秒、変更ゼロでも全セクションのシリアライズが走る | 変更検知が「全量シリアライズ→JSON 比較」 |
| 「今保存されたのか？」が分からない | 保存タイミングがユーザー操作と無関係 |

## 商用エンジンはどう動いているか（前提知識）

「全部が瞬間保存」ではない。**対象が 3 種類に分離されている**ことが仕組みの核心。

| 種類 | Unreal | Unity | 保存タイミング |
|---|---|---|---|
| 設定（Settings / Preferences） | `Config/*.ini` | `ProjectSettings/` 等 | **変更を確定した瞬間** |
| シーンの中身 | `.umap` | `.unity` | 明示保存のみ（Ctrl+S） |
| ランタイムのコンソール変数 | CVar | — | **保存されない**（揮発） |

- 瞬間保存の正体は**イベント駆動**。プロパティ編集 → 変更イベント発火 →
  その場でファイル書き込み、という一本道になっている。ポーリングは存在しない。
- 「瞬間」とは**操作の確定時**（スライダーを離す・Enter を押す）であり、
  ドラッグ中の毎フレームではない。UE の変更イベントには「編集中(Interactive)」と
  「確定(ValueSet)」の区別があり、保存は確定時のみ走る。
- 即時保存が危なくないのは、**全編集が Undo に乗っている**から。
  即時保存と Undo はセットの機能。

本エンジンの CVar は「UE の CVar（揮発）」ではなく「UE の Settings（保存対象）」に
相当する使い方をしている。実行時状態を `NoSave` で除外する規律（CVar_Design.md 参照）は、
この分類を手動で行っているということ。この規律は本設計後も変わらず必要。

## 設計の全体像

5 つの Phase に分ける。**Phase 1+2 だけで「調整した瞬間に保存」の体感は完成する**。
Phase 3 以降は堅牢化と商用同等の周辺機能。

```
Phase 1  レビジョン駆動保存      ポーリング → 変更検知＋デバウンス書き込み
Phase 2  コミット境界            スライダーを離した瞬間に即時フラッシュ
Phase 3  書き込み経路の一本化     Set() を唯一の変更経路にする（Undo の前提）
Phase 4  保存先の 2 層化          プロジェクト設定（git 共有）と個人状態の分離
Phase 5  Undo                    Ctrl+Z（即時保存の安全弁）
```

### 変更後のデータフロー（Phase 1+2 完成時）

```
スライダーをドラッグ中
   │  毎フレーム: 値更新 → NotifyChanged() → revision++
   │  （保存はまだ走らない。書き込みゼロ）
   ▼
スライダーを離す（IsItemDeactivatedAfterEdit）
   │  CVarRegistry::NotifyCommit()  ← Phase 2 の追加点
   ▼
同フレームの EndFrame
   │  コミットあり → 該当セクションを即 Serialize → WriteAtomic
   ▼
CVars.json 更新（管理パネルに保存時刻が出る）

コンソール / プリセット読込など UI 以外の変更
   │  Set() → revision++（コミットイベントは無い）
   ▼
EndFrame: revision 変化を検知 → 最後の変化から 0.3 秒静かになったら保存
          （デバウンス。連続変更を 1 回の書き込みに合流させる）
```

ポイント: **コミットは「即時」、それ以外は「0.3 秒デバウンス」の 2 段構え**。
どちらの経路でも書き込みは `WriteAtomic`（tmp → rename）なのでクラッシュ安全性は現状維持。

---

## Phase 1: レビジョン駆動保存

### 変更内容

`IEditorSettingsSection` に安価な変更シグナルを追加する。

```cpp
class IEditorSettingsSection {
public:
    /// 変更検知の方式。Revision を返すセクションは GetChangeRevision() の
    /// 整数比較だけで「変わったか」を判定できる（毎フレーム呼んでよい）。
    /// Polling は従来通り 1 秒ごとの全量シリアライズ比較（ミラー系用）。
    enum class ChangeSignal { Polling, Revision };
    virtual ChangeSignal GetChangeSignal() const { return ChangeSignal::Polling; }
    virtual uint64_t GetChangeRevision() const { return 0; }
    // ... 既存メンバはそのまま
};
```

`CVarSettingsSection` は Revision 方式を実装する。実体は既存の
`CVarRegistry::GetGlobalRevision()`（値が実際に変わったときだけ進む通番）を返すだけ。

`EditorSettingsSubsystem::EndFrame` を毎フレーム実行に変え、方式別に分岐する
（実装済み。以下は実装の要約）。

```cpp
void EditorSettingsSubsystem::EndFrame()
{
    // Polling 方式は従来通り 1 秒に 1 回だけ見る
    const bool pollNow = /* 前回ポーリングから 1 秒経過 */;

    for (auto& entry : entries_) {
        if (entry.section->GetChangeSignal() != ChangeSignal::Revision) {
            if (pollNow) { SaveIfChanged(entry); }
            continue;
        }
        // ---- イベント駆動（毎フレーム整数比較 2 回。コストほぼゼロ）----
        const uint64_t rev = entry.section->GetChangeRevision();
        if (rev != entry.lastSeenRevision) {
            entry.lastSeenRevision = rev;
            entry.lastChangeTime = now;      // 変化を観測 → デバウンス開始/延長
            entry.dirty = true;
        }
        // 確定（コミット）はデバウンスを待たず同フレームで書く
        const uint64_t commit = entry.section->GetCommitRevision();
        const bool committed = (commit != entry.lastSeenCommitRevision);
        entry.lastSeenCommitRevision = commit;

        if (!entry.dirty) { continue; }
        const bool quiet = duration(now - entry.lastChangeTime) >= kDebounceSec; // 0.3s
        if (committed || quiet) {
            entry.dirty = !SaveIfChanged(entry);  // 書き込み失敗時のみ再試行
        }
    }
}
```

実装メモ（設計スケッチからの変更点）:
- コミットの伝達は `pendingCommitFlush_` フラグではなく、セクションの仮想関数
  `GetCommitRevision()` に一般化した。CVarSettingsSection がレジストリの
  `GetCommitRevision()`（`NotifyCommit()` で進む通番）を返す。
  Settings 層が CVar 層へ直接依存せずに済み、コミットの取りこぼしも構造的に無い
- `SaveIfChanged` の戻り値を「永続化されているか」（書き込み成功 or 差分なし）に
  変更した。差分なし＝値が往復して元に戻ったケースで dirty を正しく下ろすため
- 登録時（RegisterSection）に復元直後の通番で基準を初期化する。
  Deserialize が復元で通番を進めるため、揃えないと起動直後に誤検知保存が走る

### なぜデバウンスが必要か

CVarPanel はドラッグ中も毎フレーム `NotifyChanged()` を呼ぶ（CVarPanel.cpp:170-173）。
revision 変化を見て即書き込むと**ドラッグ中に毎フレーム ファイル書き込み**が走ってしまう。
「最後の変化から 0.3 秒静かになったら書く」ことで、連続変更が 1 回の書き込みに合流する。
コンソール入力・プリセット読込・`ResetTree` など UI 以外の変更経路もこのデバウンスが拾う。

### ミラー系（DebugCamera / AtmosphereLights）の扱い【実装時の発見で設計を修正】

設計時は「ミラー系は別セクションでポーリング維持」と想定していたが、実装時に
**セクションは既に CVarSettingsSection の 1 つだけ**（旧個別セクションは全廃済み）で、
ミラー系の値も同じ CVars.json に入っていることが分かった。

そのまま Revision 方式に含めても問題ないことを確認した:
- カメラ移動中は毎フレーム revision が進む → `lastChangeTime` が更新され続け
  デバウンスが満了しない → **移動中は 1 回も書き込まれない**
- 移動を止めて 0.3 秒後に 1 回だけ書き込まれる（「前回の視点から再開」仕様にも合致）
- つまりデバウンスが「連続変更の抑制」を自然に担うため、別方式は不要だった

Polling 方式の分岐は、将来 CVar を介さないセクションが増えた場合のために
インターフェースへ残してある（既定値も Polling）。

### 起動時の上書き一覧ログ（同 Phase で実施）

`CVarSettingsSection::Deserialize` 直後に、既定値から上書きされている CVar を
1 行ずつログへ出す。

```
CVar: CVars.json により 12 個が既定値から上書きされています
  r.Water.Foam.CascadeWeights = [1,1,1] (default [1,0.5,0.2])
  r.Water.FFT.WindSpeed       = 22.53   (default 18.0)
  ...
```

差分保存方式の弱点「コード既定値を変えても保存済みの古い値が黙って勝つ」を、
毎起動で可視化する（2026-08-09 の調査で CascadeWeights の較正値が
保存値に潰されていた実害あり）。

---

## Phase 2: コミット境界（スライダーを離した瞬間に保存）

### 変更内容

CVarPanel の `DrawWidget` に確定検知を 1 行足す。

```cpp
// DrawWidget 末尾（NotifyChanged の後）
if (ImGui::IsItemDeactivatedAfterEdit()) {
    CVarRegistry::Get().NotifyCommit();   // 「編集が確定した」印
}
```

`CVarRegistry` にコミットフラグ（またはコミット通番）を追加し、
`EditorSettingsSubsystem::EndFrame` は**コミットを見たフレームでデバウンスを待たずに
即保存**する（Phase 1 のコード中 `pendingCommitFlush_`）。

- `ImGui::IsItemDeactivatedAfterEdit()` は UE の「ValueSet（確定）」に完全対応する
  ImGui 標準機能。スライダーのマウスリリース・テキスト入力の Enter/フォーカス喪失で
  true になる。
- チェックボックス・コンテキストメニューの「デフォルトに戻す」はクリック＝確定なので
  同じ経路で拾われる。
- CVarPanel を経由しない専用 UI（水面パネル等の手書きウィジェット）も、確定時に
  `NotifyCommit()` を呼べば同じ挙動になる。呼ばなくてもデバウンス（0.3 秒後）が
  保険として働くため、**移行漏れが起きても保存されないことはない**。

### 体感の完成形

| 操作 | 保存されるタイミング |
|---|---|
| スライダーをドラッグして離す | **離した瞬間**（同フレーム） |
| チェックボックスをクリック | **クリックした瞬間** |
| コンソールで値を変更 | 0.3 秒後 |
| プリセット読込（一括変更） | 読込完了の 0.3 秒後に 1 回だけ書き込み |
| カメラを動かす（ミラー系） | 従来通り 1 秒ごと |

---

## Phase 3: 書き込み経路の一本化（Set() の一本道化）

### 現状の問題

`ICVar::AsFloat()` 等が**書き込み可能な生ポインタ**を返し、ImGui がストレージを
直接書き換えている。通番の整合は「ウィジェットが true を返したら NotifyChanged を呼ぶ」
という各 UI の自己申告に依存しており、呼び忘れると変更が検知されない
（＝保存もキャッシュ無効化も走らない）。Undo を入れる場合、旧値を捕まえる場所が無い。

### 変更内容

UI は「ローカルコピーを編集 → 変化していたら `Set()`」に統一する。

```cpp
case CVarType::Float: {
    float v = *cvar->AsFloat();                     // 読み取りコピー
    if (range.valid ? ImGui::SliderFloat(label, &v, range.min, range.max, "%.3f")
                    : ImGui::DragFloat(label, &v, kDefaultDragSpeed)) {
        static_cast<CVar<float>*>(cvar)->Set(v);    // 唯一の書き込み経路
        changed = true;
    }
    break;
}
```

- `Set()` は等価判定つきなので毎フレーム呼んでも通番は「実際に変わったとき」しか進まない。
- 非 const の `AsXxx()` は削除（const 版のみ残す）。型消去側に
  `ICVar::SetFromPointer(const void*)` 等の型安全な書き込み口を 1 つ用意し、
  シリアライズ（CVarSerialization::Load）もそこへ寄せる。
- これで「値が変わる場所」がコード上ただ 1 箇所になり、Phase 5 の Undo は
  `Set()` 内に 3 行足すだけで全 CVar に効くようになる。

**実装メモ（2026-08-10 実装済み）**:
- CVarPanel は各型ともローカルコピー編集＋`SetFromPointer` に統一。
- **CVarPanel 以外にも生ポインタ利用があった**: `WaterSurfaceParameterPanel` が
  約 30 箇所で `WaterCVars::X.AsFloat()` を ImGui へ直接渡していた
  （`notify(cvar, changed)` で通番を自己申告する旧方式）。ファイル内テンプレート
  `EditCVar(cvar, [](T* v){ return ImGui::Widget(..., v, ...); })` に置き換えた。
  非 const `AsXxx()` の削除により、この種の残存はコンパイルエラーで検出される。
- Undo の記録は `Set()` 内ではなく UI の編集セッション（掴む〜離す）単位にした
  （`Set()` 内だとドラッグ中の中間値が全部レコードになるため）。

---

## Phase 4: 保存先の 2 層化（プロジェクト設定 / 個人状態）

### 現状の問題

`Application/Saved/EditorSettings/` の 1 階層に、性質の違う 2 種類が同居している。

| 性質 | 例 | 本来の扱い |
|---|---|---|
| プロジェクト資産（較正値） | `r.Water.*` `r.Cloud.*` | **git にコミットしてチーム共有** |
| 個人の作業状態 | `d.SceneCamera.*`（カメラ位置） | git 無視。個人ごとに別 |

UE は `Config/`（コミット対象）と `Saved/Config/`（個人）を分けており、
「調整した較正値が他のメンバーにも配られる」のは前者に入っているから。

### 変更内容

```
Application/Config/EngineSettings/CVars.json     ← r.* / sys.*（git 管理）
Application/Saved/EditorSettings/EditorState.json ← d.*（.gitignore）
```

- `CVarSettingsSection` を接頭辞で 2 セクションに分割する
  （`r.`/`sys.` → Config、`d.` → Saved）。分類を接頭辞に載せる規約は
  CVar_Design.md の命名規則（r./d./sys.）と一致しており、新フラグは不要。
- 読み込み順は Config → Saved（個人状態が後勝ちだが、接頭辞が排他なので実際は競合しない）。
- 移行: 初回起動時に旧 `CVars.json` があれば両ファイルへ振り分けてリネーム
  （`CVars.json.migrated`）。**ユーザーデータは消さない**（過去の教訓:
  PostEffect 移行で有効化設定を失った）。
- `.gitignore` に `Application/Saved/` を追加する。

**実装メモ（2026-08-10 実装済み・実機検証済み）**:
- `IEditorSettingsSection::StorageArea`（UserSaved / ProjectConfig）を新設し、
  `EditorSettingsSubsystem` のパス解決をセクションの区分から導出する形にした。
  バックアップ（`_backup/*.bak`）も各区分のディレクトリ配下。
- `CVarSettingsSection` はコンストラクタ引数 `userStatePart` で 2 インスタンス化
  （"CVars"=Config 側 / "EditorState"=Saved 側）。除外は
  `CVarSerialization::Save` に追加した `excludePrefix` で行う。
- 起動時の上書き一覧ログは静的 `LogOverriddenCVars()` へ移し、両セクション復元後に
  1 回だけ出す（Deserialize 内だと 2 回出るため）。
- `.gitignore` は既に `Project/Application/Saved/` を無視しており変更不要。
  `Application/Config/` は無視されない＝コミット対象（git check-ignore で確認済み）。
- 実機検証: 旧 CVars.json（31 項目）→ 設定 26 件が Config/CVars.json・状態 5 件が
  EditorState.json へ移行、旧ファイルは .migrated へ退避、復元・上書きログとも正常。

---

## Phase 5: Undo（Ctrl+Z）

即時保存の安全弁。商用エンジンの「触った瞬間に保存しても怖くない」体感の核心。

### 設計

```cpp
struct CVarUndoRecord {
    ICVar*         cvar;      // CVar は static 寿命なので生ポインタで安全
    nlohmann::json oldValue;  // 型消去の値表現は既存の JSON 形式を流用
    nlohmann::json newValue;
};
class CVarUndoStack {  // リングバッファ 64 件程度
    void Push(record);
    void Undo();  // oldValue を SetFromPointer で書き戻す（このとき Undo 記録はしない）
    void Redo();
};
```

- **記録の単位は「編集セッション」**: ImGui の `IsItemActivated()`（掴んだ瞬間）で
  旧値を控え、`IsItemDeactivatedAfterEdit()`（離した瞬間）で 1 レコードとして積む。
  ドラッグ中の中間値は記録しない（UE と同じ粒度）。
- Phase 3 が前提（`Set()` 一本道でないと UI 以外の変更に旧値を差し込めない）。
- ミラー系 CVar（実体が毎フレーム上書きする `d.SceneCamera.*` 等）は Undo 対象外。
  戻しても次フレームに実体が上書きするため無意味。`CVarFlags::Mirrored` を新設して
  宣言的に除外する（このフラグは CVarPanel でのアイコン表示にも使い、
  「手で書いても実体に上書きされる値」を UI 上で判別可能にする）。
- Undo による値変更も通番を進めるので、保存は Phase 1 の仕組みが自動で拾う。

**実装メモ（2026-08-10 実装済み）**:
- `Utility/CVar/CVarUndoStack.h/.cpp`（新規。vcxproj へ手動登録済み）。
  値は json でなく 16 バイト固定バッファ（最大 Vector4）で保持し依存ゼロ。
  リングは 64 レコード・バッチ ID で一括リセットを 1 回の Undo に束ねる。
- Ctrl+Z / Ctrl+Y は `CVarUI::DrawTree` 冒頭の `HandleUndoShortcuts()` が処理。
  **CVar ツリーを含むウィンドウがフォーカス中のみ反応**（Hierarchy のシーン Undo と
  スコープを分離）。テキスト入力中（WantTextInput）は ImGui 自身の Ctrl+Z に譲る。
  同一フレームの複数 DrawTree 呼び出しはフレーム番号ガードで 1 回に抑制。
- Undo/Redo の適用は `SetFromPointer` → `NotifyCommit` なので即時保存される。
- `CVarFlags::Mirrored` を新設し、DebugCameraCVars / EnvironmentFeature の
  ミラー CVar 全件（従来 NoUI のみ）に付与。BeginEdit が弾くため Undo 対象外。
  ツールチップにミラー値である旨と既定値を常時表示するようにした。

---

## 不変条件（実装時に守ること）

1. **`NoSave` の規律は不変**: 実行時に書き換えられる値（フェード状態・遷移状態）を
   保存すると起動が壊れる。CVar 化時に「誰がこの値を書くか」を grep で確認する
   規律は本設計後も必要（瞬間保存になることで、漏れたときの被害はむしろ速く固定化する）。
2. **書き込みは常に WriteAtomic 経由**: tmp → rename と 1 世代バックアップは変更しない。
3. **ミラー系セクションはポーリングのまま**: Revision 方式に変えない（書き込み頻度が
   無制限になる）。
4. **手編集 JSON のサポート方針を明文化**: 瞬間保存下では「エンジン起動中の手編集」は
   従来以上に上書きされやすい。サポートする編集経路は UI とコンソール。ファイル直編集は
   **エンジン停止中のみ**（管理パネルにその旨を表示する）。
5. **デバウンス中の終了**: `Finalize` の `FlushAll` が dirty なセクションを必ず書く
   （現行実装のままで満たされるが、dirty フラグ導入後もこの経路が生きていることを確認する）。
6. **プリセット読込などの一括 Set は 1 回の書き込みに合流すること**（デバウンスの検証項目）。

## 検証手順

| 項目 | 手順 | 合格条件 |
|---|---|---|
| コミット即時保存 | スライダーを動かして離す → 直後に CVars.json の更新時刻を見る | 離した瞬間（1 フレーム以内）に更新 |
| ドラッグ中は書かない | ドラッグしたまま数秒保持し、ファイル更新時刻を監視 | ドラッグ中は更新されない |
| デバウンス合流 | プリセット読込（数十 CVar が一括変更） | 書き込みが 1 回だけ |
| クラッシュ窓の消滅 | スライダーを離した直後にプロセスを kill → 再起動 | 調整値が復元される |
| ミラー系の頻度 | カメラを動かし続けて EditorState.json の更新間隔を監視 | 約 1 秒間隔のまま |
| 上書きログ | CVars.json に既定値と違う値を書いて起動 | 上書き一覧がログに出る |
| 無変更時のコスト | 何も触らず放置 | シリアライズ・書き込みが一切走らない（プロファイラで確認） |

※ ログ確認は WM_CLOSE で正常終了させてから読むこと（spdlog は終了までフラッシュされない）。

## 影響ファイル一覧

| ファイル | Phase | 変更内容 |
|---|---|---|
| `EngineSystem/Settings/IEditorSettingsSection.h` | 1 | ChangeSignal / GetChangeRevision 追加 |
| `EngineSystem/Settings/EditorSettingsSubsystem.h/.cpp` | 1,2 | EndFrame の 2 方式分岐・デバウンス・コミットフラッシュ |
| `EngineSystem/Settings/CVarSettingsSection.h/.cpp` | 1,4 | Revision 方式実装・上書きログ・接頭辞分割 |
| `Utility/CVar/CVarRegistry.h/.cpp` | 2 | NotifyCommit / コミット通番 |
| `Editor/ImGui/CVarPanel.cpp` | 2,3,5 | 確定検知・ローカルコピー編集・Undo 記録 |
| `Utility/CVar/CVar.h/.cpp` | 3,5 | 非 const AsXxx 削除・SetFromPointer・Mirrored フラグ |
| `Utility/CVar/CVarSerialization.cpp` | 3 | SetFromPointer 経由へ変更 |
| `Utility/CVar/CVarUndoStack.h/.cpp`（新規） | 5 | Undo リングバッファ |
| `.gitignore` | 4 | `Application/Saved/` 追加 |

## 本設計の範囲外（関連する既知の課題）

2026-08-09 の調査で判明した混乱源のうち、本設計と独立に対処できるもの:

- **廃止済み `Application/Saved/EditorSettings/Water.json` の削除**（どこからも
  読まれていない遺物。CVars.json と食い違う値を持ち混乱の主因の一つ）
- 実効値スナップショットのダンプ（`Cache/CVarsEffective.json`）
- 保存値に既定値の指紋を添えて既定値変更を検出する形式拡張

これらは上書きログ（Phase 1）実装後に改めて要否を判断する。
