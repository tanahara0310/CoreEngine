# 水面際の「反射が二重に重なって見える」原因調査（2026-08-09）

## 結論

**物理的に正しくない。** 反射が「幾何的に食い違う2枚のレイヤー」の混合になっているため。

| レイヤー | 反射方向に使う法線 | 見え方 |
|---|---|---|
| A: RT反射（`gReflectionTexture`） | **波法線**（RTパスがレイを飛ばす時点で使用） | 波に沿って動く。実シーン（島・岩）が映る |
| B: 空キューブのフォールバック | **完全な平面 `float3(0,1,0)`** | 波に一切追従しない。のっぺりしたベールになる |

`Water.PS.hlsl:445` で
```hlsl
reflectColor = lerp(skyReflectColor, rtReflection.rgb, rtConfidence);
```
と**ピクセル単位で混ぜている**。`rtConfidence` はかすめ角で高周波にばらつくので、
**「波に沿って動く像」と「動かない像」が同じ画素領域に同時に出る** → 二重像に見える。

実際の水面は各点で法線が1つしかなく、反射する放射輝度もその法線1つから決まる。
「空の分だけ平面として反射し、シーンの分だけ波に沿って反射する」という状態は物理的に存在しない。

> 参考: かすめ角の波面で「空を映す帯」と「島を映す帯」が縞状に交互に出ること**自体は物理的に正しい**
> （facet の向きで反射先が変わるため）。ただし本物は**両方が同じ法線から決まる**ので縞が波の形と一致し、
> ゴーストのような二枚重ねにはならない。

---

## 実測（同一構図・夕方・1920×1080）

構図: `eye(-120,7,-230) → look(-30,5,80) fov 0.95`、太陽高度 8°

| # | 条件 | 結果 |
|---|---|---|
| 1 | base | 淡いベージュのベール（B層）に、濃いティールの筋（A層）が重なる＝**二重像** |
| 2 | `r.Water.FresnelScale = 0`（反射を切る） | **二重像が完全に消える** → 原因は反射経路で確定 |
| 3 | `rtConfidence = 1` 固定（A層のみ） | 波に沿った像になるが**全面が細かいスペックル**（SSR成否のピクセル単位ばらつき） |
| 4 | `rtConfidence = 0` 固定（B層のみ） | **のっぺりした平坦なベージュのベール**。波が一切見えない＝平面法線の証拠 |
| 5 | フレネル法線を波法線に一致 | 変化が小さい（主因ではない） |
| 6 | **空キューブも波法線で引く** | **2つの層が同じ波形で動き、二重像が解消**（ただし斑は増える） |

→ 主因は **B層の法線が平面固定であること**（#4 と #6 で確定）。

---

## 該当箇所

`Engine/Assets/Shaders/Water/Surface/Water.PS.hlsl:437`
```hlsl
// 反射方向は波法線ではなくフラット面法線で計算し、波の斜面ごとの
// まだら混入を避ける（鏡像時代の知見を踏襲）。
float3 envReflectDir = reflect(-viewDir, float3(0.0f, 1.0f, 0.0f));
```
コメントのとおり**意図的**。鏡像カメラ平面反射だった頃の「まだら対策」をそのまま持ち込んでいる。
当時は反射像が1枚だけだったので平面化しても矛盾は出なかったが、
**RT反射と併用して lerp するようになった時点で「2つの面を混ぜる」構造になった。**

### 二次要因: `rtConfidence` が高周波にばらつく
`Engine/Assets/Shaders/Water/RayTracing/RTWaterReflection.hlsl`
- `L207` `depthMismatch > threshold` の**ハード棄却**（SSR再投影先が別物体なら失敗）
- `L169` `edgeFade` … 再投影先が画面端に近いほど信頼度が落ちる

かすめ角では反射レイの再投影先が画面外・遮蔽になる画素が急増するため、
成功/失敗がピクセル単位で入り混じる。ここが滑らかなら二重像も目立ちにくい。

---

## 修正内容（案2で実装済み・2026-08-09）

**「2枚を混ぜる」構造そのものを廃止し、空も RT パスが実際にトレースしたレイの向きで解決する。**
Water.PS が受け取るのは常に解決済みの 1 枚だけになった。

| ファイル | 変更 |
|---|---|
| `RayTracing/GlobalRootSignatureManager.cpp` | DXR ルートシグネチャに**静的サンプラ s0**（線形・Clamp）を追加。従来 `NumStaticSamplers = 0` で `TextureCube.SampleLevel` が使えなかった。宣言しないシェーダーには影響しない追加のみ |
| `Water/RayTracing/RTWaterReflection.hlsl` | `gSkyEnvironmentMap : register(t5)` と `gLinearClamp : register(s0)` を追加。**レイがミス／再投影失敗のときは `ray.Direction` で空キューブを引いて成功として返す**。`gUnused0` → `gSkyEnvReflectionEnabled` へ転用（cbuffer レイアウト不変・192B のまま） |
| 同上 | **背景ピクセル（外洋）の早期 return を撤去。** ここが二重像の主因だった（不透明ジオメトリが無い水面には RT 反射が一切走らず、Water.PS の平面法線の空しか出ていなかった）。深度に依存しない視線方向を再構築し、`sceneDistance` は `gMaxRayDistance` を上限にする |
| 同上 | `depthMismatch > 閾値` の**ハード棄却を撤去**し `smoothstep` の信頼度に。画面端フェードと合わせて「同じレイ向きの空」との連続ブレンドにした（2 値切替を残さない） |
| `Water/RayTracing/WaterReflectionRayTracingManager.{h,cpp}` | `gSkyEnvironmentMap` を SRV テーブルへ追加。`Dispatch` に `skyEnvironmentSRV` 引数。未取得ならダミーを差して `skyEnvReflectionEnabled=0` |
| `EngineSystem/Subsystem/RayTracingSubsystem.cpp` | `AtmosphereManager::GetSkySpecularSRVHandle()` を反射パスへ渡す（`IsSkySpecularEnabled() && IsSkyEnvironmentReady()` のときだけ） |
| `Water/Surface/Water.PS.hlsl` | 空キューブの `reflect(-viewDir, float3(0,1,0))` を**廃止**。保険フォールバックだけ残し、そこも `geomNormal`（波法線）で引くので RT と面が食い違わない |

### 結果（同一構図・夕方）
- 二重像は解消。反射が波に沿った 1 枚の像になり、島とヤシの反射が水面を下ってくる形で出る
- 副次効果: 外洋にも RT 反射が効くようになり、平坦な空のベールが消えて水色が濃くなった（昼の構図でも確認）
- 60 FPS / 16.37 ms を維持（**VSync 上限なので「60 を割らない」ことしか言えない。余裕の増減は未計測**）

### 残っている既知の弱点
- SSR 由来の細かいスペックルは残る（`rtConfidence=1` 固定の A/B でも出ていた成分）
- 外洋ピクセルにもレイを飛ばすようになったので、**水面が画面を占める構図では反射パスのコストが増えている**（地平線より上を向く画素は TraceRay 前に弾かれるので増分は水面画素だけ）

---

## 直し方の案（採用前の検討メモ）

| 案 | 内容 | 備考 |
|---|---|---|
| A（筋がいい） | 空キューブも**波法線**で引く。まだら対策はラフネス依存のミップ選択で行う（`kWaterReflectionMicroRoughness` を法線のフットプリントから導出して `SampleLevel` の mip に反映） | #6 で二重像が消えるのは確認済み。そのままだと斑が増えるので mip 側の手当てが要る |
| B | A層とB層を混ぜず、**空はRTパス側で解決**する（レイがミスしたら RT シェーダー内で空キューブを引いて色を返す）。Water.PS は常に1枚だけ受け取る | 「2枚を混ぜる」構造自体を無くすので根本的。RTWaterReflection のミス処理を変更 |
| C（対症） | `rtConfidence` を空間的に平滑化してから lerp する | 二重像は薄まるがゼロにはならない |

---

## 調査に使った手順（再現用）

- **シェーダーは実行時にソースツリーから読まれるので C++ リビルド不要**。`Water.PS.hlsl` を書き換えて起動し直すだけで A/B できる
- 構図固定は `CVars.json` の `d.SceneCamera.Target/Distance/Pitch/Yaw/Fov`
  （`BaseScene::SetupCamera` の `SetUseSceneCamera` を一時 true にする必要がある。**調査後に復元済み**）
- 反射経路かどうかの一発判定は `r.Water.FresnelScale = 0`

**この調査で変更したファイルは全て復元し、Development をリビルド済み**（`git status` クリーン）。
