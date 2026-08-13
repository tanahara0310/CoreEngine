#pragma once
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include <d3d12.h>
#include <cstdint>

namespace CoreEngine
{
    /// @brief gIBLParams 定数バッファのレイアウト（HLSL IBLSceneParams 構造体と一致）
    struct IBLSceneParamsCPU {
        float rotationX;
        float rotationY;
        float rotationZ;
        float environmentIntensity; ///< 環境輝度スケール（SkyBox intensity と連動）
        uint32_t sceneIBLEnabled;   ///< シーンに IBL マップ（Irradiance/Prefiltered/BRDF LUT）が揃っているか
        float padding[3];
    };
    static_assert(sizeof(IBLSceneParamsCPU) == 32, "IBLSceneParamsCPU size mismatch");

    static constexpr Cb::Field kIBLSceneParamsFields[] = {
        CB_FIELD(IBLSceneParamsCPU, rotationX), CB_FIELD(IBLSceneParamsCPU, rotationY),
        CB_FIELD(IBLSceneParamsCPU, rotationZ), CB_FIELD(IBLSceneParamsCPU, environmentIntensity),
        CB_FIELD(IBLSceneParamsCPU, sceneIBLEnabled), CB_FIELD(IBLSceneParamsCPU, padding),
    };
    CB_VERIFY_LAYOUT(IBLSceneParamsCPU, kIBLSceneParamsFields);
    // HLSL 側の照合（CB_BIND_HLSL）は入れていない。"gIBLParams" という変数名を
    // ConstantBuffer<IBLParams>（マテリアル側・16B）と ConstantBuffer<IBLSceneParams>（本構造体・32B）が
    // 別レイアウトで共用しており、名前だけではどちらを指すか決められないため。
    // 照合を有効にしたい場合は HLSL 側の変数名を分けること。
}
