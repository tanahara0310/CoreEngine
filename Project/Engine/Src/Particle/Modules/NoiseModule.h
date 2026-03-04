#pragma once

#include "ParticleModule.h"
#include "Math/MathCore.h"

struct Particle;

/// @brief パーティクルのノイズモジュール（Unity Noise Module相当）
/// パーリンノイズによる位置の揺らぎを追加します

namespace CoreEngine
{
class NoiseModule : public ParticleModule {
public:
    /// @brief ノイズデータ構造体
    struct NoiseData {
        float strength = 1.0f;     // ノイズの強度
        float frequency = 1.0f;      // ノイズの周波数
        float scrollSpeed = 0.0f;       // ノイズの移動速度
        bool damping = true;   // ライフタイムに応じて減衰するか
        Vector3 positionAmount = { 1.0f, 1.0f, 1.0f }; // 各軸のノイズ影響量
    };

    NoiseModule();
    ~NoiseModule() = default;

    /// @brief ノイズデータを設定
    /// @param data ノイズデータ
    void SetNoiseData(const NoiseData& data) { noiseData_ = data; }

    /// @brief ノイズデータを取得
    /// @return ノイズデータの参照
    const NoiseData& GetNoiseData() const { return noiseData_; }

    /// @brief ノイズデータを取得（書き換え可能）
    /// @return ノイズデータの参照
    NoiseData& GetNoiseData() { return noiseData_; }
    
    /// @brief パーティクルにノイズを適用
    /// @param particle 対象のパーティクル
    /// @param deltaTime フレーム時間
    void ApplyNoise(Particle& particle, float deltaTime);

#ifdef _DEBUG
    /// @brief ImGuiデバッグ表示
    /// @return UIに変更があった場合true
    bool ShowImGui() override;
#endif

private:
    NoiseData noiseData_;

    /// @brief パーリンノイズ関数
    /// @param x X座標
    /// @param y Y座標
    /// @param z Z座標
    /// @return ノイズ値 (-1.0 ～ 1.0)
    float PerlinNoise3D(float x, float y, float z);

    /// @brief フェード関数（スムーズステップ）
    /// @param t 入力値 (0.0 - 1.0)
    /// @return フェード値
    float Fade(float t);

    /// @brief 線形補間
    /// @param a 開始値
    /// @param b 終了値
    /// @param t 補間係数
    /// @return 補間値
    float Lerp(float a, float b, float t);

    /// @brief グラディエント関数
    /// @param hash ハッシュ値
    /// @param x X座標
    /// @param y Y座標
    /// @param z Z座標
    /// @return グラディエント値
    float Gradient(int hash, float x, float y, float z);
};
}
