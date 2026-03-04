#include "SkyBoxMaterialInstance.h"

namespace CoreEngine
{
    void SkyBoxMaterialInstance::Initialize(ID3D12Device* device)
    {
        materialResource_ = ResourceFactory::CreateBufferResource(device, sizeof(GpuData));
        materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
        materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    }

    void SkyBoxMaterialInstance::SetColor(const Vector4& color)
    {
        materialData_->color = color;
    }

    Vector4 SkyBoxMaterialInstance::GetColor() const
    {
        return materialData_->color;
    }

    D3D12_GPU_VIRTUAL_ADDRESS SkyBoxMaterialInstance::GetGPUVirtualAddress() const
    {
        return materialResource_->GetGPUVirtualAddress();
    }

} // namespace CoreEngine
