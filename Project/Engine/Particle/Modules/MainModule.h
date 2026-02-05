#pragma once

#include "ParticleModule.h"
#include "MathCore.h"

/// @brief パーティクルシステムのメインモジュール
/// Unityの「Main Module」に相当

namespace CoreEngine
{
class MainModule : public ParticleModule {
public:
    /// @brief シミュレーション空間
    enum class SimulationSpace {
        Local,  // ローカル空間（エミッターに追従）
        World   // ワールド空間（エミッターから独立）
    };

    /// @brief メインモジュールのデータ
    struct MainData {
        // ===== システム設定 =====
        float duration = 5.0f;   // 持続時間（秒）
        bool looping = true;// ループするか
        bool playOnAwake = true; // 開始時に自動再生するか
        uint32_t maxParticles = 1000;       // 最大パーティクル数
        SimulationSpace simulationSpace = SimulationSpace::World;  // シミュレーション空間

        // ===== パーティクルの初期設定（Start～） =====
        
        // 寿命
        float startLifetime = 1.0f;       // 初期寿命（秒）
        float startLifetimeRandomness = 0.0f;// 寿命のランダム性（0.0～1.0）

        // 速度
        float startSpeed = 5.0f; // 初期速度の大きさ
        float startSpeedRandomness = 0.0f;  // 速度のランダム性（0.0～1.0）

        // サイズ
        Vector3 startSize = { 1.0f, 1.0f, 1.0f };  // 初期サイズ
        float startSizeRandomness = 0.0f;   // サイズのランダム性（0.0～1.0）

        // 回転
        Vector3 startRotation = { 0.0f, 0.0f, 0.0f };  // 初期回転（度数）
        float startRotationRandomness = 0.0f;  // 回転のランダム性（0.0～1.0）

        // 色
        Vector4 startColor = { 1.0f, 1.0f, 1.0f, 1.0f };  // 初期色
        float startColorRandomness = 0.0f;  // 色のランダム性（0.0～1.0）

        // ===== スケーリング設定 =====
        float gravityModifier = 0.0f;   // 重力の影響度（削除予定：ForceModuleに移行）
    };

    MainModule();
    ~MainModule() = default;

    /// @brief メインデータを設定
    /// @param data メインデータ
    void SetMainData(const MainData& data) { mainData_ = data; }

    /// @brief メインデータを取得
    /// @return メインデータの参照
    const MainData& GetMainData() const { return mainData_; }

    /// @brief メインデータを取得（書き換え可能）
    /// @return メインデータの参照
    MainData& GetMainData() { return mainData_; }

    /// @brief システムの再生制御
    void Play();
    void Stop();
    void Restart();

    /// @brief 再生中かどうか
    /// @return 再生中の場合true
    bool IsPlaying() const { return isPlaying_; }

    /// @brief システム時間を更新
    /// @param deltaTime フレーム時間
    void UpdateTime(float deltaTime);

    /// @brief 現在の経過時間を取得
    /// @return 経過時間（秒）
    float GetElapsedTime() const { return elapsedTime_; }

    /// @brief システムが終了したかどうか（ループなしの場合）
    /// @return 終了している場合true
    bool IsFinished() const;

    /// @brief パーティクルの初期寿命を生成（ランダム性を適用）
    /// @return 寿命（秒）
    float GenerateStartLifetime() const;

    /// @brief パーティクルの初期速度を生成（ランダム性を適用）
    /// @return 速度
    float GenerateStartSpeed() const;

    /// @brief パーティクルの初期サイズを生成（ランダム性を適用）
    /// @return サイズ
    Vector3 GenerateStartSize() const;

    /// @brief パーティクルの初期回転を生成（ランダム性を適用）
    /// @return 回転（ラジアン）
    Vector3 GenerateStartRotation() const;

    /// @brief パーティクルの初期色を生成（ランダム性を適用）
    /// @return 色
    Vector4 GenerateStartColor() const;

#ifdef _DEBUG
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui() override;
#endif

private:
    MainData mainData_;
    float elapsedTime_ = 0.0f;  // 経過時間
    bool isPlaying_ = false;

    /// @brief ランダム値を生成（-range ～ +range）
    /// @param base 基本値
    /// @param randomness ランダム性（0.0～1.0）
    /// @return ランダム値
    float ApplyRandomness(float base, float randomness) const;
};
}
