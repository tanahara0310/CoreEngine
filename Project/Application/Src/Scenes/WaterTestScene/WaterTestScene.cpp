#include "pch.h"
#include "WaterTestScene.h"

#include "Graphics/Water/Render/WaterRenderFeature.h"
#include "Math/MathCore.h"
#include "Scene/SceneManager.h"
#include "Utility/FrameRate/FrameRateController.h"
#include <cmath>
#include <memory>

using namespace CoreEngine;

namespace {
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

    // 太陽高度角・方位角からライト方向（太陽 → 地表への進行方向）を計算する。
    // AtmosphereEditor::ComputeSunLightDirection と同じ規約（elevation=90°で天頂）。
    Vector3 ComputeSunLightDirection(float elevationDeg, float azimuthDeg) {
        const float elevation = elevationDeg * kDegToRad;
        const float azimuth = azimuthDeg * kDegToRad;
        const Vector3 toSun = {
            std::cos(elevation) * std::sin(azimuth),
            std::sin(elevation),
            std::cos(elevation) * std::cos(azimuth),
        };
        return CoreEngine::Normalize(-toSun);
    }
}

void WaterTestScene::OnInitialize() {
    SetSceneName("WaterTestScene");

    // 既定床は使わない。このシーンは海面（y≈0）と島の地形を自前で持っているため、
    // y=0 に平らな床を置くと水面と Z 争いを起こし、海底の地形とも二重になる。
    SetDefaultGroundEnabled(false);

    // ===== 太陽ライト =====
    // BaseScene::SetupLight() の既定値（天頂・intensity=1）は通常の直接光用の目安であり、
    // 大気散乱が期待する輝度スケール（AtmosphereEditorSunSettings 既定値は intensity=20）とは
    // 整合しない。既定背景が大気散乱モードになったため、AtmosphereTestScene と同じ考え方で
    // 見栄えの良い太陽高度・強度に調整し、水面反射／コースティクスが参照する太陽と
    // 空に映る太陽を一致させる。
    if (directionalLight_) {
        directionalLight_->direction = ComputeSunLightDirection(35.0f, 25.0f);
        // 空（大気・雲）の輝度スケールと、サーフェスの直接光は単位系が別なので分離して与える。
        // 両方に 20 を入れると床のような明るいアルベドが ACES の飽和域に入り真っ白になる。
        directionalLight_->atmosphereIntensity = 20.0f;
        directionalLight_->intensity = kAtmosphereSunIlluminanceLux;
    }

    // 水面一式（水面オブジェクト・波シミュレーション・リソース結線）は Feature が持つ。
    // シーン側は登録するだけで、以降の毎フレーム処理に手を入れる必要がない。
    auto* waterFeature = static_cast<WaterRenderFeature*>(
        AddFeature(std::make_unique<WaterRenderFeature>()));
    waterController_.Initialize(waterFeature, *engine_);

    // 起動時のリリースカメラは「1 カット見せる → 黒へフェード → 暗転中に構図を差し替える」を巡回する。
    // 構図を 1 つに固定すると、水面すれすれの視点では大気散乱の白いもやが画面の半分を占め、
    // 逆に高い俯瞰では水の表情が見えない——どれか 1 つを選ぶ必要をなくすための演出。
    // 各カットの数値は実機のスクリーンショットで決めた（カメラが埋まる方角・
    // 海底メッシュの切れ目が正面に来る方角は候補から外してある）。
    // 既定の画角 0.45rad(≒25.8°) は風景には狭く、既定のファークリップ 1000m では
    // 水面メッシュ（4000m 四方）が水平線の手前で切れるため、カットごとにレンズも与える。
    cameraShowcase_.Initialize(
        engine_,
        {
            // ① 礁湖から外洋へ抜ける水路。手前は浅瀬（コースティクス・砕波泡）、左右を岩と椰子が締める
            { { 55.0f, 15.0f, -180.0f }, { 0.0166f, 0.0942f, 0.0f }, 50.0f, 20000.0f },
            // ② 南から主島を正面に。島の全景と外洋のうねりが同時に入る
            { { 60.0f, 14.0f, -160.0f }, { 0.0342f, -0.3488f, 0.0f }, 55.0f, 20000.0f },
            // ③ 北側の浅瀬から順光で。水の透明感とコースティクスが最も出る向き
            { { 8.0f, 20.0f, 143.0f }, { 0.0798f, -3.1416f, 0.0f }, 55.0f, 20000.0f },
            // ④ 島々に囲まれた内側の礁湖。椰子が両側からフレームになる
            { { 170.0f, 30.0f, 130.0f }, { 0.0708f, -2.5454f, 0.0f }, 55.0f, 20000.0f },
            // ⑤ 高所からの俯瞰。島の連なりと雲の広がりでスケールを見せる
            { { 10.0f, 35.0f, 190.0f }, { 0.1155f, -3.1216f, 0.0f }, 55.0f, 20000.0f },
            // ⑥ 外洋から島影を望む。手前は深場のうねりと白波だけの構図
            { { -30.0f, 10.0f, -230.0f }, { -0.0187f, 0.1882f, 0.0f }, 60.0f, 20000.0f },
        },
        [this](const CameraShowcase::Shot& shot) {
            SetReleaseCameraTransform(shot.translate, shot.rotate);
            SetReleaseCameraLens(shot.fovDegrees, shot.farClip);
        });
}

void WaterTestScene::OnUpdate() {
    auto* frameRate = engine_ ? engine_->GetService<FrameRateController>() : nullptr;
    cameraShowcase_.Update(frameRate ? frameRate->GetDeltaTime() : 0.016f);
}

void WaterTestScene::Draw() {
    BaseScene::Draw();
}

void WaterTestScene::OnFinalize() {
    // シーン遷移中はフェードの主導権が SceneTransition にあるので触らない
    // （暗転しているはずの画が 1 フレーム戻ってしまう）
    auto* sceneManager = engine_ ? engine_->GetSceneManager() : nullptr;
    cameraShowcase_.Shutdown(sceneManager && sceneManager->IsTransitioning());

    // WaterRenderFeature の所有者は BaseScene（この直後に features_ が破棄される）。
    // UI が Feature ポインタを持ったままにならないよう、ここで先に切る。
    waterController_.Shutdown();
}

std::vector<RenderViewRequest> WaterTestScene::BuildRenderViewRequests()
{
    // 鏡像カメラによる平面反射ビューは廃止した。
    // 水面反射は DXR（RTWaterReflectionPass）へ置き換え済みで、シーン全体を
    // もう一周描画する反射ビューは不要になった（反射コストがシーン複雑度から独立化）。
    // 反射ビューを発行しないことで、GBuffer/FFTOcean/ライティングの二重実行が消える。
    return {};
}


