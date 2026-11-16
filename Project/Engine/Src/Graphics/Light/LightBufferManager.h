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
        /// @brief 種類 1 つ分のライトの配列（StructuredBuffer）
        /// @tparam T GPU へ写すライトの型
        template <typename T>
        class LightArray
        {
        public:
            /// @param bufferName バッファのデバッグ名
            /// @param typeName 警告に出す種類の名前
            LightArray(const char* bufferName, const char* typeName) : bufferName_(bufferName), typeName_(typeName) {}

            /// @brief フレームスロットの数だけ UPLOAD バッファと SRV を作る
            /// @param capacity ライトの最大数
            void Initialize(GraphicsCore& graphics, uint32_t capacity);

            /// @brief ライトをバッファに入る数まで写す（超えている間に 1 回だけ警告する）
            /// @return 写した数
            uint32_t Set(const std::vector<T>& lights);

            /// @brief 配列を差す（作る前は差さない）
            void Bind(ShaderBinder& binder, RootSlot slot) const;

            /// @brief SRV の GPU ハンドル（そのフレームの記録中だけ今の値を保つ）
            D3D12_GPU_DESCRIPTOR_HANDLE Srv() const { return buffer_.Srv(); }

        private:
            PerFrameStructuredBuffer<T> buffer_;
            const char* bufferName_;
            const char* typeName_;
            bool overflowLogged_ = false; ///< 確保した数を超えている間に警告を出したか
        };

        LightArray<DirectionalLightData> directionalLights_{ "DirectionalLights", "Directional" };
        LightArray<PointLightData> pointLights_{ "PointLights", "Point" };
        LightArray<SpotLightData> spotLights_{ "SpotLights", "Spot" };
        LightArray<AreaLightData> areaLights_{ "AreaLights", "Area" };
        PerFrameConstants<LightCounts> lightCounts_;
    };
}
