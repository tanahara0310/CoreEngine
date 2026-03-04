#include "SpriteMaterialInstance.h"

namespace CoreEngine
{
    void SpriteMaterialInstance::Initialize(ID3D12Device* device)
    {
        InitializeBuffer(device);
        materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
        materialData_->uvTransform = MathCore::Matrix::Identity();
    }

    void SpriteMaterialInstance::SetColor(const Vector4& color)
    {
        materialData_->color = color;
    }

    Vector4 SpriteMaterialInstance::GetColor() const
    {
        return materialData_->color;
    }

    void SpriteMaterialInstance::SetUVTransform(const Matrix4x4& uvTransform)
    {
        materialData_->uvTransform = uvTransform;
    }

    Matrix4x4 SpriteMaterialInstance::GetUVTransform() const
    {
        return materialData_->uvTransform;
    }

} // namespace CoreEngine
