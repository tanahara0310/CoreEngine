#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/PlaneMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector2.h"

/// @brief 水面表現用のグリッドメッシュオブジェクト
/// PlaneMeshGenerator を使用して N×N 分割の平面メッシュを生成する。
/// resolution（分割数）が高いほど後のステップで波の表現が細かくなる。
class WaterPlaneObject : public CoreEngine::PrimitiveGameObject
                       , public CoreEngine::ICustomShaderProvider {
public:
    /// @param size 水面の一辺のサイズ（XZ 方向共通）
    /// @param resolution XZ 方向の分割数
    /// @param albedoTextureName アルベドテクスチャのファイル名（空文字列の場合は単色）
    WaterPlaneObject(float size = 50.0f, uint32_t resolution = 64,
        const std::string& albedoTextureName = {});

    const char* GetObjectName() const override { return "WaterPlane"; }

    // ===== ICustomShaderProvider =====
    std::wstring GetVertexShaderPath() const override { return L"Water.VS.hlsl"; }
    std::wstring GetPixelShaderPath()  const override { return L"Water.PS.hlsl"; }

    /// @brief ノーマルマップテクスチャのファイル名を設定する（Initialize 後に呼ぶこと）
    void SetNormalMapTextureName(const std::string& fileName);

    /// @brief UV スクロール速度を設定する（単位: UV/秒）
    void SetScrollSpeed(const CoreEngine::Vector2& speed);

    /// @brief UV タイリング（繰り返し回数）を設定する
    void SetUVTiling(const CoreEngine::Vector2& tiling);

    /// @brief UV スクロールを毎フレーム更新する
    /// @param deltaTime 前フレームからの経過時間（秒）
    void UpdateUVScroll(float deltaTime);

protected:
    std::string GetTexturePath() const override { return albedoTextureName_; }

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

    /// @brief Initialize 完了後に独自シェーダー PSO を登録する
    void OnInitialize() override;

private:
    /// @brief UV タイリングとオフセットをマテリアルの uvTransform 行列に反映する
    void ApplyUVTransform();

    float    size_;
    uint32_t resolution_;

    std::string albedoTextureName_;   ///< アルベドテクスチャのファイル名

    CoreEngine::Vector2 scrollSpeed_; ///< UV スクロール速度（U方向, V方向）
    CoreEngine::Vector2 uvTiling_;    ///< UV タイリング回数
    CoreEngine::Vector2 uvOffset_;    ///< 現在の UV オフセット（内部状態）
};
