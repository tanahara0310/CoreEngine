# コンポーネント一覧

インスペクタの **＋ コンポーネント追加**から足せるものです。
どれも、スクリプトから `owner.<型名の先頭を小文字にした名前>` で取れます。

```cpp
Rigidbody@ body = owner.rigidbody;
UISlider@ slider = owner.uiSlider;
```

!!! info "この表はソースから作っています"
    項目名と説明は、エンジンの型記述子（`REFLECT_*`）から機械的に抜き出したものです。インスペクタに出るものと一致します。

## 置き場所

### Transform（トランスフォーム）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `translate` | 位置 |  |
| `rotate` | 回転 |  |
| `scale` | スケール |  |
| `parent` | 親 |  |

### EulerTransform（トランスフォーム）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `translate` | 位置 |  |
| `rotate` | 回転 |  |
| `scale` | スケール |  |

### RectTransform（UI トランスフォーム）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `anchor` | アンカー |  |
| `anchoredPosition` | 位置 |  |
| `pivot` | 基準点 |  |
| `size` | 大きさ |  |
| `rotation` | 回転 |  |
| `sortOrder` | 描画順 |  |

## 見た目

### MeshRenderer（メッシュ描画）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `model` | モデル |  |
| `texture` | テクスチャ |  |
| `blendMode` | ブレンド |  |

### Material（マテリアル）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `color` | ベースカラー |  |
| `metallic` | メタリック |  |
| `roughness` | ラフネス |  |
| `occlusionStrength` | オクルージョン |  |
| `emissive` | エミッシブ |  |
| `iblIntensity` | IBL 強度 |  |
| `lighting` | ライティング |  |
| `normalMap` | 法線マップ |  |
| `dithering` | ディザリング |  |
| `ditheringScale` | ディザリングの細かさ |  |

### SpriteRenderer（スプライト描画）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `texture` | テクスチャ |  |
| `color` | カラー |  |
| `anchor` | アンカー |  |
| `uvMin` | UV 左上 |  |
| `uvMax` | UV 右下 |  |
| `uvOffset` | UV の移動 |  |
| `uvScale` | UV の倍率 |  |
| `uvRotation` | UV の回転 |  |
| `flipX` | 左右反転 |  |
| `flipY` | 上下反転 |  |
| `blendMode` | ブレンド |  |

### Text3DRenderer（3D テキスト描画）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `font` | フォント |  |
| `text` | 文字列 |  |
| `fontSize` | 文字の大きさ |  |
| `lineSpacing` | 行間 |  |
| `wrapWidth` | 折り返し幅 |  |
| `fieldAutoFit` | 枠を文字に合わせる |  |
| `fieldSize` | 枠の大きさ |  |
| `alignH` | 横揃え |  |
| `alignV` | 縦揃え |  |
| `pivot` | 中心 |  |
| `color` | カラー |  |
| `outlineColor` | 縁取りの色 |  |
| `outlineWidth` | 縁取りの太さ |  |
| `weight` | 太さ調整 |  |
| `billboard` | ビルボード |  |
| `depthMode` | 深度 |  |

### Animator（アニメーション）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `model` | モデル |  |

スクリプトから呼べる操作

| 操作 | 意味 |
|---|---|
| `Switch()` | クリップの切り替え |
| `SwitchWithBlend()` | クリップの切り替え（ブレンド） |
| `GetCurrentClipName()` | 再生中のクリップ名 |

## 物理

### Collider（コライダー）

インスペクタで編集する項目はありません（専用の編集画面を持つか、印として置くだけのものです）。

### Rigidbody（剛体）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `bodyType` | 種別 | 動的 = 重力と力で動く / キネマティック = 速度を指定して動かす / 静的 = 動かない |
| `mass` | 質量 | kg。キネマティックと静的では無限として扱う |
| `useGravity` | 重力を受ける |  |
| `linearDamping` | 抗力 | 大きいほど速度が早く落ちる（0 で減らさない） |
| `angularDamping` | 回転の抗力 | 大きいほど回転が早く止まる（転がり続けるのを防ぐ） |
| `freezeRotation` | 回転を止める | 接触で回らなくなる（向きは自分で指定する） |
| `velocity` | 速度 | m/s。実行中の値なので保存しない |
| `angularVelocity` | 角速度 | rad/s。実行中の値なので保存しない |
| `sleeping` | 眠っている | 止まっている間は計算から外れる。触ると起きる |

スクリプトから呼べる操作

| 操作 | 意味 |
|---|---|
| `AddForce()` | 力を加える |
| `AddImpulse()` | 撃力を加える |
| `AddTorque()` | トルクを加える |
| `WakeUp()` | 起こす |

### CharacterController（キャラクタコントローラ）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `slopeLimit` | 登れる坂 | 度。これより急な坂では滑り落ちる |
| `stepOffset` | 越えられる段差 | m。これ以下の段差は歩いたまま上がる |
| `skinWidth` | 表面の余白 | m。壁との間にこれだけ隙間を残す |
| `useGravity` | 重力を受ける | 接地していないと落ちる |
| `grounded` | 接地している |  |
| `velocity` | 速度 | m/s。実行中の値なので保存しない |

スクリプトから呼べる操作

| 操作 | 意味 |
|---|---|
| `Move()` | 動かす |
| `SimpleMove()` | 歩かせる |
| `Jump()` | 跳ぶ |

### PhysicsMaterial（物理マテリアル）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `materialAsset` | 材質ファイル |  |
| `restitution` | 反発係数 | 0 で跳ねず、1 で高さが落ちない |
| `friction` | 摩擦係数 | 0 で滑り続け、大きいほど早く止まる |
| `restitutionCombine` | 反発の合成 |  |
| `frictionCombine` | 摩擦の合成 |  |

## UI

### UIImage（UI 画像）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `texture` | テクスチャ |  |
| `color` | 色 |  |

### UIText（UI テキスト）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `text` | テキスト |  |
| `font` | フォント | 一覧に無いフォントは、フォントフォルダのファイル名かインストール済みのフォント名を入力する |
| `fontSize` | フォントサイズ |  |
| `lineSpacing` | 行間 |  |
| `fieldAutoFit` | 枠を文字に合わせる | UI トランスフォームの大きさを文字列に合わせる。大きさを変えると切れ、その枠の中で折り返す |
| `wrapWidth` | 折り返し幅 | 0 で折り返さない。枠を文字に合わせていないときは枠の幅で折り返す |
| `alignH` | 横揃え |  |
| `alignV` | 縦揃え |  |
| `color` | 色 |  |
| `outlineColor` | 縁取りの色 |  |
| `outlineWidth` | 縁取りの太さ | em 単位。フォントの距離場の幅で上限が決まる |
| `weight` | 太さ調整 |  |

### UIButton（UI ボタン）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `interactable` | 押せる | 外すと押せない色になり、ポインタを受け付けなくなる |
| `normalColor` | 通常の色 |  |
| `hoveredColor` | 乗せたときの色 | キー・パッドで選んでいるときもこの色になる |
| `pressedColor` | 押したときの色 |  |
| `disabledColor` | 押せないときの色 |  |

### UISlider（UI スライダー）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `interactable` | 動かせる | 外すと動かせない色になり、ポインタもフォーカスも受け付けなくなる |
| `direction` | 向き |  |
| `minValue` | 最小値 |  |
| `maxValue` | 最大値 |  |
| `value` | 値 |  |
| `wholeNumbers` | 整数だけ | 入れると値を整数に丸める |
| `navigationStep` | キーで動く量 | 0 なら（最大値 − 最小値）の 1/10 ずつ動く |
| `fill` | 伸びる帯 |  |
| `handle` | つまみ |  |
| `normalColor` | 通常の色 |  |
| `hoveredColor` | 乗せたときの色 | キー・パッドで選んでいるときもこの色になる |
| `pressedColor` | 掴んでいるときの色 |  |
| `disabledColor` | 動かせないときの色 |  |

### UIToggle（UI トグル）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `isOn` | 入り |  |
| `interactable` | 押せる | 外すと押せない色になり、ポインタもフォーカスも受け付けなくなる |
| `checkmark` | 入りのときに出す印 |  |
| `group` | グループ | 同じ綴りのトグル同士で 1 つだけが入りになる。空なら他と関わらない |
| `normalColor` | 通常の色 |  |
| `hoveredColor` | 乗せたときの色 | キー・パッドで選んでいるときもこの色になる |
| `pressedColor` | 押したときの色 |  |
| `disabledColor` | 押せないときの色 |  |

## 音

### AudioSource（音の再生）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `clip` | 音 |  |
| `bus` | 出力先 | 音量の設定をまとめる単位。BGM だけ絞るといった操作がここで効く |
| `playOnAwake` | 開始時に鳴らす |  |
| `loop` | 繰り返す |  |
| `volume` | 音量 |  |
| `pitch` | 高さ | 再生速度の倍率。上げると速く高くなる |
| `mute` | 消音 |  |
| `fadeInTime` | フェードイン | 鳴らし始めに音量 0 からここまで上げる秒数 |
| `spatialBlend` | 距離で変える | 0 で距離に関係なく同じ音量、1 で聞き手からの距離と左右を反映する |
| `minDistance` | 減衰の始まり | この距離までは音量が下がらない |
| `maxDistance` | 聞こえる限界 | この距離で音量が 0 になる |
| `isPlaying` | 再生中 |  |

スクリプトから呼べる操作

| 操作 | 意味 |
|---|---|
| `Play()` | 鳴らす |
| `Stop()` | 止める |
| `Pause()` | 一時停止 |
| `UnPause()` | 再開 |
| `PlayOneShot()` | 重ねて鳴らす |
| `FadeOut()` | フェードアウト |

### AudioListener（音の聞き手）

インスペクタで編集する項目はありません（専用の編集画面を持つか、印として置くだけのものです）。

## エフェクト

### ParticleSystem（パーティクル）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `texture` | テクスチャ |  |
| `model` | モデル | 指すと、板ポリの代わりにこのモデルを粒として描く |
| `blendMode` | ブレンド | 背景との合成のしかた。炎や光は「加算」、煙は「アルファ」が向く |
| `billboard` | ビルボード | 板ポリの向き。草や炎の柱のように立てたいものは「Y 軸だけ回す」。モデルの粒では使わない |

スクリプトから呼べる操作

| 操作 | 意味 |
|---|---|
| `Play()` | 再生 |
| `Stop()` | 停止 |
| `IsPlaying()` | 再生中か |
| `Clear()` | 粒を消す |
| `Emit()` | 放出 |

### GpuParticleSystem（GPU パーティクル）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `texture` | テクスチャ |  |
| `blendMode` | ブレンド | 背景との合成のしかた。炎や光は「加算」、煙は「アルファ」が向く |
| `billboard` | ビルボード | 板ポリの向き。草や炎の柱のように立てたいものは「Y 軸だけ回す」 |

スクリプトから呼べる操作

| 操作 | 意味 |
|---|---|
| `Play()` | 再生 |
| `Stop()` | 停止 |
| `IsPlaying()` | 再生中か |

## 環境

### Light（ライト）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `type` | 種類 |  |
| `color` | 色 |  |
| `intensity` | 強さ | 平行光源は照度 [lx]（快晴の太陽 = 100000）、点光源とスポットは光度 [cd] |
| `direction` | 向き |  |
| `range` | 届く距離 |  |
| `innerConeAngleDeg` | 内側の角度 |  |
| `outerConeAngleDeg` | 外側の角度 |  |
| `areaWidth` | 発光面の幅 |  |
| `areaHeight` | 発光面の高さ |  |
| `isAtmosphereSun` | 大気の太陽 |  |
| `isAtmosphereMoon` | 大気の月 |  |
| `atmosphereIntensity` | 空の明るさ | 空・雲の明るさ（無次元。太陽の目安 20）。0 で照度から自動換算 |

### SkyBox（空）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `rotation` | 向き |  |
| `environmentIntensity` | 環境光の強さ |  |

### WaterSurface（水面）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `size` | 一辺の長さ | メッシュの大きさ [m]。シーンを読み込むときに効く |
| `resolution` | 分割数 | XZ 方向の分割数。シーンを読み込むときに効く |
| `useFFTOcean` | FFT の海を使う | 外洋のうねり（FFT）で波を作る。切ると Gerstner 波になる |
| `scrollSpeed` | UV の流れる速さ |  |
| `uvTiling` | UV の繰り返し |  |

## その他

### Camera（カメラ）

| 項目 | 表示名 | 説明 |
|---|---|---|
| `isMainCamera` | ゲームの視点 | 入れるとこのカメラがゲームビューに映る |
| `projection` | 投影 |  |
| `fov` | 視野角 | 透視投影のときだけ使う [度] |
| `nearClip` | 手前の限界 |  |
| `farClip` | 奥の限界 |  |

## 記述子を持たないもの

次のコンポーネントは、値を **CVar**（設定値）側が持っています。シーンに置くのは「ここに在る」という印で、中身は **Engine Settings** で編集し、シーンの `_environment.json` に保存されます。

- `HeightFog`（高さ霧）
- `VolumetricCloud`（ボリューム雲）
- `PostProcess`（ポストエフェクト）

`SkeletonSocket`（ソケット追従）は、スクリプトから追従先を指定して使います。

