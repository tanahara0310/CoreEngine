#include "MaterialInstance.h"

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    void MaterialInstance::Initialize(ID3D12Device* device, ResourceFactory* resourceFactory)
    {
        materialResource_ = resourceFactory->CreateBufferResource(device, sizeof(MaterialConstants));
        materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

        // 初期値の設定 (白・ライティング有効・単位行列)
        materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
        materialData_->enableLighting = 1;
        materialData_->uvTransform = Matrix::Identity();
        materialData_->shininess = 64.0f;
        materialData_->shadingMode = static_cast<int32_t>(ShadingMode::HalfLambert);
        materialData_->toonThreshold = 0.5f;
        materialData_->toonSmoothness = 0.1f;
        materialData_->enableDithering = 1;
        materialData_->ditheringScale = 1.0f;
        materialData_->enableEnvironmentMap = 0;
        materialData_->environmentMapIntensity = 0.3f;
        materialData_->enableIBL = 0;
        materialData_->iblIntensity = 1.0f;
        materialData_->environmentRotationY = 0.0f;
        materialData_->useNormalMap = 0;
        materialData_->useMetallicMap = 0;
        materialData_->useRoughnessMap = 0;
        materialData_->useAOMap = 0;
    }

} // namespace CoreEngine
