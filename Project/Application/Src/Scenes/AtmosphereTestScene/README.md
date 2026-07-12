# AtmosphereTestScene

大気散乱（Sky Atmosphere）システムの実装・検証専用シーン。

## 目的

アンリアルエンジンの SkyAtmosphereComponent が採用している
Bruneton/Hillaire 方式（"A Scalable and Production Ready Sky and Atmosphere
Rendering Technique", Hillaire 2020）をベースにした大気散乱レンダリングを、
本番シーンから隔離された環境で段階的に実装・検証する。

## 構成

| ファイル | 役割 |
|---|---|
| `AtmosphereTestScene.h/.cpp` | シーン本体。太陽用 DirectionalLight 1灯（`isAtmosphereSun = true`）と床の Plane のみを配置し、毎フレーム `AtmosphereManager::Update()` を呼ぶ |
| `AtmosphereEditorFacade.h/.cpp` | ImGui パネルと Engine 内部（`AtmosphereManager` / `LightManager`）の仲介。太陽の高度角・方位角・強度の編集と診断情報の表示を担当 |

WaterTestScene（シーン本体＋ EditorFacade）と同じ役割分担を踏襲している。

## エンジン側の関連実装

- `Engine/Src/Graphics/Atmosphere/AtmosphereManager.h/.cpp`
  — 大気パラメータ・太陽情報・カメラ高度・LUT 群（Transmittance / Multi-Scattering /
  Sky-View / Camera Volume）・Aerial Perspective 合成の管理。`RenderDomainContext` が所有し、
  `RenderContext::atmosphereManager` として各 RenderPass へ供給される
- `Engine/Src/Graphics/Render/Pass/AtmosphereLUTPass.h/.cpp`
  — LUT 生成パス（ShadowMap 後・GBuffer 前）。ダーティフラグ制御:
  大気パラメータ変更→全 LUT 再生成 / 太陽方向・カメラ高度変更→Sky-View のみ /
  カメラ姿勢変更→Camera Volume のみ。全て不変なら再計算しない
- `Engine/Src/Graphics/Render/Pass/AerialPerspectivePass.h/.cpp`
  — 空気遠近感合成パス（DeferredLighting 後・Geometry 前、GameView のみ）
- `Engine/Assets/Shaders/Atmosphere/`
  — `Common/AtmosphereCommon.hlsli`（共通数式・LUT パラメータ化）、
  `TransmittanceLUT.CS` / `MultiScatteringLUT.CS` / `SkyViewLUT.CS` / `CameraVolumeLUT.CS`（LUT 生成）、
  `SkyAtmosphere.PS`（空描画 = Sky-View LUT 1サンプル＋太陽ディスク解析描画）、
  `AerialPerspective.CS`（シーンへの霞合成）
- `Engine/Src/Graphics/Render/SkyBox/SkyBoxRenderer.h/.cpp`
  — 大気散乱モード用の第2パイプラインを追加（キューブマップ版と VS・メッシュ・深度設定共通）。
  `SkyBoxObject::SetAtmosphereMode(true)` でオプトインする（他シーンのキューブマップ空は無影響）
- `Engine/Src/Graphics/Light/LightData.h`
  — `DirectionalLightData::isAtmosphereSun`（大気の太陽として扱うライトの目印。
  UE の Atmosphere Sun Light フラグに相当）
- `Engine/Src/Graphics/Light/LightManager.h/.cpp`
  — `GetAtmosphereSunLight()`（フラグ付きライトの検出。無ければインデックス0へフォールバック）

## 設計方針からの変更点（記録）

- 当初方針の「`Skybox.PS.hlsl` の中身を差し替え」は、HDR キューブマップを使う他シーン
  （PrimitiveTestScene 等）の空まで大気に置き換わってしまうため、
  **`SkyAtmosphere.PS.hlsl` を新設し SkyBoxRenderer に大気モード用の第2パイプラインを追加する**
  方式に変更した（メッシュ・VS・深度設定の流用という設計意図は維持）
- AP 合成の深度スカイ判定は far クリップが大きい場合を考慮し、しきい値を 0.9999999 とした
  （far=50km の標準深度では 2km 先の不透明物でも深度が 0.9999 を超えるため）

## 座標系の前提

- ワールド座標は 1unit = 1m
- ワールド全体を惑星スケールへは変換しない。カメラの Y 座標のみを
  `groundLevelY`（既定0）基準の高度として惑星中心距離へ変換する
- 惑星半径 6,360km・大気圏上端 6,460km は大気計算専用の定数

## 実装フェーズと現状

- [x] Phase 0: 土台（太陽フラグ・テストシーン・AtmosphereManager 骨組み）
- [x] Phase 1: 単一散乱によるパラメトリックスカイ（レイマーチング）
- [x] Phase 2: Transmittance LUT
- [x] Phase 3: Multi-Scattering LUT
- [x] Phase 4: Sky-View LUT
- [x] Phase 5: 太陽ディスクの解析的描画
- [x] Phase 6: Aerial Perspective（空気遠近感）
- [x] Phase 7: パラメータ公開・仕上げ（ImGui で散乱係数・オゾン・アルベド・太陽ディスク等を編集可能）

## スコープ外（将来対応）

- 水面など ReflectionView への大気の映り込み
- 大気出力の IBL（環境光・映り込み）ソース化
