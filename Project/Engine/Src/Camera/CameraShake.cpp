#include "CameraShake.h"
#include <cmath>


namespace CoreEngine
{
void CameraShake::Start(float duration, float intensity, float frequency) {
    intensity_ = intensity;
    frequency_ = frequency;
    currentOffset_ = { 0.0f, 0.0f, 0.0f };

    // メインタイマーを開始（シェイクの持続時間）
    shakeTimer_.Start(duration, false);
    shakeTimer_.SetName("ShakeTimer");

    // 揺れの間隔タイマーを開始
    float shakeInterval = 1.0f / frequency;
    shakeIntervalTimer_.Start(shakeInterval, true);  // ループする
    shakeIntervalTimer_.SetName("ShakeIntervalTimer");

    // ループごとに新しいオフセットを生成
    shakeIntervalTimer_.SetOnLoopComplete([this]() {
        if (shakeTimer_.IsActive()) {
            // 時間経過に応じて振幅を減衰（減衰カーブを適用）
            float progress = shakeTimer_.GetProgress();
            float decayFactor = 1.0f - std::pow(progress, decayCurve_);
            float currentAmplitude = intensity_ * decayFactor;

            // ランダムなオフセットを生成
            currentOffset_ = GenerateRandomOffset(currentAmplitude);
        }
    });

    // 初回のオフセットを即座に生成
    currentOffset_ = GenerateRandomOffset(intensity_);
}

void CameraShake::Stop() {
    shakeTimer_.Stop();
    shakeIntervalTimer_.Stop();
    currentOffset_ = { 0.0f, 0.0f, 0.0f };
}

void CameraShake::Update(float deltaTime) {
    // メインタイマーの更新
    shakeTimer_.Update(deltaTime);

    // シェイクが終了したら停止
    if (shakeTimer_.IsFinished()) {
        Stop();
        return;
    }

    // 間隔タイマーの更新（揺れの頻度を管理）
    if (shakeTimer_.IsActive()) {
        shakeIntervalTimer_.Update(deltaTime);
    }
}

Vector3 CameraShake::GenerateRandomOffset(float amplitude) {
    return Vector3{
        random_.GetFloatSigned() * amplitude,
        random_.GetFloatSigned() * amplitude,
        random_.GetFloatSigned() * amplitude * 0.5f  // Z軸は控えめに
    };
}
}
