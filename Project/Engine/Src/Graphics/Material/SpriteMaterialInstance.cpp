#include "pch.h"
#include "SpriteMaterialInstance.h"
#include "Graphics/RHI/GraphicsCore.h"

namespace CoreEngine
{
    void SpriteMaterialInstance::Initialize(GraphicsCore& graphics)
    {
        InitializeBuffer(graphics.GetConstantBufferPool());
        materialData_.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        materialData_.uvTransform = MathCore::Matrix::Identity();
        materialData_.filterMode = 0;
        materialData_.padding = { 0.0f, 0.0f, 0.0f };
        Upload();
    }

    void SpriteMaterialInstance::SetColor(const Vector4& color)
    {
        materialData_.color = color;
        Upload();
    }

    Vector4 SpriteMaterialInstance::GetColor() const
    {
        return materialData_.color;
    }

    void SpriteMaterialInstance::SetUVTransform(const Matrix4x4& uvTransform)
    {
        materialData_.uvTransform = uvTransform;
        Upload();
    }

    Matrix4x4 SpriteMaterialInstance::GetUVTransform() const
    {
        return materialData_.uvTransform;
    }

    void SpriteMaterialInstance::SetFilterMode(uint32_t filterMode)
    {
        materialData_.filterMode = filterMode;
        Upload();
    }

} // namespace CoreEngine
