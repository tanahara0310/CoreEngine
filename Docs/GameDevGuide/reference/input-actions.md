# 入力アクション

`Application/Config/EngineSettings/InputActions.json` の中身です。
**Help → キー操作を見る**（Key Config）から編集できます。

## 場面

| 場面 | いつ効くか |
|---|---|
| `Game` | ふつうに遊んでいる間 |
| `UI` | **UI を選んでいる間**（ボタンにフォーカスがある間） |
| `Editor` | エディタのあるビルドでだけ |

UI を選んでいる間は `Game` の操作が止まります。**決定ボタンでボタンを押した瞬間にキャラも跳ぶ、ということが起きません。**

## 一覧

| 名前 | 表示 | 場面 | 既定の割り当て |
|---|---|---|---|
| `MoveForward` | 前進 | `Game` | `Key:W` / `Key:Up` / `Axis:LeftStickY+` / `Gamepad:DPadUp` |
| `MoveBack` | 後退 | `Game` | `Key:S` / `Key:Down` / `Axis:LeftStickY-` / `Gamepad:DPadDown` |
| `MoveLeft` | 左移動 | `Game` | `Key:A` / `Key:Left` / `Axis:LeftStickX-` / `Gamepad:DPadLeft` |
| `MoveRight` | 右移動 | `Game` | `Key:D` / `Key:Right` / `Axis:LeftStickX+` / `Gamepad:DPadRight` |
| `Jump` | ジャンプ | `Game` | `Key:Space` / `Gamepad:A` |
| `Sprint` | ダッシュ | `Game` | `Key:LShift` / `Gamepad:LeftThumb` |
| `Attack` | 攻撃 | `Game` | `Mouse:Left` / `Gamepad:X` |
| `Interact` | インタラクト | `Game` | `Key:E` / `Gamepad:B` |
| `UINavigateUp` | UI上 | `UI` | `Key:Up` / `Axis:LeftStickY+` / `Gamepad:DPadUp` |
| `UINavigateDown` | UI下 | `UI` | `Key:Down` / `Axis:LeftStickY-` / `Gamepad:DPadDown` |
| `UINavigateLeft` | UI左 | `UI` | `Key:Left` / `Axis:LeftStickX-` / `Gamepad:DPadLeft` |
| `UINavigateRight` | UI右 | `UI` | `Key:Right` / `Axis:LeftStickX+` / `Gamepad:DPadRight` |
| `UIConfirm` | UI決定 | `UI` | `Key:Enter` / `Gamepad:A` |
| `UICancel` | UIキャンセル | `UI` | `Key:Escape` / `Gamepad:B` |
| `Pause` | ポーズ | `Game\|UI` | `Key:Escape` / `Gamepad:Start` |
| `EditorFocusSelection` | 選択へ寄る | `Editor` | `Key:F` |
| `EditorGizmoTranslate` | ギズモ：移動 | `Editor` | `Key:W` |
| `EditorGizmoRotate` | ギズモ：回転 | `Editor` | `Key:E` |
| `EditorGizmoScale` | ギズモ：拡縮 | `Editor` | `Key:R` |

## 綴りの読み方

| 形 | 意味 |
|---|---|
| `Key:W` | キーボードの W |
| `Mouse:Left` | マウスの左ボタン |
| `Gamepad:A` | パッドの A ボタン |
| `Axis:LeftStickY+` | 左スティックの Y を倒した向き（`+` / `-`） |
| `Ctrl+Key:S` | 組み合わせ（`Ctrl` `Shift` `Alt`） |

## 足す

Key Config で増やすか、`InputActions.json` に直接書きます。

```json
{
    "id": "Dash",
    "display": "回避",
    "context": "Game",
    "defaults": ["Key:LControl", "Gamepad:B"]
}
```

スクリプトからは `InputAction::Dash` で使えます。

