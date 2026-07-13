#include "pch.h"
#include "MaterialInstance.h"

namespace CoreEngine
{
    using namespace CoreEngine::MathCore;

    void MaterialInstance::Initialize(ID3D12Device* device)
    {
        InitializeBuffer(device);

        // PBR デフォルト値: 白・ライティング有効・非金属・中程度の粗さ
        // ファクターはテクスチャと乗算されるため、テクスチャ有りマテリアルでは
        // ModelLoader が読み込んだアセット側ファクターで上書きされる。
        materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
        materialData_->uvTransform = Matrix::Identity();
        materialData_->metallic = 0.0f;
        materialData_->roughness = 0.5f;
        materialData_->occlusionStrength = 1.0f;
        materialData_->useNormalMap = 0;
        materialData_->emissiveFactor = { 0.0f, 0.0f, 0.0f };
        materialData_->enableLighting = 1;
        materialData_->enableDithering = 1;
        materialData_->ditheringScale = 1.0f;
        materialData_->alphaCutoff = 0.5f;
        materialData_->shadingMode = static_cast<int32_t>(ShadingMode::PBR);
        materialData_->iblIntensity = 1.0f;
    }

} // namespace CoreEngine
