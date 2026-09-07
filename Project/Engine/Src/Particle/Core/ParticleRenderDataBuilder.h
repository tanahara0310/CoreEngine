#pragma once

#include "Math/MathCore.h"
#include "ParticleResourceManager.h"  // ParticleForGPU定義のため
#include <vector>
#include <cstdint>

namespace CoreEngine
{
// 前方宣言
class Camera;
struct Particle;
struct ParticleForGPU;

/// @brief ビルボードタイプ
enum class BillboardType {
    None,           // ビルボード無効
    ViewFacing,     // カメラに向く
    YAxisOnly,      // Y軸のみ固定
    ScreenAligned   // スクリーン平行
};

/// @brief パーティクル描画モード
enum class ParticleRenderMode {
    Billboard,      // ビルボードテクスチャ（従来）
    Model           // 3Dモデル
};

/// @brief パーティクルの描画データビルダー
/// GPU送信データの準備とビルボード計算を担当
class ParticleRenderDataBuilder {
public:
    ParticleRenderDataBuilder() = default;
    ~ParticleRenderDataBuilder() = default;

    /// @brief 描画データを準備
    /// @param instancingData GPU 送信データ（出力）
    /// @return 準備したインスタンス数
    uint32_t BuildRenderData(
        const std::vector<Particle>& particles,
        const Camera* camera,
        BillboardType billboardType,
        ParticleRenderMode renderMode,
        ParticleForGPU* instancingData,
        uint32_t maxInstances
    );

    /// @brief ビルボードなし（モデルパーティクル）のワールド行列を作る
    /// @param particle パーティクル
    /// @return ワールド行列
    /// @note TLAS インスタンス構築（RayTracingSubsystem）からも呼ぶため公開している。
    ///       影と見た目がずれないよう、行列の式はここ 1 箇所に保つこと。
    static Matrix4x4 MakeModelParticleWorldMatrix(const Particle& particle);

private:
    /// @brief ビルボード行列を作成
    /// @param viewMatrix ビュー行列
    /// @param type ビルボードタイプ
    /// @return ビルボード行列
    Matrix4x4 CreateBillboardMatrix(const Matrix4x4& viewMatrix, BillboardType type);

    /// @brief ワールド行列を計算
    /// @param particle パーティクル
    /// @param billboardType ビルボードタイプ
    /// @param billboardMatrix ビルボード行列
    /// @return ワールド行列
    Matrix4x4 CalculateWorldMatrix(
        const Particle& particle,
        BillboardType billboardType,
        const Matrix4x4& billboardMatrix
    );
};

} // namespace CoreEngine
