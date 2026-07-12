#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"

/// @brief 立方体プリミティブオブジェクト
class CubeObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @param size 各軸のサイズ（幅・高さ・奥行きが均等）
    explicit CubeObject(float size = 1.0f);

    /// @param width  X軸方向のサイズ
    /// @param height Y軸方向のサイズ
    /// @param depth  Z軸方向のサイズ
    CubeObject(float width, float height, float depth);

    const char* GetObjectName() const override;

protected:
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override;

private:
    float width_;
    float height_;
    float depth_;
};
