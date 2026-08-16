#include "pch.h"
#include "LightManager.h"

#include "Math/MathCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace CoreEngine
{
    namespace
    {
        constexpr float kDegToRad = MathCore::Constants::kDegToRad;

        const char* GetLightTypeName(LightType type)
        {
            switch (type) {
            case LightType::Directional: return "Directional";
            case LightType::Point:       return "Point";
            case LightType::Spot:        return "Spot";
            case LightType::Area:        return "Area";
            }
            return "Unknown";
        }
    }

    void LightManager::Initialize(ID3D12Device* device, ResourceFactory* resourceFactory, DescriptorManager* descriptorManager)
    {
        bufferManager_.Initialize(
            device,
            descriptorManager,
            resourceFactory,
            MAX_DIRECTIONAL_LIGHTS,
            MAX_POINT_LIGHTS,
            MAX_SPOT_LIGHTS,
            MAX_AREA_LIGHTS
        );

        // 総最大数ぶんを先に確保し、スロットは再確保しない（Light* がスロット生存中は安定）
        slots_.reserve(MAX_TOTAL_LIGHTS);
    }

    // ==================== 生成・破棄・参照 ====================

    void LightManager::SetupDefaults(Light& light, LightType type)
    {
        light.type = type;
        light.enabled = true;
        light.color = { 1.0f, 1.0f, 1.0f };

        // 生成位置は床（y=0）より上に置く。床面上（y=0）ぴったりに生成すると
        // 床への入射がかすめ角（NdotL≈0）になり、逆二乗減衰も相まって
        // 「ライトを追加したのに何も照らされない」ように見える
        switch (type) {
        case LightType::Directional:
            // 旧既定（シェーダー単位 intensity=1.0）と同輝度になる照度（≈ 57,143 lx。薄曇り相当）
            light.intensity = 1.0f / LightUnits::kShaderUnitsPerLux;
            light.direction = { 0.0f, -1.0f, 0.0f };
            light.position = { 0.0f, 5.0f, 0.0f };  // 平行光のためギズモ表示位置のみに使用
            break;
        case LightType::Point:
            // 100,000 cd: 1m で 100,000 lx。快晴の太陽下でも存在が分かる光度
            light.intensity = 100000.0f;
            light.position = { 0.0f, 2.0f, 0.0f };
            light.range = 10.0f;
            break;
        case LightType::Spot:
            light.intensity = 100000.0f;
            light.position = { 0.0f, 3.0f, 0.0f };
            light.direction = { 0.0f, -1.0f, 0.0f };
            light.range = 10.0f;
            light.innerConeAngleDeg = 30.0f;
            light.outerConeAngleDeg = 45.0f;
            break;
        case LightType::Area:
            // 25,000 nt × 2m×2m ≈ 旧既定（シェーダー単位 1.75）相当の発光面
            light.intensity = 25000.0f;
            light.position = { 0.0f, 2.5f, 0.0f };
            light.direction = { 0.0f, -1.0f, 0.0f };
            light.range = 10.0f;
            light.areaWidth = 2.0f;
            light.areaHeight = 2.0f;
            break;
        }
    }

    LightHandle LightManager::CreateLight(LightType type, std::string name)
    {
        if (GetLightCount(type) >= GetMaxLightCount(type)) {
            return {};
        }

        // 空きスロットを再利用し、無ければ末尾へ追加（reserve 済みのため再確保は起きない）
        size_t slotIndex = slots_.size();
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].alive) {
                slotIndex = i;
                break;
            }
        }
        if (slotIndex == slots_.size()) {
            if (slots_.size() >= MAX_TOTAL_LIGHTS) {
                return {};
            }
            slots_.emplace_back();
        }

        Slot& slot = slots_[slotIndex];
        slot.alive = true;
        slot.light = Light{};
        SetupDefaults(slot.light, type);

        if (name.empty()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%s %u", GetLightTypeName(type), GetLightCount(type));
            slot.light.name = buf;
        } else {
            slot.light.name = std::move(name);
        }

        return { static_cast<uint16_t>(slotIndex), slot.generation };
    }

    bool LightManager::DestroyLight(LightHandle handle)
    {
        Light* light = GetLight(handle);
        if (!light) {
            return false;
        }
        Slot& slot = slots_[handle.index];
        slot.alive = false;
        slot.generation++;  // 世代を進めて既存ハンドル・ポインタを無効化する
        slot.light = Light{};
        return true;
    }

    Light* LightManager::GetLight(LightHandle handle)
    {
        return const_cast<Light*>(static_cast<const LightManager*>(this)->GetLight(handle));
    }

    const Light* LightManager::GetLight(LightHandle handle) const
    {
        if (!handle.IsValid() || handle.index >= slots_.size()) {
            return nullptr;
        }
        const Slot& slot = slots_[handle.index];
        if (!slot.alive || slot.generation != handle.generation) {
            return nullptr;
        }
        return &slot.light;
    }

    LightHandle LightManager::FindLightByName(std::string_view name) const
    {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive && slots_[i].light.name == name) {
                return { static_cast<uint16_t>(i), slots_[i].generation };
            }
        }
        return {};
    }

    void LightManager::ForEachLight(const std::function<void(LightHandle, Light&)>& fn)
    {
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive) {
                fn({ static_cast<uint16_t>(i), slots_[i].generation }, slots_[i].light);
            }
        }
    }

    uint32_t LightManager::GetLightCount(LightType type) const
    {
        uint32_t count = 0;
        for (const Slot& slot : slots_) {
            if (slot.alive && slot.light.type == type) {
                ++count;
            }
        }
        return count;
    }

    uint32_t LightManager::GetMaxLightCount(LightType type)
    {
        switch (type) {
        case LightType::Directional: return MAX_DIRECTIONAL_LIGHTS;
        case LightType::Point:       return MAX_POINT_LIGHTS;
        case LightType::Spot:        return MAX_SPOT_LIGHTS;
        case LightType::Area:        return MAX_AREA_LIGHTS;
        }
        return 0;
    }

    // ==================== ディレクショナルの正準順 ====================

    std::vector<uint16_t> LightManager::CollectDirectionalSlotsCanonical() const
    {
        std::vector<uint16_t> indices;
        indices.reserve(MAX_DIRECTIONAL_LIGHTS);
        for (size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].alive && slots_[i].light.type == LightType::Directional) {
                indices.push_back(static_cast<uint16_t>(i));
            }
        }

        // 大気の太陽（無ければ先頭のまま）をメインとして先頭へ移動する。
        // シャドウマップ・シェーダーの gDirectionalLights[0]・RT シャドウの
        // インデックス 0 が常にメインライトを指すことを保証する
        auto sunIt = std::find_if(indices.begin(), indices.end(), [this](uint16_t idx) {
            return slots_[idx].light.isAtmosphereSun;
        });
        if (sunIt != indices.end() && sunIt != indices.begin()) {
            std::rotate(indices.begin(), sunIt, sunIt + 1);
        }
        return indices;
    }

    Light* LightManager::GetDirectionalLight(size_t index)
    {
        const std::vector<uint16_t> indices = CollectDirectionalSlotsCanonical();
        if (index >= indices.size()) {
            return nullptr;
        }
        return &slots_[indices[index]].light;
    }

    const Light* LightManager::FindAtmosphereSunLight() const
    {
        const Light* firstDirectional = nullptr;
        for (const Slot& slot : slots_) {
            if (!slot.alive || slot.light.type != LightType::Directional) {
                continue;
            }
            if (slot.light.isAtmosphereSun) {
                return &slot.light;
            }
            if (!firstDirectional) {
                firstDirectional = &slot.light;
            }
        }
        // フラグ付きライトが無い場合は最初のディレクショナルライトへフォールバック
        return firstDirectional;
    }

    Light* LightManager::GetAtmosphereSunLight()
    {
        return const_cast<Light*>(FindAtmosphereSunLight());
    }

    const Light* LightManager::FindAtmosphereMoonLight() const
    {
        for (const Slot& slot : slots_) {
            if (!slot.alive || slot.light.type != LightType::Directional) {
                continue;
            }
            // 同一ライトに両フラグが立っている場合は太陽として扱う（月はオプトイン・フォールバック無し）
            if (slot.light.isAtmosphereMoon && !slot.light.isAtmosphereSun) {
                return &slot.light;
            }
        }
        return nullptr;
    }

    Light* LightManager::GetAtmosphereMoonLight()
    {
        return const_cast<Light*>(FindAtmosphereMoonLight());
    }

    Vector3 LightManager::GetEffectiveLightColorRGB(const Light& light) const
    {
        Vector3 color = light.color;
        if (&light == FindAtmosphereSunLight())
        {
            color.x *= atmosphereSunTransmittance_.x;
            color.y *= atmosphereSunTransmittance_.y;
            color.z *= atmosphereSunTransmittance_.z;
        }
        else if (&light == FindAtmosphereMoonLight())
        {
            color.x *= atmosphereMoonTransmittance_.x;
            color.y *= atmosphereMoonTransmittance_.y;
            color.z *= atmosphereMoonTransmittance_.z;
        }
        return color;
    }

    // ==================== GPU 転送 ====================

    void LightManager::ComputeAreaLightBasis(const Vector3& normal, Vector3& outRight, Vector3& outUp)
    {
        Vector3 n = CoreEngine::Normalize(normal);
        Vector3 ref = { 0.0f, 1.0f, 0.0f };
        if (std::abs(CoreEngine::Dot(n, ref)) > 0.99f) {
            ref = { 1.0f, 0.0f, 0.0f };
        }
        outRight = CoreEngine::Normalize(CoreEngine::Cross(ref, n));
        outUp = CoreEngine::Cross(n, outRight);
    }

    void LightManager::UpdateAll()
    {
        // ---- ディレクショナル（正準順。太陽・月には大気透過率を掛けた実効色を転送する）----
        std::vector<DirectionalLightData> gpuDirectionals;
        for (uint16_t slotIndex : CollectDirectionalSlotsCanonical()) {
            const Light& light = slots_[slotIndex].light;
            const Vector3 color = GetEffectiveLightColorRGB(light);

            DirectionalLightData gpu{};
            gpu.color = { color.x, color.y, color.z, 1.0f };
            gpu.direction = CoreEngine::Normalize(light.direction);
            gpu.intensity = LightUnits::LuxToShader(light.intensity);
            gpu.enabled = light.enabled ? 1u : 0u;
            gpuDirectionals.push_back(gpu);
        }

        // ---- ポイント / スポット / エリア（生成順）----
        std::vector<PointLightData> gpuPoints;
        std::vector<SpotLightData> gpuSpots;
        std::vector<AreaLightData> gpuAreas;
        for (const Slot& slot : slots_) {
            if (!slot.alive) {
                continue;
            }
            const Light& light = slot.light;

            switch (light.type) {
            case LightType::Point: {
                PointLightData gpu{};
                gpu.color = { light.color.x, light.color.y, light.color.z, 1.0f };
                gpu.position = light.position;
                gpu.intensity = LightUnits::CandelaToShader(light.intensity);
                gpu.radius = light.range;
                gpu.decay = 1.0f;
                gpu.enabled = light.enabled ? 1u : 0u;
                gpuPoints.push_back(gpu);
                break;
            }
            case LightType::Spot: {
                // 内角 > 外角 の不正入力はここで正す（オーサリング値は角度 [deg] のまま保持）
                const float outerDeg = std::max(light.outerConeAngleDeg, 0.1f);
                const float innerDeg = std::clamp(light.innerConeAngleDeg, 0.0f, outerDeg);

                SpotLightData gpu{};
                gpu.color = { light.color.x, light.color.y, light.color.z, 1.0f };
                gpu.position = light.position;
                gpu.intensity = LightUnits::CandelaToShader(light.intensity);
                gpu.direction = CoreEngine::Normalize(light.direction);
                gpu.distance = light.range;
                gpu.decay = 1.0f;
                gpu.cosAngle = std::cos(outerDeg * kDegToRad);
                gpu.cosFalloffStart = std::cos(innerDeg * kDegToRad);
                gpu.enabled = light.enabled ? 1u : 0u;
                gpuSpots.push_back(gpu);
                break;
            }
            case LightType::Area: {
                AreaLightData gpu{};
                gpu.color = { light.color.x, light.color.y, light.color.z, 1.0f };
                gpu.position = light.position;
                gpu.intensity = LightUnits::NitToShader(
                    light.intensity, light.areaWidth * light.areaHeight);
                gpu.normal = CoreEngine::Normalize(light.direction);
                gpu.width = light.areaWidth;
                gpu.height = light.areaHeight;
                ComputeAreaLightBasis(light.direction, gpu.right, gpu.up);
                gpu.range = light.range;
                gpu.enabled = light.enabled ? 1u : 0u;
                gpuAreas.push_back(gpu);
                break;
            }
            case LightType::Directional:
            default:
                break;
            }
        }

        bufferManager_.UpdateBuffers(gpuDirectionals, gpuPoints, gpuSpots, gpuAreas);

#ifdef _DEBUG
        if (debugVisualizer_.IsVisualizationEnabled()) {
            // 選択中のライトのみ詳細ギズモ、他は簡略マーカーで描く（画面の線ノイズ抑制）
            const LightHandle selected = debugVisualizer_.GetSelectedLight();
            for (size_t i = 0; i < slots_.size(); ++i) {
                const Slot& slot = slots_[i];
                if (!slot.alive) continue;
                const LightHandle handle{ static_cast<uint16_t>(i), slot.generation };
                debugVisualizer_.DrawVisualization(slot.light, handle == selected);
            }
        }
#endif
    }

    void LightManager::DrawAllImGui()
    {
        debugVisualizer_.DrawImGui(*this);
    }

    bool LightManager::DrawLightTreeImGui()
    {
        return debugVisualizer_.DrawHierarchyChildren(*this);
    }

    void LightManager::ClearLightUISelection()
    {
        debugVisualizer_.ClearSelection();
    }

    // ==================== GPU バインディング ====================

    void LightManager::SetLightsToCommandList(
        ID3D12GraphicsCommandList* commandList,
        int lightCountsRootParameterIndex,
        int directionalLightsRootParameterIndex,
        int pointLightsRootParameterIndex,
        int spotLightsRootParameterIndex,
        int areaLightsRootParameterIndex
    )
    {
        bufferManager_.SetToCommandList(
            commandList,
            lightCountsRootParameterIndex,
            directionalLightsRootParameterIndex,
            pointLightsRootParameterIndex,
            spotLightsRootParameterIndex,
            areaLightsRootParameterIndex
        );
    }

    D3D12_GPU_VIRTUAL_ADDRESS LightManager::GetLightCountsGPUAddress() const
    {
        return bufferManager_.GetLightCountsGPUAddress();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetDirectionalLightsSRVHandle() const
    {
        return bufferManager_.GetDirectionalLightsSRVHandle();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetPointLightsSRVHandle() const
    {
        return bufferManager_.GetPointLightsSRVHandle();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetSpotLightsSRVHandle() const
    {
        return bufferManager_.GetSpotLightsSRVHandle();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE LightManager::GetAreaLightsSRVHandle() const
    {
        return bufferManager_.GetAreaLightsSRVHandle();
    }

    // ==================== その他 ====================

    void LightManager::ClearAllLights()
    {
        for (Slot& slot : slots_) {
            if (slot.alive) {
                slot.alive = false;
                slot.generation++;
                slot.light = Light{};
            }
        }

        // シーン切り替え時は透過率もリセットする（次のシーンが大気非対応でも変調が残らないように）
        atmosphereSunTransmittance_ = { 1.0f, 1.0f, 1.0f };
        atmosphereMoonTransmittance_ = { 1.0f, 1.0f, 1.0f };
    }

}
