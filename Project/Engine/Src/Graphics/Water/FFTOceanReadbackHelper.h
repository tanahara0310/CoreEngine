#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

namespace CoreEngine
{
    struct FFTOceanReadbackTextureCreateRequest {
        ID3D12Device* device = nullptr;
        ID3D12Resource* texture = nullptr;
        const char* debugName = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource>* outReadbackBuffer = nullptr;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT* outLayout = nullptr;
        UINT64* outTotalBytes = nullptr;
    };

    /// @brief FFT Ocean のGPU readback解析を担当するヘルパー
    class FFTOceanReadbackHelper {
    public:
        struct SurfaceReadbackRequest {
            ID3D12Resource* displacementReadbackBuffer = nullptr;
            ID3D12Resource* normalReadbackBuffer = nullptr;
            UINT64 displacementReadbackBytes = 0;
            UINT64 normalReadbackBytes = 0;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT displacementLayout{};
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT normalLayout{};
            uint32_t resolution = 0;
            uint64_t sequence = 0;
        };

        struct SpectrumReadbackRequest {
            ID3D12Resource* spectrumAReadbackBuffer = nullptr;
            ID3D12Resource* spectrumBReadbackBuffer = nullptr;
            UINT64 spectrumAReadbackBytes = 0;
            UINT64 spectrumBReadbackBytes = 0;
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT spectrumALayout{};
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT spectrumBLayout{};
            uint32_t resolution = 0;
            uint64_t sequence = 0;
            bool isIfft = false;
        };

        /// @brief Readback用バッファを作成する
        static Microsoft::WRL::ComPtr<ID3D12Resource> CreateReadbackBuffer(ID3D12Device* device, UINT64 sizeInBytes);

        /// @brief テクスチャに対応する Readback バッファを作成する
        static bool CreateTextureReadbackBuffer(const FFTOceanReadbackTextureCreateRequest& request);

        /// @brief displacement/normal readbackの内容をログ出力する
        /// @return 成功時 true（pending解除可能）
        static bool TryLogSurfaceReadback(const SurfaceReadbackRequest& request);

        /// @brief evolution/ifft readbackの内容をログ出力する
        /// @return 成功時 true（pending解除可能）
        static bool TryLogSpectrumReadback(const SpectrumReadbackRequest& request);
    };
}
