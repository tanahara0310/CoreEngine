# Step 3 : Gerstner Wave（頂点変位による波）

> **状態:** ✅ 完了  
> **目的:** 物理ベースの波形「Gerstner Wave」により頂点を変位させ、立体的な波を作る  
> **位置付け:** メッシュ形状そのものが変化する最初のステップ。波の「動き」が生まれる

---

## 目標

- Vertex Shader 上で Gerstner Wave の数式を計算し、各頂点を XYZ 方向に変位させる
- 最大 4 本の波を重ね合わせ、複雑な波面を表現する
- 法線・接線を変位後の座標から再計算し、ライティングを正確にする
- 波のパラメータ（振幅・波長・速度・急峻度・方向）を Constant Buffer で GPU に渡す

---

## アルゴリズム

### Gerstner Wave の変位計算

Gerstner Wave は海洋学的に正確な波形で、横方向の変位（Steepness）を含む点が正弦波と異なる。

```
各波のパラメータ:
  D   = 進行方向（XZ平面の単位ベクトル）
  A   = 振幅（波の高さ）
  L   = 波長（山から山の距離）
  S   = 位相速度（波が進む速さ）
  Q   = 急峻度（0 = 正弦波、1 = 完全 Gerstner）

導出される値:
  k   = 2π / L          （波数）
  ω   = S * k            （角周波数）
  φ   = k * dot(D, xz) - ω * time  （位相）

変位:
  ΔX = Q * A * D.x * cos(φ)
  ΔZ = Q * A * D.z * cos(φ)
  ΔY = A * sin(φ)
```

### 法線・接線の再計算

頂点変位後は法線を再計算しないとライティングがおかしくなる。  
各波の偏微分を合算して法線・接線を更新する。

```
接線方向（X 軸）の偏微分:
  ΔTx.x += Q * A * D.x * D.x * (-sin(φ)) * k
  ΔTx.y += A * D.x * cos(φ) * k
  ΔTx.z += Q * A * D.x * D.z * (-sin(φ)) * k

法線:
  normal = normalize(cross(tangent, bitangent))
```

---

## 使用技術

| 技術名 | 役割 |
|--------|------|
| **Gerstner Wave** | 海洋学的に正確な波形モデル。正弦波と異なり横方向の変位（Steepness）を含み、尖った波頭と丸い波谷を表現できる |
| **Vertex Displacement（頂点変位）** | Vertex Shader 上で頂点座標を動的に変更する手法。GPU 上で計算するため CPU 負荷はほぼゼロ |
| **Wave Superposition（波の重ね合わせ）** | 複数の異なるパラメータの波を加算することで複雑な波面を生成する |
| **Normal Recalculation（法線再計算）** | 変位後の頂点座標から偏微分を使って法線・接線を再計算する。ライティングの正確さに直結する |
| **Custom Shader Pipeline** | 水面専用の VS / PS を独立した PSO として構築し、標準シェーダーとは分離する |

---

## 波パラメータの設計（WaveParams 構造体）

```cpp
struct WaveParams {
    Vector2 direction;  // 進行方向（XZ, 正規化済み）
    float   amplitude;  // 振幅（波の高さ）
    float   wavelength; // 波長（山から山の距離）
    float   speed;      // 位相速度（波が進む速さ）
    float   steepness;  // 横揺れ係数 Q（0=正弦波, 1=完全Gerstner）
    float   padding[2]; // 16 バイトアライメント用
};
// sizeof(WaveParams) == 32 であることを static_assert で保証する
```

HLSL 側の `WaveParams` 構造体とメモリレイアウトを一致させる必要がある。  
パディングの有無・順序に注意すること。

---

## D3D12 実装上の注意点

- **GBuffer パスではカスタムシェーダーが実行されない**  
  Gerstner Wave が動作するには Forward パスで描画する必要がある。  
  `BlendMode::kBlendModeNormal` を設定して GBuffer をバイパスすること。

- **gInstanceData の RootDescriptor 化**  
  カスタム RootSignature では `gInstanceData` を `RootDescriptor`（SRV）として扱う必要がある。  
  `DescriptorTable` のままでは `SetGraphicsRootShaderResourceView` と不整合になりクラッシュする。

- **カスタム RS 切り替え後のシーンリソース再バインド**  
  `SetGraphicsRootSignature` を呼ぶと以前のすべての root バインドが無効になる。  
  カメラ・ライト・IBL など共通リソースの再バインド処理が必要。

---

## 確認ポイント

- [x] 水面の頂点が上下・横方向に動いている
- [x] 複数波の重ね合わせで複雑な波面になっている
- [x] 法線再計算によりライティングが波形に追従している
- [x] ImGui から振幅・波長・速度・急峻度・方向をリアルタイム変更できる

---

*[← Step 2 UV スクロール](Step2_UVScroll.md) | [次: Step 4 Planar Reflection →](Step4_PlanarReflection.md)*
