#include "pch.h"
#include "LightBufferManager.h"
#include "Graphics/RootSignature/ShaderBinder.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/Logger/Logger.h"
#include <span>

namespace CoreEngine
{
    template <typename T>
    void LightBufferManager::LightArray<T>::Initialize(GraphicsCore& graphics, uint32_t capacity)
    {
        buffer_.Initialize(graphics, capacity, bufferName_);
    }

    template <typename T>
    uint32_t LightBufferManager::LightArray<T>::Set(const std::vector<T>& lights)
    {
        // 確保した数を超えた分は写さない。警告は超えている間に 1 回だけ出す
        if (lights.size() > buffer_.Capacity()) {
            if (!overflowLogged_) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Graphics,
                    "{} のライトが {} 個あり、バッファに入る {} 個を超えた分は描かない",
                    typeName_, lights.size(), buffer_.Capacity());
                overflowLogged_ = true;
            }
        } else {
            overflowLogged_ = false;
        }

        return buffer_.Set(std::span<const T>(lights));
    }

    template <typename T>
    void LightBufferManager::LightArray<T>::Bind(ShaderBinder& binder, RootSlot slot) const
    {
        // ハンドルが 0 のとき（初期化前）は差せないので、ここで弾く
        if (const D3D12_GPU_DESCRIPTOR_HANDLE handle = buffer_.Srv(); handle.ptr != 0) {
            binder.Set(slot, handle);
        }
    }

    // 種類ごとの配列と個数の置き場を作る。最大数は起動時に決め打ちで、以後変えない
    void LightBufferManager::Initialize(
        GraphicsCore& graphics,
        uint32_t maxDirectionalLights,
        uint32_t maxPointLights,
        uint32_t maxSpotLights,
        uint32_t maxAreaLights
    )
    {
        directionalLights_.Initialize(graphics, maxDirectionalLights);
        pointLights_.Initialize(graphics, maxPointLights);
        spotLights_.Initialize(graphics, maxSpotLights);
        areaLights_.Initialize(graphics, maxAreaLights);
        lightCounts_.Initialize(graphics.GetUploadRing());
    }

    void LightBufferManager::UpdateBuffers(
        const std::vector<DirectionalLightData>& directionalLights,
        const std::vector<PointLightData>& pointLights,
        const std::vector<SpotLightData>& spotLights,
        const std::vector<AreaLightData>& areaLights
    )
    {
        // 種別ごとに StructuredBuffer を持つ。シェーダーへ渡す数は写した数
        LightCounts counts{};
        counts.directionalLightCount = directionalLights_.Set(directionalLights);
        counts.pointLightCount = pointLights_.Set(pointLights);
        counts.spotLightCount = spotLights_.Set(spotLights);
        counts.areaLightCount = areaLights_.Set(areaLights);
        lightCounts_.Set(counts);
    }

    void LightBufferManager::SetToCommandList(
        ShaderBinder& binder,
        RootSlot lightCounts,
        RootSlot directionalLights,
        RootSlot pointLights,
        RootSlot spotLights,
        RootSlot areaLights)
    {
        // 未解決スロット（そのシェーダーが参照しない種別）は ShaderBinder 側で no-op になる。
        // アドレスが 0 のとき（初期化前）は差せないので、ここで弾く。
        if (const D3D12_GPU_VIRTUAL_ADDRESS address = lightCounts_.Address(); address != 0) {
            binder.Set(lightCounts, address);
        }
        directionalLights_.Bind(binder, directionalLights);
        pointLights_.Bind(binder, pointLights);
        spotLights_.Bind(binder, spotLights);
        areaLights_.Bind(binder, areaLights);
    }
}
