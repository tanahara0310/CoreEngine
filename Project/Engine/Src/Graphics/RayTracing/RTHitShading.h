#pragma once

#include <d3d12.h>
#include <cstdint>

#include "Graphics/Water/WaterSurfaceData.h"

namespace CoreEngine
{
    class GraphicsCore;
    struct RenderContext;

    /// @brief レイが当たった点を照らすための入力（番号はすべてシェーダー可視ヒープ内のインデックス）
    /// @details HLSL 側 Include/RayTracing/RTHitShading.hlsli の RTHitShadingConstants に詰める。
    ///          照らし方は DeferredLighting と同じ（ライト・空の光・雲の影・濡れ跡）。
    struct RTHitShadingInput {
        bool enabled = false;                         ///< false なら当たった点を照らさない
        uint32_t instanceTableIndex = UINT32_MAX;     ///< RTHitInstance の表
        uint32_t subMeshTableIndex = UINT32_MAX;      ///< RTHitSubMesh の表
        uint32_t directionalLightsIndex = UINT32_MAX; ///< DirectionalLightData の配列
        uint32_t directionalLightCount = 0;
        uint32_t skyIrradianceSHIndex = UINT32_MAX;   ///< 空の放射照度の SH9
        bool skyAmbientEnabled = false;
        float skyAmbientScale = 0.0f;                 ///< 空の輝度単位 → サーフェス光単位
        uint32_t skySpecularMapIndex = UINT32_MAX;    ///< 空のスペキュラキューブマップ
        bool skySpecularEnabled = false;
        uint32_t cloudShadowMapIndex = UINT32_MAX;    ///< 雲の影のマップ（UINT32_MAX = 雲の影なし）
        float cloudShadowStrength = 0.0f;             ///< 雲の影の強さ
        float cloudShadowRegionCenterXZ[2] = {};      ///< 雲の影のマップが覆う範囲の中心
        float cloudShadowRegionSize = 0.0f;           ///< 雲の影のマップが覆う範囲の幅 [m]
        float cloudShadowAnchorY = 0.0f;              ///< 雲の影のマップを置く高さ（雲底）
        float cloudShadowEdgeFadeStart = 1.0f;        ///< 範囲の端で雲の影を消し始める正規化半径
        WaterWetnessBinding wetSurface{};             ///< 濡れ跡の表（enabled = 0 なら濡れ跡なし）
        float wetSurfaceRegionCenterXZ[2] = {};       ///< 濡れ跡を付ける範囲（水面の範囲）の中心
        float wetSurfaceRegionHalfExtentXZ[2] = {};   ///< 濡れ跡を付ける範囲の半分の幅
    };

    /// @brief このフレームの、レイが当たった点を照らすための入力を集める（材質の表が無いフレームは enabled = false）
    RTHitShadingInput BuildRTHitShadingInput(const RenderContext& context);

    /// @brief 入力を RTHitShadingConstants に詰めて記録中のフレームの UploadRing に置き、GPU アドレスを返す
    D3D12_GPU_VIRTUAL_ADDRESS UploadRTHitShadingConstants(GraphicsCore& dx, const RTHitShadingInput& input);
}
