#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/CylinderMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Material/MaterialInstance.h"
#include "EngineSystem/EngineSystem.h"
#include "Utility/FrameRate/FrameRateController.h"
#include "Math/Vector/Vector2.h"
#include "Math/Matrix/Matrix4x4.h"
#include "Math/MathCore.h"

#include <cmath>

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
        std::string texturePath = "")
        : topRadius_(topRadius)
        , bottomRadius_(bottomRadius)
        , height_(height)
        , divisions_(divisions)
        , texturePath_(std::move(texturePath)) {
    }

    const char* GetObjectName() const override { return "Cylinder"; }

    /// @brief discard 判定に使用するアルファしきい値を設定する
    void SetDiscardThreshold(float threshold) {
        discardThreshold_ = threshold;
        if (auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr) {
            mat->SetAlphaCutoff(discardThreshold_);
        }
    }
    float GetDiscardThreshold() const { return discardThreshold_; }

    /// @brief UV スクロール速度を設定する（単位: UV/秒）
    void SetUVScrollSpeed(const CoreEngine::Vector2& speed) { uvScrollSpeed_ = speed; }
    const CoreEngine::Vector2& GetUVScrollSpeed() const { return uvScrollSpeed_; }

protected:
    std::wstring GetVertexShaderPath() const override { return L"Object3d.VS.hlsl"; }
    std::wstring GetPixelShaderPath() const override { return L"Object3d.PS.hlsl"; }
    D3D12_CULL_MODE GetCullMode() const override { return D3D12_CULL_MODE_NONE; }

    /// @brief エフェクト表現用にシリンダーは深度書き込みを行わない（DepthWriteMask::ZERO 相当）
    bool GetDepthWriteEnable() const override { return false; }

    void OnInitialize() override {
        SetCustomShaderProvider(this);
        // Deferred の不透明スキップ経路を避け、カスタムPSO（Cull None）で描画する
        SetBlendMode(CoreEngine::BlendMode::kBlendModeNormal);

        if (auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr) {
            mat->SetAlphaCutoff(discardThreshold_);
        }
        ApplyUVTransform();
    }

    void OnUpdate() override {
        auto* fc = GetEngineSystem() ? GetEngineSystem()->GetComponent<CoreEngine::FrameRateController>() : nullptr;
        const float deltaTime = fc ? fc->GetDeltaTime() : 0.0f;

        uvOffset_.x = std::fmod(uvOffset_.x + uvScrollSpeed_.x * deltaTime, 1.0f);
        uvOffset_.y = std::fmod(uvOffset_.y + uvScrollSpeed_.y * deltaTime, 1.0f);

        ApplyUVTransform();
    }

    std::string GetTexturePath() const override { return texturePath_; }

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override {
        return std::make_unique<CoreEngine::CylinderMeshGenerator>(
            topRadius_, bottomRadius_, height_, divisions_);
    }

private:
    /// @brief UV オフセットをマテリアルの uvTransform 行列に反映する
    void ApplyUVTransform() {
        auto* mat = GetModel() ? GetModel()->GetMaterial() : nullptr;
        if (!mat) {
            return;
        }
        CoreEngine::Matrix4x4 uvMat = CoreEngine::MathCore::Matrix::Identity();
        uvMat.m[3][0] = uvOffset_.x;
        uvMat.m[3][1] = uvOffset_.y;
        mat->SetUVTransform(uvMat);
    }

    float topRadius_;
    float bottomRadius_;
    float height_;
    uint32_t divisions_;
    std::string texturePath_;

    float discardThreshold_ = 0.5f; ///< discard 判定に使用するアルファしきい値

    CoreEngine::Vector2 uvScrollSpeed_ = { 0.1f, 0.0f }; ///< UV スクロール速度（U方向, V方向）
    CoreEngine::Vector2 uvOffset_ = { 0.0f, 0.0f };      ///< 現在の UV オフセット（内部状態）
};
