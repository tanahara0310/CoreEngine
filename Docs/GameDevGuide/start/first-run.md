# 起動してみる

## ビルドの種類は 3 つ

| 構成 | エディタ | 速さ | いつ使うか |
|---|---|---|---|
| **Debug** | あり | 遅い | 落ちる原因を追うとき |
| **Development** | あり | 実用的 | **普段の制作はこれ** |
| **Release** | **なし** | 速い | 完成したゲームを配るとき |

`Release` だけ `CORE_EDITOR` が付かないので、**エディタの画面が出ません**。起動するといきなりゲームが始まります。

## 起動する

Visual Studio で `Project/CoreEngine.vcxproj` を開き、構成を **Development** にして実行します。

コマンドから起動するなら、**作業フォルダを `Project` にして**ください。アセットをそこからの相対パスで探します。

```bat
cd C:\CoreEngine\Project
..\generated\CoreEngine\outputs\Development\CoreEngine.exe
```

!!! warning "作業フォルダを間違えるとアセットが見つかりません"
    `Development` と `Debug` は `Project/Application/Assets` を読みます。`Release` は **exe の隣**のアセットを読みます。

## 最初に出る画面

![エディタの全体](../assets/images/editor-full.png)

左から **ヒエラルキー**（シーンにあるものの一覧）、中央が **Game**（ゲームの画面）、右が **インスペクタ**（選んだものの中身）、下が **Project / Console / Profiler** です。

どのシーンで始まるかは `Application/Config/EngineSettings/Project.json` の `initialScene` で決まります。

```json
{
    "initialScene": "PhysicsTestScene",
```

## 再生してみる

左上の **▶** を押すと再生が始まります。

![ツールバー](../assets/images/panel-toolbar.png)

| ボタン | 意味 |
|---|---|
| ▶ | 再生する／止める |
| ⏸ | 一時停止する |
| ⏭ | 一時停止したまま 1 フレームだけ進める |

!!! info "再生中の編集は、止めると元に戻ります"
    再生を始めた瞬間のシーンを控えていて、止めるとそこへ戻します。**再生中にいくら動かしても、保存したシーンは壊れません。** 逆に、再生中に調整した値を残したいときは、止める前に値をメモしてください。

## 止まっている間は何が動かないか

エディタは**停止状態**で起動します。停止中は次のものが動きません。

- スクリプトの `Update` / `FixedUpdate` / `LateUpdate`
- 物理（落ちない・転がらない）
- `Time::DeltaTime()`（0 を返す）

逆に、**描画・カメラ操作・ギズモ・インスペクタの編集は止まっていても動きます。** だから止めたまま見た目を作り込めます。

## 次は

- 画面の各部分を詳しく → [エディタの画面](editor-tour.md)
- とにかく何か動かしたい → [チュートリアル 1. 玉を転がす](../tutorial/01-ball.md)
