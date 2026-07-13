#include "pch.h"
#include "MaterialInstance.h"
#include "Utility/JsonManager/JsonManager.h"

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
        materialData_->iblIntensity = 1.0f;
    }

    nlohmann::json MaterialInstance::ToJson() const
    {
        nlohmann::json m;
        m["color"] = JsonManager::Vector4ToJson(GetColor());
        m["lighting"] = IsLightingEnabled();
        m["metallic"] = GetMetallic();
        m["roughness"] = GetRoughness();
        m["occlusionStrength"] = GetOcclusionStrength();
        m["emissive"] = JsonManager::Vector3ToJson(GetEmissiveFactor());
        m["normalMap"] = IsNormalMapEnabled();
        m["dithering"] = IsDitheringEnabled();
        m["ditheringScale"] = GetDitheringScale();
        m["alphaCutoff"] = GetAlphaCutoff();
        m["iblIntensity"] = GetIBLIntensity();
        return m;
    }

    void MaterialInstance::FromJson(const nlohmann::json& m)
    {
        if (m.contains("color")) {
            SetColor(JsonManager::JsonToVector4(m["color"]));
        }
        SetLightingEnabled(JsonManager::SafeGet<bool>(m, "lighting", IsLightingEnabled()));
        SetMetallic(JsonManager::SafeGet<float>(m, "metallic", GetMetallic()));
        SetRoughness(JsonManager::SafeGet<float>(m, "roughness", GetRoughness()));

        // 旧フォーマットの "ao"（定数AO）は occlusionStrength として読み替える
        SetOcclusionStrength(JsonManager::SafeGet<float>(m, "occlusionStrength",
            JsonManager::SafeGet<float>(m, "ao", GetOcclusionStrength())));

        if (m.contains("emissive")) {
            SetEmissiveFactor(JsonManager::JsonToVector3(m["emissive"]));
        }
        SetNormalMapEnabled(JsonManager::SafeGet<bool>(m, "normalMap", IsNormalMapEnabled()));
        SetDitheringEnabled(JsonManager::SafeGet<bool>(m, "dithering", IsDitheringEnabled()));
        SetDitheringScale(JsonManager::SafeGet<float>(m, "ditheringScale", GetDitheringScale()));
        SetAlphaCutoff(JsonManager::SafeGet<float>(m, "alphaCutoff", GetAlphaCutoff()));
        SetIBLIntensity(JsonManager::SafeGet<float>(m, "iblIntensity", GetIBLIntensity()));

        // 旧フォーマットの "ibl"（bool）: false の場合のみ intensity=0 として読み替える
        if (m.contains("ibl") && !JsonManager::SafeGet<bool>(m, "ibl", true)) {
            SetIBLIntensity(0.0f);
        }
    }

} // namespace CoreEngine
