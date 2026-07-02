#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/CylinderMeshGenerator.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

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

protected:
    std::wstring GetVertexShaderPath() const override { return L"Object3d.VS.hlsl"; }
    std::wstring GetPixelShaderPath() const override { return L"Object3d.PS.hlsl"; }
    D3D12_CULL_MODE GetCullMode() const override { return D3D12_CULL_MODE_NONE; }

    void OnInitialize() override {
        SetCustomShaderProvider(this);
        // Deferred の不透明スキップ経路を避け、カスタムPSO（Cull None）で描画する
        SetBlendMode(CoreEngine::BlendMode::kBlendModeNormal);
    }

    std::string GetTexturePath() const override { return texturePath_; }

    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override {
        return std::make_unique<CoreEngine::CylinderMeshGenerator>(
            topRadius_, bottomRadius_, height_, divisions_);
    }

private:
    float topRadius_;
    float bottomRadius_;
    float height_;
    uint32_t divisions_;
    std::string texturePath_;
};
