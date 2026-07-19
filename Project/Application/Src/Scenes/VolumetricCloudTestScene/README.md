# VolumetricCloudTestScene

ボリューメトリック雲（Volumetric Cloud）システムの実装・検証専用シーン。

## 目的

アンリアルエンジンの Volumetric Cloud コンポーネントが採用している
Schneider "Nubis" 方式（"The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn",
"Nubis: Authoring Real-Time Volumetric Cloudscapes with the Decima Engine"）を
ベースにした雲レンダリングを、本番シーンから隔離された環境で段階的に実装・検証する。

## 構成

| ファイル | 役割 |
|---|---|
| `VolumetricCloudTestScene.h/.cpp` | シーン本体。太陽用 DirectionalLight 1灯と床の Plane のみを配置する。雲の更新は `BaseScene::UpdateAtmosphere()` が大気散乱の直後に毎フレーム自動で行う |

編集 UI（旧 `CloudEditorFacade`）は雲が全シーン既定機能になったのに伴い、
エンジン常駐の `Engine/Src/Editor/Environment/VolumetricCloudEditor.h/.cpp`（DebugSubsystem 所有）へ移設した。
どのシーンでも Hierarchy の Environment ツリー →「Volumetric Cloud」から編集でき、
天候プリセット（快晴／部分的な曇り／曇天／高い薄雲／嵐の前）を1クリックで適用できる。

## エンジン側の関連実装

- `Engine/Src/Graphics/Cloud/VolumetricCloudManager.h/.cpp`
  — 雲パラメータ・CB・ノイズテクスチャ（Base Shape / Detail / Weather Map）・
  レイマーチ/合成パイプラインの管理。`RenderDomainContext` が所有し、
  `RenderContext::volumetricCloudManager` として各 RenderPass へ供給される。
  太陽情報は `AtmosphereManager` から取得する（単一情報源）
- `Engine/Src/Graphics/Render/Pass/VolumetricCloudNoisePass.h/.cpp`
  — ノイズ生成パス（FrameSetup フェーズ、AtmosphereLUTPass の後）。
  ダーティ時のみノイズテクスチャを再生成する
- `Engine/Src/Graphics/Render/Pass/VolumetricCloudPass.h/.cpp`
  — レイマーチ＋合成パス（Sky フェーズ、SkyBoxQueuePass の直後・GameView のみ）
- `Engine/Assets/Shaders/Cloud/`
  — `Common/CloudNoiseCommon.hlsli`（ハッシュ・Perlin・Worley・FBM）、
  `Common/CloudCommon.hlsli`（共通定数・雲層ジオメトリ・密度関数）、
  `CloudBaseShapeNoise.CS` / `CloudDetailNoise.CS` / `CloudWeatherMap.CS`（ノイズ生成）、
  `CloudRayMarch.CS`（雲レイマーチ・ライティング）、`CloudComposite.CS`（SceneColor 合成）

## 設計方針からの変更点（記録）

実装中に判明した、当初設計書（`Docs/Engine/Graphics/Rendering/VolumetricCloud_Design.md`）
からの逸脱点。詳細は同ドキュメントのチェックリストを参照:

- 単一散乱＋Powder のみだと `densityScale` を上げた際に光学的深さが飽和し、
  濃い雲の内部が真っ黒になる。Hillaire/Frostbite の多重散乱オクターブ近似を追加した
- サンライトマーチ（セルフシャドウ）に画面空間ジッタ（IGN）を使うと、
  `exp()` の急峻さでジッタの周期性が増幅され、雲面に格子状の模様が出る。
  ジッタを使わない指数ステップ（近傍密・遠方粗）に変更した
- Worley FBM の最高オクターブがノイズテクスチャ解像度のナイキスト限界に達すると、
  生成時点でエイリアシングしたパターンが焼き込まれる。`WorleyFBM3D_Safe` で
  テクスチャ解像度の 1/4 を上限にクランプしている
- 半解像度でレイマーチした雲バッファをバイリニアでアップサンプルすると、
  手前の不透明物の輪郭ににじみ（ハロ）が出る。不透明物が雲層の最近距離より
  手前にある画素では雲を合成しないことで回避した（`CloudComposite.CS.hlsl`）

## 座標系の前提

- ワールド座標は 1unit = 1m（大気散乱と共通）
- 雲層は惑星中心を共有する球殻（内殻半径 = 惑星半径 + 雲底高度、外殻半径 = 内殻 + 層厚）
- カメラは雲層の下・中・上のいずれの高度にあっても正しく描画される

## 実装フェーズと現状

`Docs/Engine/Graphics/Rendering/VolumetricCloud_Design.md` の 9 章を参照。
Phase 0〜5 完了、本シーンは Phase 6（デフォルト背景統合・エディタ・検証シーン）の成果物。

## スコープ外（将来対応）

- テンポラル再投影・再構成（品質・負荷の改善）
- 雲影（Cloud Shadow Map）の地表への落とし込み
- ReflectionView（水面反射）への雲の映り込み
- 雲の IBL 寄与
- Texture3D ミップチェーン生成・天候マップの外部アセット化
