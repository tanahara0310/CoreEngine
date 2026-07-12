#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Math/Vector/Vector2.h"

/// @brief シリンダープリミティブオブジェクト
class CylinderObject : public CoreEngine::PrimitiveGameObject
    , public CoreEngine::ICustomShaderProvider {
public:
    /// @param topRadius 上面半径
    /// @param bottomRadius 下面半径
    /// @param height 高さ
    /// @param divisions 円周分割数
    CylinderObject(float topRadius = 0.5f, float bottomRadius = 0.5f,
        float height = 1.0f, uint32_t divisions = 32,
        std::string texturePath = "");

    const char* GetObjectName() const override;

    /// @brief discard 判定に使用するアルファしきい値を設定する
    void SetDiscardThreshold(float threshold);
    float GetDiscardThreshold() const;

    /// @brief UV スクロール速度を設定する（単位: UV/秒）
    void SetUVScrollSpeed(const CoreEngine::Vector2& speed);
    const CoreEngine::Vector2& GetUVScrollSpeed() const;

protected:
    std::wstring GetVertexShaderPath() const override;
    std::wstring GetPixelShaderPath() const override;
    D3D12_CULL_MODE GetCullMode() const override;

    /// @brief エフェクト表現用にシリンダーは深度書き込みを行わない（DepthWriteMask::ZERO 相当）
    bool GetDepthWriteEnable() const override;

    void OnInitialize() override;
    void OnUpdate() override;

    std::string GetTexturePath() const override;

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

private:
    /// @brief UV オフセットをマテリアルの uvTransform 行列に反映する
    void ApplyUVTransform();

    float topRadius_;
    float bottomRadius_;
    float height_;
    uint32_t divisions_;
    std::string texturePath_;

    float discardThreshold_ = 0.5f; ///< discard 判定に使用するアルファしきい値

    CoreEngine::Vector2 uvScrollSpeed_ = { 0.1f, 0.0f }; ///< UV スクロール速度（U方向, V方向）
    CoreEngine::Vector2 uvOffset_ = { 0.0f, 0.0f };      ///< 現在の UV オフセット（内部状態）
};
