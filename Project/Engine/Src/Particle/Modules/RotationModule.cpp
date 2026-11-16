#include "pch.h"
#include "RotationModule.h"
#include "Particle/Core/Particle.h"
#include "Math/MathCore.h"
#include <numbers>
#include <algorithm>

// コンストラクタでデフォルトパラメータを設定

namespace CoreEngine
{
RotationModule::RotationModule() {
    data_.rotationSpeed = { 0.0f, 0.0f, 0.0f };
    data_.rotationSpeedRandomness = { 0.0f, 0.0f, 0.0f };
    data_.use2DRotation = true;
    data_.rotation2DSpeed = 0.0f;
    data_.rotation2DSpeedRandomness = 0.0f;
    data_.rotationDirection = RotationData::RotationDirection::Random;
    data_.rotationOverLifetime = false;
    data_.startRotationSpeedMultiplier = 1.0f;
    data_.endRotationSpeedMultiplier = 1.0f;
    data_.limitRotationRange = false;
    data_.minRotation = { -180.0f, -180.0f, -180.0f };
    data_.maxRotation = { 180.0f, 180.0f, 180.0f };
    data_.alignToVelocity = false;
    data_.velocityAlignmentStrength = 1.0f;
}

void RotationModule::ApplyInitialRotation(Particle& particle)
{
    if (!enabled_) {
        return;
    }

    // 注意: 初期回転はMainModuleで設定されているため、ここでは回転速度のみを設定

    if (data_.use2DRotation) {
        // 2D回転モード（Z軸のみ）
        float rotationSpeed = data_.rotation2DSpeed;
        if (data_.rotation2DSpeedRandomness > 0.0f) {
            rotationSpeed = ApplyRandomness(rotationSpeed, data_.rotation2DSpeedRandomness);
        }
        
        // 回転方向を適用
        float directionFactor = GetRotationDirectionFactor(data_.rotationDirection);
        rotationSpeed *= directionFactor;
        
        // パーティクルの回転速度を設定（初期回転は変更しない）
        particle.rotationSpeed = {0.0f, 0.0f, rotationSpeed};
        
    } else {
        // 3D回転モード
        Vector3 rotationSpeed = data_.rotationSpeed;
        rotationSpeed.x = ApplyRandomness(rotationSpeed.x, data_.rotationSpeedRandomness.x);
        rotationSpeed.y = ApplyRandomness(rotationSpeed.y, data_.rotationSpeedRandomness.y);
        rotationSpeed.z = ApplyRandomness(rotationSpeed.z, data_.rotationSpeedRandomness.z);
        
        // パーティクルの回転速度を設定（初期回転は変更しない）
        particle.rotationSpeed = rotationSpeed;
    }
}

void RotationModule::UpdateRotation(Particle& particle, float deltaTime)
{
    if (!enabled_) {
        return;
    }

    // 現在の回転速度を取得
    Vector3 currentRotationSpeed = particle.rotationSpeed;
    
    // ライフタイムで回転速度を変化させる
    if (data_.rotationOverLifetime) {
        float lifetimeRatio = GetLifetimeRatio(particle);
        float speedMultiplier = data_.startRotationSpeedMultiplier + 
            (data_.endRotationSpeedMultiplier - data_.startRotationSpeedMultiplier) * lifetimeRatio;
        currentRotationSpeed.x *= speedMultiplier;
        currentRotationSpeed.y *= speedMultiplier;
        currentRotationSpeed.z *= speedMultiplier;
    }
    
    // 移動方向への整列
    if (data_.alignToVelocity) {
        Vector3 velocityAlignment = CalculateVelocityAlignment(particle);
        // 移動方向の回転を加算（強度で調整）
        currentRotationSpeed.x += velocityAlignment.x * data_.velocityAlignmentStrength;
        currentRotationSpeed.y += velocityAlignment.y * data_.velocityAlignmentStrength;
        currentRotationSpeed.z += velocityAlignment.z * data_.velocityAlignmentStrength;
    }
    
    // 回転を更新
    particle.transform.rotate.x += currentRotationSpeed.x * deltaTime;
    particle.transform.rotate.y += currentRotationSpeed.y * deltaTime;
    particle.transform.rotate.z += currentRotationSpeed.z * deltaTime;
    
    // 角度制限を適用
    if (data_.limitRotationRange) {
        float minX = MathCore::Constants::ToRadians(data_.minRotation.x);
        float maxX = MathCore::Constants::ToRadians(data_.maxRotation.x);
        float minY = MathCore::Constants::ToRadians(data_.minRotation.y);
        float maxY = MathCore::Constants::ToRadians(data_.maxRotation.y);
        float minZ = MathCore::Constants::ToRadians(data_.minRotation.z);
        float maxZ = MathCore::Constants::ToRadians(data_.maxRotation.z);
        
        particle.transform.rotate.x = std::clamp(particle.transform.rotate.x, minX, maxX);
        particle.transform.rotate.y = std::clamp(particle.transform.rotate.y, minY, maxY);
        particle.transform.rotate.z = std::clamp(particle.transform.rotate.z, minZ, maxZ);
    } else {
        // 角度を正規化（-π〜πの範囲に）
        particle.transform.rotate.x = MathCore::NormalizeAngle(particle.transform.rotate.x);
        particle.transform.rotate.y = MathCore::NormalizeAngle(particle.transform.rotate.y);
        particle.transform.rotate.z = MathCore::NormalizeAngle(particle.transform.rotate.z);
    }
}

#ifdef CORE_EDITOR
bool RotationModule::ShowImGui() {
    bool changed = false;

    // 2D/3D回転切り替え
    changed |= UI::Widgets::ToggleSwitch("2D回転（Z軸のみ）", &data_.use2DRotation);
    UI::Tooltip("ビルボード（板ポリ）の見た目回転は通常2Dで十分です。\n3Dモデルパーティクルには3D回転を使います。");

    if (data_.use2DRotation) {
        // 2D回転設定
        changed |= UI::DragFloat("回転速度（rad/秒）", data_.rotation2DSpeed, 0.1f, -10.0f, 10.0f);
        UI::SameLine();
        UI::HelpMarker("約6.28で1秒に1回転します。");
        changed |= UI::SliderFloat("速度ランダム幅", data_.rotation2DSpeedRandomness, 0.0f, 1.0f, "%.2f");

        // 回転方向設定
        static const char* directionNames[] = {
            "時計回り", "反時計回り", "ランダム", "両方向"
        };
        int currentDirection = static_cast<int>(data_.rotationDirection);
        if (ImGui::Combo("回転方向", &currentDirection, directionNames, IM_ARRAYSIZE(directionNames))) {
            data_.rotationDirection = static_cast<RotationData::RotationDirection>(currentDirection);
            changed = true;
        }
    } else {
        // 3D回転設定
        changed |= UI::DragVec3("回転速度（rad/秒）", data_.rotationSpeed, 0.1f, -10.0f, 10.0f);
        UI::SameLine();
        UI::HelpMarker("初期の向きはメインモジュールの「回転」で設定します。");
        changed |= UI::DragVec3("速度ランダム幅", data_.rotationSpeedRandomness, 0.01f, 0.0f, 1.0f);
    }

    // 詳細設定
    UI::SectionHeader("詳細");

    changed |= UI::Widgets::ToggleSwitch("寿命で回転速度を変化", &data_.rotationOverLifetime);
    if (data_.rotationOverLifetime) {
        changed |= UI::DragFloat("開始時の倍率", data_.startRotationSpeedMultiplier, 0.01f, 0.0f, 5.0f);
        changed |= UI::DragFloat("終了時の倍率", data_.endRotationSpeedMultiplier, 0.01f, 0.0f, 5.0f);
    }

    // 以下はCPU版のみ対応（GPU側のシェーダーには実装されていない）
    if (!gpuBackend_) {
        changed |= UI::Widgets::ToggleSwitch("進行方向へ整列", &data_.alignToVelocity);
        UI::Tooltip("移動方向に合わせて回転を加えます（火花や流星の軌跡向け）");
        if (data_.alignToVelocity) {
            changed |= UI::DragFloat("整列の強さ", data_.velocityAlignmentStrength, 0.01f, 0.0f, 2.0f);
        }

        changed |= UI::Widgets::ToggleSwitch("回転角度を制限", &data_.limitRotationRange);
        if (data_.limitRotationRange) {
            changed |= UI::DragVec3("最小角度（度）", data_.minRotation, 1.0f, -360.0f, 360.0f);
            changed |= UI::DragVec3("最大角度（度）", data_.maxRotation, 1.0f, -360.0f, 360.0f);
        }
    }

    return changed;
}
#endif

float RotationModule::GetLifetimeRatio(const Particle& particle)
{
    if (particle.lifeTime <= 0.0f) {
        return 1.0f;
    }
    
    float ratio = particle.currentTime / particle.lifeTime;
    return std::clamp(ratio, 0.0f, 1.0f);
}

float RotationModule::GetRotationDirectionFactor(RotationData::RotationDirection direction)
{
    switch (direction) {
        case RotationData::RotationDirection::Clockwise:
            return 1.0f;
            
        case RotationData::RotationDirection::CounterClockwise:
            return -1.0f;
            
        case RotationData::RotationDirection::Random:
        case RotationData::RotationDirection::Both:
            return random_.GetBool() ? -1.0f : 1.0f;
            
        default:
            return 1.0f;
    }
}

Vector3 RotationModule::CalculateVelocityAlignment(const Particle& particle)
{
    // 移動方向に基づく回転を計算
    Vector3 velocity = particle.velocity;
    float velocityLength = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    
    if (velocityLength < 0.001f) {
        return {0.0f, 0.0f, 0.0f}; // 速度がほぼゼロの場合は回転しない
    }
    
    // 正規化された速度ベクトル
    velocity.x /= velocityLength;
    velocity.y /= velocityLength;
    velocity.z /= velocityLength;
    
    // 移動方向に基づく回転角度を計算
    // これは簡易的な実装で、より複雑な計算が必要な場合は調整
    Vector3 alignment;
    alignment.x = atan2f(velocity.y, velocity.z); // ピッチ
    alignment.y = atan2f(velocity.x, velocity.z); // ヨー
    alignment.z = atan2f(velocity.y, velocity.x); // ロール
    
    return alignment;
}

float RotationModule::ApplyRandomness(float baseValue, float randomness)
{
    if (randomness <= 0.0f) {
        return baseValue;
    }
    
    // ±randomness の範囲でランダム係数を生成
    float randomOffset = random_.GetFloat(-randomness, randomness);
    
    return baseValue + randomOffset;
}
}
