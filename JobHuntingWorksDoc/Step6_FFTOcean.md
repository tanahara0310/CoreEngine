# Step 6 : FFT Ocean（本格的な波シミュレーション）

> **状態:** ⬜ 未着手  
> **目的:** フーリエ変換（FFT）を使って海洋学的に正確な波スペクトルからリアルな波面を生成する  
> **位置付け:** Gerstner Wave の「重ね合わせ」を数万波に拡張したもの。映像作品レベルの海を目指す

---

## このステップで行うこと

Gerstner Wave（Step 3）は手動で 4 本の波を設定したが、実際の海面は  
数千〜数万の波が重なり合っている。FFT Ocean はこれを GPU で高速計算する。

1. **Phillips スペクトルの生成** — 風向き・風速から初期周波数スペクトルを計算する
2. **IFFT による空間変換** — 周波数領域の波スペクトルを空間領域の変位マップに変換する
3. **変位マップの適用** — XYZ 3 チャンネルの変位マップを Vertex Shader でサンプリングして頂点に適用する
4. **法線マップの自動生成** — 変位マップの勾配から法線マップをリアルタイム生成する
5. **Jacobian による泡検出** — 波が崩れる（砕波）箇所を Jacobian から検出して泡テクスチャを重ねる

---

## アルゴリズム

### 全体フロー（毎フレーム）

```
[Compute Shader パス群]

1. Initial Spectrum Pass
   → Phillips スペクトル h0(k) を生成（起動時 or 風パラメータ変更時のみ）

2. Time Evolution Pass（毎フレーム）
   → h(k, t) = h0(k) * exp(i * ω(k) * t) で時刻 t のスペクトルを更新

3. IFFT Pass（毎フレーム）
   → 水平変位 Dx, Dz と垂直変位 Dy を 2D IFFT で空間変換

4. Normal / Jacobian Pass（毎フレーム）
   → 変位マップの勾配から法線マップと Jacobian を計算

[描画パス]

5. Vertex Shader
   → 変位テクスチャ（Displacement Map）をサンプリングして頂点を変位

6. Pixel Shader
   → 法線マップをサンプリングして PBR + Fresnel + 反射を計算
   → Jacobian から泡を発生させる
```

### Phillips スペクトル

```
波数ベクトル k に対するエネルギー分布:
  P(k) = A * exp(-1 / (k * L)^2) / k^4 * |dot(k_hat, wind_hat)|^2

  A        = 振幅スケール係数
  L        = V^2 / g （V=風速, g=重力加速度）
  k_hat    = k の単位ベクトル
  wind_hat = 風向きの単位ベクトル

初期スペクトル:
  h0(k) = (gaussian1 + i * gaussian2) / sqrt(2) * sqrt(P(k))
```

### 分散関係式（深海近似）

```
ω(k) = sqrt(g * |k|)  // g = 9.81 m/s^2

この式で波の角周波数が波数から導出される
→ 波長が長いほど速く進む（分散性）
```

### IFFT の次元と解像度

```
一般的な設定:
  解像度 N = 256 or 512 （N × N のタイル）
  タイルサイズ = 1000m × 1000m（実世界スケール）

Compute Shader での 2D FFT:
  1. 各行に対して 1D FFT を実行
  2. 各列に対して 1D FFT を実行
  → 計 2N 回の 1D FFT で 2D FFT が完成
```

---

## 変位マップの構成

FFT の出力は以下の 3 チャンネルテクスチャ：

| チャンネル | 内容 | 説明 |
|-----------|------|------|
| R | Dx（水平変位 X） | 波の横方向のうねり |
| G | Dy（垂直変位 Y） | 波の高さ |
| B | Dz（水平変位 Z） | 波の横方向のうねり |

---

## 使用技術

| 技術名 | 役割 |
|--------|------|
| **FFT（Fast Fourier Transform）** | 周波数領域のスペクトルを空間領域の変位マップに高速変換する |
| **Phillips Spectrum** | 風向き・風速から海面の周波数エネルギー分布を物理的に計算するモデル |
| **Compute Shader** | FFT 計算・法線生成・Jacobian 計算を GPU 上で並列実行する |
| **Displacement Map（変位マップ）** | FFT 出力の XYZ 変位を格納したテクスチャ。頂点シェーダーでサンプリングして使用する |
| **Jacobian Map** | 変位の偏微分から算出される値。1 未満になると波が崩れる（砕波）と判定し、泡を生成する |

---

## D3D12 実装上の注意点

- **Compute Shader のスレッドグループサイズと N の関係**  
  N = 256 の場合、スレッドグループ `[256, 1, 1]` を 256 回ディスパッチして各行 / 列の 1D FFT を処理する

- **UAV と SRV の切り替え**  
  Compute Shader が書き込む（UAV）テクスチャを、次のパスで読み込む（SRV）には  
  リソースバリア `D3D12_RESOURCE_STATE_UNORDERED_ACCESS → D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` が必要

- **タイルの継ぎ目処理**  
  N 枚のタイルをタイリングする際に端の継ぎ目が目立たないよう、  
  変位マップの端をラップしてサンプリングする（`D3D12_TEXTURE_ADDRESS_MODE_WRAP`）

---

## 確認ポイント

- [ ] Compute Shader が正常にディスパッチされている
- [ ] 変位マップが毎フレーム更新されている
- [ ] 波面が自然なうねりを持っている（Phillips スペクトルの影響が見える）
- [ ] 波長の長い波が速く、短い波が遅く動いている（分散性）
- [ ] Jacobian から泡が発生する箇所が特定できる

---

*[← Step 5 Normal Map + PBR](Step5_NormalMap_PBR.md) | [次: Step 7 SSR / Caustics / Foam →](Step7_SSR_Caustics_Foam.md)*
