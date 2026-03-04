#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

/// @brief パーティクルの回転制御モジュール
/// 注意: 初期回転の設定はMainModuleで行います
/// このモジュールは回転速度と回転の変化のみを担当します

namespace CoreEngine
{
class RotationModule : public ParticleModule {
public:
    struct RotationData {
        Vector3 rotationSpeed = {0.0f, 0.0f, 0.0f};   // 回転速度（ラジアン/秒）
        Vector3 rotationSpeedRandomness = {0.0f, 0.0f, 0.0f}; // 回転速度のランダム性
        
        // 簡易2D回転（Z軸回転のみ）
        bool use2DRotation = true;                     // 2D回転モード（Z軸のみ）
        float rotation2DSpeed = 0.0f;                  // 2D回転速度（ラジアン/秒）
        float rotation2DSpeedRandomness = 0.0f;        // 2D回転速度のランダム性
        
        // 回転方向制御
        enum class RotationDirection {
            Clockwise,          // 時計回り
            CounterClockwise,   // 反時計回り
            Random,             // ランダム
            Both                // 両方向（パーティクルごとにランダム）
        };
        RotationDirection rotationDirection = RotationDirection::Random;
        
        // 高度な回転設定
        bool rotationOverLifetime = false;             // ライフタイムで回転速度変化
        float startRotationSpeedMultiplier = 1.0f;    // 開始時の回転速度倍率
        float endRotationSpeedMultiplier = 1.0f;      // 終了時の回転速度倍率
        
        // 角度制限
        bool limitRotationRange = false;              // 回転角度を制限するか
        Vector3 minRotation = {-180.0f, -180.0f, -180.0f}; // 最小回転角度（度）
        Vector3 maxRotation = {180.0f, 180.0f, 180.0f};    // 最大回転角度（度）
        
        // ビルボード連動
        bool alignToVelocity = false;                 // 移動方向に向けて回転
        float velocityAlignmentStrength = 1.0f;       // 移動方向への整列強度
    };

    RotationModule();
    ~RotationModule() = default;

    /// @brief 回転データを設定
    /// @param data 回転データ
    void SetRotationData(const RotationData& data) { rotationData_ = data; }

    /// @brief 回転データを取得
    /// @return 回転データの参照
    const RotationData& GetRotationData() const { return rotationData_; }

    /// @brief パーティクルに初期回転を適用
    /// @param particle 対象のパーティクル
    void ApplyInitialRotation(Particle& particle);

    /// @brief パーティクルの回転を更新
    /// @param particle 対象のパーティクル
    /// @param deltaTime フレーム時間
    void UpdateRotation(Particle& particle, float deltaTime);

#ifdef _DEBUG
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui() override;
#endif

private:
    RotationData rotationData_;
    
    /// @brief ライフタイム係数を取得
    /// @param particle パーティクル
    /// @return ライフタイム係数（0.0f - 1.0f）
    float GetLifetimeRatio(const Particle& particle);

    /// @brief 回転方向を決定
    /// @param direction 回転方向設定
    /// @return 回転方向係数（1.0f: 正方向, -1.0f: 逆方向）
    float GetRotationDirectionFactor(RotationData::RotationDirection direction);

    /// @brief 角度をラジアンに変換
    /// @param degrees 度
    /// @return ラジアン
    float DegreesToRadians(float degrees);

    /// @brief 角度を正規化（-π〜πの範囲に）
    /// @param angle ラジアン
    /// @return 正規化された角度
    float NormalizeAngle(float angle);

    /// @brief 移動方向に基づく回転を計算
    /// @param particle パーティクル
    /// @return 移動方向ベースの回転角度
    Vector3 CalculateVelocityAlignment(const Particle& particle);

    /// @brief ランダム性を適用
    /// @param baseValue ベース値
    /// @param randomness ランダム性
    /// @return ランダム性が適用された値
    float ApplyRandomness(float baseValue, float randomness);
};
}
