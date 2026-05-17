# Step 2 : UV スクロール（流れるだけの水）

> **状態:** ✅ 完了  
> **目的:** テクスチャの UV 座標を時間でオフセットし、水が流れているように見せる  
> **位置付け:** メッシュ形状には変化なし。視覚的な「動き」を最小コストで加える最初のステップ

---

## 目標

- CPU から GPU へ `time`（経過時間）を Constant Buffer で毎フレーム送る
- Pixel Shader 上で `uv += time * speed` を計算してスクロールを実現する
- 2 方向・2 速度の UV スクロールを重ね合わせて単調さを取り除く
- テクスチャは `D3D12_TEXTURE_ADDRESS_MODE_WRAP` でタイリング設定する

---

## アルゴリズム

### UV スクロール計算

```
毎フレーム:
  uvOffset += scrollSpeed * deltaTime
  uvOffset = fmod(uvOffset, 1.0f)   // 0〜1 に折り返す（精度劣化防止）

Pixel Shader:
  float2 uv = input.texcoord * tiling + uvOffset
  float4 color = texture.Sample(sampler, uv)
```

### 2 枚重ね合わせの効果

方向・速度が異なる UV スクロールを 2 回サンプリングして合成すると、  
規則的な繰り返しが崩れ、より自然な「水面の揺らぎ感」が生まれる。

```
uv1 = texcoord * tiling + float2(time * speed1.x, time * speed1.y)
uv2 = texcoord * tiling + float2(time * speed2.x, time * speed2.y)
color = lerp(sample(uv1), sample(uv2), 0.5)
```

---

## 使用技術

| 技術名 | 役割 |
|--------|------|
| **UV スクロール** | `uv += time * speed` で毎フレームテクスチャ座標をずらす最もシンプルな水の動き |
| **Tiling（テクスチャタイリング）** | `uv * tiling` で 1 枚のテクスチャを大きな水面に対応させる |
| **Constant Buffer（cbuffer）** | CPU → GPU へ毎フレーム値（時間・その他）を渡す定数バッファ。D3D12 では **256 バイトアライメント**が必要 |
| **Sampler State（サンプラーステート）** | テクスチャのフィルタリング方法・ラッピング方法を設定するオブジェクト。タイリングには `D3D12_TEXTURE_ADDRESS_MODE_WRAP` を使用 |

---

## D3D12 実装上の注意点

### Constant Buffer の 256 バイトアライメント

D3D12 では Constant Buffer のサイズを **256 バイト境界に揃える** 必要がある。  
`CreateCommittedResource` で Upload ヒープを確保し、データの更新は `Map / Unmap` で行う。  
D3D11 の `UpdateSubresource` に直接対応する API は存在しないため注意。

```
// アライメント計算式
UINT alignedSize = (sizeof(MyConstantBuffer) + 255) & ~255u;
```

### サンプラーのアドレスモード

テクスチャをタイリングするには以下の設定が必要：

```
D3D12_TEXTURE_ADDRESS_MODE_WRAP  // UV が 0〜1 を超えると折り返す
```

---

## 確認ポイント

- [x] テクスチャが流れるように動いている
- [x] `tiling` 値を変えるとテクスチャの繰り返し回数が変わる
- [x] 複数方向にスクロールすると複雑さが増す

---

*[← Step 1 グリッドメッシュ](Step1_GridMesh.md) | [次: Step 3 Gerstner Wave →](Step3_GerstnerWave.md)*
