#pragma once

#include <d3d12.h>
#include <vector>

#include "LightData.h"
#include "Graphics/RHI/Resource/PerFrameConstants.h"
#include "Graphics/RHI/Resource/PerFrameStructuredBuffer.h"
#include "Graphics/RootSignature/RootSlot.h"

namespace CoreEngine
{
    class ShaderBinder;
    class GraphicsCore;

    /// @brief ライトバッファの管理クラス
    /// @details 種類ごとのライトの配列（StructuredBuffer）と個数（定数）を持ち、
    ///          GPU へは差すときに、記録中のフレームの置き場へ写す
    class LightBufferManager
    {
    public:
        /// @brief 初期化（max* は種別ごとのライト最大数）
        void Initialize(
            GraphicsCore& graphics,
            uint32_t maxDirectionalLights,
            uint32_t maxPointLights,
            uint32_t maxSpotLights,
            uint32_t maxAreaLights
        );

        /// @brief ライトの値を差し替える（GPU へは次に差すときに写す）
        /// @note 種類ごとに、確保した数を超えた分は写さない（超えている間に 1 回だけ警告する）。
        void UpdateBuffers(
            const std::vector<DirectionalLightData>& directionalLights,
            const std::vector<PointLightData>& pointLights,
            const std::vector<SpotLightData>& spotLights,
            const std::vector<AreaLightData>& areaLights
        );

        /// @brief コマンドリストにライトをセット（ShaderBinder 経由）
        /// @details Set* の選択は RootSlot の種別から ShaderBinder が行う。
        ///          binder が差したことを記録するので、Draw 前の取りこぼし検出が効く。
        void SetToCommandList(
            ShaderBinder& binder,
            RootSlot lightCounts,
            RootSlot directionalLights,
            RootSlot pointLights,
            RootSlot spotLights,
            RootSlot areaLights
        );

        /// @brief ライトカウントのGPU仮想アドレスを取得（そのフレームの記録中だけ有効）
        D3D12_GPU_VIRTUAL_ADDRESS GetLightCountsGPUAddress() const { return lightCounts_.Address(); }

        /// @brief ディレクショナルライトSRVのGPUハンドルを取得（そのフレームの記録中だけ今の値を保つ）
        D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalLightsSRVHandle() const { return directionalLights_.Srv(); }

        /// @brief ポイントライトSRVのGPUハンドルを取得（そのフレームの記録中だけ今の値を保つ）
        D3D12_GPU_DESCRIPTOR_HANDLE GetPointLightsSRVHandle() const { return pointLights_.Srv(); }

        /// @brief スポットライトSRVのGPUハンドルを取得（そのフレームの記録中だけ今の値を保つ）
        D3D12_GPU_DESCRIPTOR_HANDLE GetSpotLightsSRVHandle() const { return spotLights_.Srv(); }

        /// @brief エリアライトSRVのGPUハンドルを取得（そのフレームの記録中だけ今の値を保つ）
        D3D12_GPU_DESCRIPTOR_HANDLE GetAreaLightsSRVHandle() const { return areaLights_.Srv(); }

    private:
        /// @brief ライトをバッファに入る数まで写す
        /// @param overflowLogged 超えている間に警告を出したか
        /// @return 写した数
        template <typename T>
        static uint32_t CopyLights(
            PerFrameStructuredBuffer<T>& buffer,
            const std::vector<T>& lights,
            bool& overflowLogged,
            const char* typeName
        );

    private:
        PerFrameStructuredBuffer<DirectionalLightData> directionalLights_;
        PerFrameStructuredBuffer<PointLightData> pointLights_;
        PerFrameStructuredBuffer<SpotLightData> spotLights_;
        PerFrameStructuredBuffer<AreaLightData> areaLights_;
        PerFrameConstants<LightCounts> lightCounts_;

        // 種類ごとに、確保した数を超えている間に警告を出したか
        bool directionalOverflowLogged_ = false;
        bool pointOverflowLogged_ = false;
        bool spotOverflowLogged_ = false;
        bool areaOverflowLogged_ = false;
    };
}
