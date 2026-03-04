#include "SpriteMaterialInstance.h"

namespace CoreEngine
{
    void SpriteMaterialInstance::Initialize(ID3D12Device* device)
    {
        materialResource_ = ResourceFactory::CreateBufferResource(device, sizeof(GpuData));
        materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
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

    D3D12_GPU_VIRTUAL_ADDRESS SpriteMaterialInstance::GetGPUVirtualAddress() const
    {
        return materialResource_->GetGPUVirtualAddress();
    }

} // namespace CoreEngine
