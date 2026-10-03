#include "pch.h"
#include "LightBufferManager.h"
#include "Graphics/RootSignature/ShaderBinder.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Utility/Logger/Logger.h"
#include <span>

namespace CoreEngine
{
    // 種類ごとの配列と個数の置き場を作る。最大数は起動時に決め打ちで、以後変えない
    void LightBufferManager::Initialize(
        GraphicsCore& graphics,
        uint32_t maxDirectionalLights,
        uint32_t maxPointLights,
        uint32_t maxSpotLights,
        uint32_t maxAreaLights
    )
    {
        directionalLights_.Initialize(graphics, maxDirectionalLights, "DirectionalLights");
        pointLights_.Initialize(graphics, maxPointLights, "PointLights");
        spotLights_.Initialize(graphics, maxSpotLights, "SpotLights");
        areaLights_.Initialize(graphics, maxAreaLights, "AreaLights");
        lightCounts_.Initialize(graphics.GetUploadRing());
    }

    template <typename T>
    uint32_t LightBufferManager::CopyLights(
        PerFrameStructuredBuffer<T>& buffer,
        const std::vector<T>& lights,
        bool& overflowLogged,
        const char* typeName
    )
    {
        // 確保した数を超えた分は写さない。警告は超えている間に 1 回だけ出す
        if (lights.size() > buffer.Capacity()) {
            if (!overflowLogged) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Graphics,
                    "{} のライトが {} 個あり、バッファに入る {} 個を超えた分は描かない",
                    typeName, lights.size(), buffer.Capacity());
                overflowLogged = true;
            }
        } else {
            overflowLogged = false;
        }

        return buffer.Set(std::span<const T>(lights));
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
        counts.directionalLightCount =
            CopyLights(directionalLights_, directionalLights, directionalOverflowLogged_, "Directional");
        counts.pointLightCount = CopyLights(pointLights_, pointLights, pointOverflowLogged_, "Point");
        counts.spotLightCount = CopyLights(spotLights_, spotLights, spotOverflowLogged_, "Spot");
        counts.areaLightCount = CopyLights(areaLights_, areaLights, areaOverflowLogged_, "Area");
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
        // アドレスやハンドルが 0 のとき（初期化前）は差せないので、ここで弾く。
        if (const D3D12_GPU_VIRTUAL_ADDRESS address = lightCounts_.Address(); address != 0) {
            binder.Set(lightCounts, address);
        }
        if (const D3D12_GPU_DESCRIPTOR_HANDLE handle = directionalLights_.Srv(); handle.ptr != 0) {
            binder.Set(directionalLights, handle);
        }
        if (const D3D12_GPU_DESCRIPTOR_HANDLE handle = pointLights_.Srv(); handle.ptr != 0) {
            binder.Set(pointLights, handle);
        }
        if (const D3D12_GPU_DESCRIPTOR_HANDLE handle = spotLights_.Srv(); handle.ptr != 0) {
            binder.Set(spotLights, handle);
        }
        if (const D3D12_GPU_DESCRIPTOR_HANDLE handle = areaLights_.Srv(); handle.ptr != 0) {
            binder.Set(areaLights, handle);
        }
    }
}
