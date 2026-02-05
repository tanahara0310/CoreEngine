#pragma once

#include "MathCore.h"
#include "ParticleResourceManager.h"  // ParticleForGPU定義のため
#include <list>
#include <cstdint>

namespace CoreEngine
{
// 前方宣言
class ICamera;
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
    /// @param particles パーティクルリスト
    /// @param camera カメラ
    /// @param billboardType ビルボードタイプ
    /// @param renderMode 描画モード
    /// @param instancingData GPU送信データ（出力）
    /// @param maxInstances 最大インスタンス数
    /// @return 準備したインスタンス数
    uint32_t BuildRenderData(
        const std::list<Particle>& particles,
        const ICamera* camera,
        BillboardType billboardType,
        ParticleRenderMode renderMode,
        ParticleForGPU* instancingData,
        uint32_t maxInstances
    );

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
