#include "RotationModule.h"
#include "../ParticleSystem.h"
#include <numbers>
#include <algorithm>

// コンストラクタでデフォルトパラメータを設定

namespace CoreEngine
{
RotationModule::RotationModule() {
    rotationData_.rotationSpeed = { 0.0f, 0.0f, 0.0f };
    rotationData_.rotationSpeedRandomness = { 0.0f, 0.0f, 0.0f };
    rotationData_.use2DRotation = true;
    rotationData_.rotation2DSpeed = 0.0f;
    rotationData_.rotation2DSpeedRandomness = 0.0f;
    rotationData_.rotationDirection = RotationData::RotationDirection::Random;
    rotationData_.rotationOverLifetime = false;
    rotationData_.startRotationSpeedMultiplier = 1.0f;
    rotationData_.endRotationSpeedMultiplier = 1.0f;
    rotationData_.limitRotationRange = false;
    rotationData_.minRotation = { -180.0f, -180.0f, -180.0f };
    rotationData_.maxRotation = { 180.0f, 180.0f, 180.0f };
    rotationData_.alignToVelocity = false;
    rotationData_.velocityAlignmentStrength = 1.0f;
}

void RotationModule::ApplyInitialRotation(Particle& particle)
{
    if (!enabled_) {
        return;
    }

    // 注意: 初期回転はMainModuleで設定されているため、ここでは回転速度のみを設定

    if (rotationData_.use2DRotation) {
        // 2D回転モード（Z軸のみ）
        float rotationSpeed = rotationData_.rotation2DSpeed;
        if (rotationData_.rotation2DSpeedRandomness > 0.0f) {
            rotationSpeed = ApplyRandomness(rotationSpeed, rotationData_.rotation2DSpeedRandomness);
        }
        
        // 回転方向を適用
        float directionFactor = GetRotationDirectionFactor(rotationData_.rotationDirection);
        rotationSpeed *= directionFactor;
        
        // パーティクルの回転速度を設定（初期回転は変更しない）
        particle.rotationSpeed = {0.0f, 0.0f, rotationSpeed};
        
    } else {
        // 3D回転モード
        Vector3 rotationSpeed = rotationData_.rotationSpeed;
        rotationSpeed.x = ApplyRandomness(rotationSpeed.x, rotationData_.rotationSpeedRandomness.x);
        rotationSpeed.y = ApplyRandomness(rotationSpeed.y, rotationData_.rotationSpeedRandomness.y);
        rotationSpeed.z = ApplyRandomness(rotationSpeed.z, rotationData_.rotationSpeedRandomness.z);
        
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
    if (rotationData_.rotationOverLifetime) {
        float lifetimeRatio = GetLifetimeRatio(particle);
        float speedMultiplier = rotationData_.startRotationSpeedMultiplier + 
            (rotationData_.endRotationSpeedMultiplier - rotationData_.startRotationSpeedMultiplier) * lifetimeRatio;
        currentRotationSpeed.x *= speedMultiplier;
        currentRotationSpeed.y *= speedMultiplier;
        currentRotationSpeed.z *= speedMultiplier;
    }
    
    // 移動方向への整列
    if (rotationData_.alignToVelocity) {
        Vector3 velocityAlignment = CalculateVelocityAlignment(particle);
        // 移動方向の回転を加算（強度で調整）
        currentRotationSpeed.x += velocityAlignment.x * rotationData_.velocityAlignmentStrength;
        currentRotationSpeed.y += velocityAlignment.y * rotationData_.velocityAlignmentStrength;
        currentRotationSpeed.z += velocityAlignment.z * rotationData_.velocityAlignmentStrength;
    }
    
    // 回転を更新
    particle.transform.rotate.x += currentRotationSpeed.x * deltaTime;
    particle.transform.rotate.y += currentRotationSpeed.y * deltaTime;
    particle.transform.rotate.z += currentRotationSpeed.z * deltaTime;
    
    // 角度制限を適用
    if (rotationData_.limitRotationRange) {
        float minX = DegreesToRadians(rotationData_.minRotation.x);
        float maxX = DegreesToRadians(rotationData_.maxRotation.x);
        float minY = DegreesToRadians(rotationData_.minRotation.y);
        float maxY = DegreesToRadians(rotationData_.maxRotation.y);
        float minZ = DegreesToRadians(rotationData_.minRotation.z);
        float maxZ = DegreesToRadians(rotationData_.maxRotation.z);
        
        particle.transform.rotate.x = std::clamp(particle.transform.rotate.x, minX, maxX);
        particle.transform.rotate.y = std::clamp(particle.transform.rotate.y, minY, maxY);
        particle.transform.rotate.z = std::clamp(particle.transform.rotate.z, minZ, maxZ);
    } else {
        // 角度を正規化（-π〜πの範囲に）
        particle.transform.rotate.x = NormalizeAngle(particle.transform.rotate.x);
        particle.transform.rotate.y = NormalizeAngle(particle.transform.rotate.y);
        particle.transform.rotate.z = NormalizeAngle(particle.transform.rotate.z);
    }
}

#ifdef _DEBUG
bool RotationModule::ShowImGui() {
    bool changed = false;
    
    // 有効/無効の切り替え
    if (ImGui::Checkbox("有効##回転", &enabled_)) {
        changed = true;
    }

    if (!enabled_) {
        ImGui::BeginDisabled();
    }

    // 2D/3D回転切り替え
    changed |= ImGui::Checkbox("2D回転使用", &rotationData_.use2DRotation);

    if (rotationData_.use2DRotation) {
        // 2D回転設定
        changed |= ImGui::DragFloat("回転速度", &rotationData_.rotation2DSpeed, 0.1f, -10.0f, 10.0f);
        changed |= ImGui::DragFloat("速度ランダム性", &rotationData_.rotation2DSpeedRandomness, 0.01f, 0.0f, 1.0f);
        
        // 回転方向設定
        static const char* directionNames[] = {
            "時計回り", "反時計回り", "ランダム", "両方向"
        };
        int currentDirection = static_cast<int>(rotationData_.rotationDirection);
        if (ImGui::Combo("回転方向", &currentDirection, directionNames, IM_ARRAYSIZE(directionNames))) {
            rotationData_.rotationDirection = static_cast<RotationData::RotationDirection>(currentDirection);
            changed = true;
        }
    } else {
        // 3D回転設定
        ImGui::TextDisabled("注意: 初期回転はMainModuleで設定してください");
        changed |= ImGui::DragFloat3("回転速度", &rotationData_.rotationSpeed.x, 0.1f, -10.0f, 10.0f);
        changed |= ImGui::DragFloat3("速度ランダム性", &rotationData_.rotationSpeedRandomness.x, 0.01f, 0.0f, 1.0f);
    }

    // 高度な設定
    ImGui::Separator();
    changed |= ImGui::Checkbox("寿命に応じた回転", &rotationData_.rotationOverLifetime);
    if (rotationData_.rotationOverLifetime) {
        changed |= ImGui::DragFloat("開始速度倍率", &rotationData_.startRotationSpeedMultiplier, 0.01f, 0.0f, 5.0f);
        changed |= ImGui::DragFloat("終了速度倍率", &rotationData_.endRotationSpeedMultiplier, 0.01f, 0.0f, 5.0f);
    }

    changed |= ImGui::Checkbox("速度に合わせて回転", &rotationData_.alignToVelocity);
    if (rotationData_.alignToVelocity) {
        changed |= ImGui::DragFloat("速度合わせ強度", &rotationData_.velocityAlignmentStrength, 0.01f, 0.0f, 2.0f);
    }

    // 角度制限
    changed |= ImGui::Checkbox("角度制限", &rotationData_.limitRotationRange);
    if (rotationData_.limitRotationRange) {
        changed |= ImGui::DragFloat3("最小角度(度)", &rotationData_.minRotation.x, 1.0f, -360.0f, 360.0f);
        changed |= ImGui::DragFloat3("最大角度(度)", &rotationData_.maxRotation.x, 1.0f, -360.0f, 360.0f);
    }

    if (!enabled_) {
        ImGui::EndDisabled();
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

float RotationModule::DegreesToRadians(float degrees)
{
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

float RotationModule::NormalizeAngle(float angle)
{
    while (angle > std::numbers::pi_v<float>) {
        angle -= 2.0f * std::numbers::pi_v<float>;
    }
    while (angle < -std::numbers::pi_v<float>) {
        angle += 2.0f * std::numbers::pi_v<float>;
    }
    return angle;
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
