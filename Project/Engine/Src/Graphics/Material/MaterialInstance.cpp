#include "MaterialInstance.h"

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    void MaterialInstance::Initialize(ID3D12Device* device)
    {
        InitializeBuffer(device);

        // PBR デフォルト値: 白・ライティング有効・非金属・中程度の粗さ
        materialData_->color              = { 1.0f, 1.0f, 1.0f, 1.0f };
        materialData_->enableLighting     = 1;
        materialData_->uvTransform        = Matrix::Identity();
        materialData_->metallic           = 0.0f;
        materialData_->roughness          = 0.5f;
        materialData_->ao                 = 1.0f;
        materialData_->useNormalMap       = 0;
        materialData_->useMetallicMap     = 0;
        materialData_->useRoughnessMap    = 0;
        materialData_->useAOMap           = 0;
        materialData_->enableDithering    = 1;
        materialData_->ditheringScale     = 1.0f;
        materialData_->enableIBL          = 0;
        materialData_->iblIntensity       = 1.0f;
        materialData_->padding2           = 0.0f;
    }

} // namespace CoreEngine
