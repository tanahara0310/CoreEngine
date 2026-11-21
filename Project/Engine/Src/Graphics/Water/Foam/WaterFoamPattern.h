#pragma once

#include <d3d12.h>

namespace CoreEngine
{
    /// @brief 泡の模様の画像（泡の網目と気泡の粒）を読み込んで持つ
    /// @details 画像は Tools/Water/generate_foam_textures.py が作る BC5 の DDS（ミップも台本が作る）。
    ///          R = 泡になる順（どのミップも [0,1] に一様に散らばる）・G = 泡の明るさ
    class WaterFoamPattern
    {
    public:
        /// @brief 泡の網目の画像（1 枚 4m 四方）
        static constexpr const char* kNetTexture = "Engine/Assets/Textures/Water/FoamNet.dds";

        /// @brief 気泡の粒の画像（1 枚 0.5m 四方）
        static constexpr const char* kGrainTexture = "Engine/Assets/Textures/Water/FoamGrain.dds";

        /// @brief 2 枚の画像を読み込む（描画するフレームの外で呼ぶ）
        /// @return 2 枚とも読めたら true
        bool Initialize();

        /// @brief 2 枚とも読めているか
        bool IsInitialized() const { return initialized_; }

        /// @brief 泡の網目の画像の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetNetSRV() const { return netSRV_; }

        /// @brief 気泡の粒の画像の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetGrainSRV() const { return grainSRV_; }

    private:
        D3D12_GPU_DESCRIPTOR_HANDLE netSRV_{};
        D3D12_GPU_DESCRIPTOR_HANDLE grainSRV_{};
        bool initialized_ = false;
    };
}
