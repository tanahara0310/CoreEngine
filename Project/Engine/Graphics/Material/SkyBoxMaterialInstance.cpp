#include "SkyBoxMaterialInstance.h"

namespace CoreEngine
{
    void SkyBoxMaterialInstance::Initialize(ID3D12Device* device)
    {
        InitializeBuffer(device);
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

} // namespace CoreEngine
