#pragma once

#include "ObjectCommon/Primitive/PrimitiveGameObject.h"
#include "Graphics/Primitive/CubeMeshGenerator.h"

/// @brief 立方体プリミティブオブジェクト
class CubeObject : public CoreEngine::PrimitiveGameObject {
public:
    /// @param size 各軸のサイズ（幅・高さ・奥行きが均等）
    explicit CubeObject(float size = 1.0f)
        : width_(size), height_(size), depth_(size) {}

    /// @param width  X軸方向のサイズ
    /// @param height Y軸方向のサイズ
    /// @param depth  Z軸方向のサイズ
    CubeObject(float width, float height, float depth)
        : width_(width), height_(height), depth_(depth) {}

    const char* GetObjectName() const override { return "Cube"; }

protected:
    std::unique_ptr<CoreEngine::IPrimitiveMeshGenerator> CreateMeshGenerator() const override {
        return std::make_unique<CoreEngine::CubeMeshGenerator>(width_, height_, depth_);
    }

private:
    float width_;
    float height_;
    float depth_;
};
