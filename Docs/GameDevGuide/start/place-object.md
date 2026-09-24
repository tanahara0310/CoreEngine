# オブジェクトを置く

## オブジェクトは「器」

オブジェクト自体は何もしません。**コンポーネントを足して初めて**、見えたり落ちたり動いたりします。

<div class="figure" markdown="0">
--8<-- "assets/figures/object-is-container.svg"
</div>

## 空のオブジェクトを作る

**GameObject → 空のオブジェクトを作成**。

できたばかりのオブジェクトには `Transform` だけが付いています。ヒエラルキーで名前をダブルクリックすると変えられます。

## コンポーネントを足す

インスペクタのいちばん下、**＋ コンポーネント追加**を押します。

一覧が出るので、上の **型を検索**に文字を入れて絞り込み、選びます。

!!! tip "自分で書いたスクリプトもここに出ます"
    `Assets/Scripts` の下に `.as` を置くと、そのクラスがこの一覧に並びます。エンジンのコンポーネントと同じ扱いです。

## よく使う組み合わせ

| 作りたいもの | 付けるコンポーネント |
|---|---|
| ただ見えるだけの飾り | `Transform` + `MeshRenderer` + `Material` |
| 落ちて転がるもの | 上 + `Collider` + `Rigidbody` |
| 動かない壁・床 | 上から `Rigidbody` を外し、`Collider` の **静的**を入れる |
| 通り抜けるが反応するもの（アイテム） | `Collider` の **トリガー**を入れる |
| 操作するキャラ | `Transform` + `MeshRenderer` + `Collider` + `CharacterController` |
| 画面に出す文字 | `RectTransform` + `UIText` |

## モデルをドラッグして置く

Project ビューからモデル（`.obj` `.fbx` `.gltf` `.glb`）を **Game ビューへドラッグ**すると、そのモデルを持ったオブジェクトができます。`MeshRenderer` と `Material` が最初から付いています。

テクスチャ（`.png` `.jpg` `.hdr`）は、**インスペクタのテクスチャ欄へ**ドラッグします。

## 複製する・消す

| 操作 | やり方 |
|---|---|
| 複製 | <kbd>Ctrl</kbd>+<kbd>D</kbd>、または **GameObject → 複製** |
| 削除 | <kbd>Del</kbd>、または **GameObject → 削除** |
| 取り消す | <kbd>Ctrl</kbd>+<kbd>Z</kbd> |
| やり直す | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |

## 親子にする

インスペクタの `Transform` にある **親**の欄で、別のオブジェクトを選ぶと子になります。

子は親について動きます。「台車の上に荷物を乗せる」「手に武器を持たせる」といったときに使います。

!!! note "骨に付けたいときは SkeletonSocket"
    アニメーションする人型の**手**に武器を持たせたいときは、親子ではなく `SkeletonSocket` コンポーネントを使います。ジョイント名（`mixamorig:RightHand` など）を指定して追従させます。

## 保存する

<kbd>Ctrl</kbd>+<kbd>S</kbd>、または **File → シーンを保存**。

右下の表示が「保存済み」になります。保存していない変更があると「未保存」になります。

## 次は

- 動かし方 → [ギズモとカメラ](../editor/gizmo-camera.md)
- 実際に 1 本作る → [チュートリアル 1. 玉を転がす](../tutorial/01-ball.md)
