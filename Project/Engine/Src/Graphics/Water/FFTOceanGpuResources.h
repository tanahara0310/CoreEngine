#pragma once

#include <array>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    /// @brief リソース本体・追跡ステート・SRV/UAV ビューをひとまとめにした束
    /// @details 「1 テクスチャ = 1 構造体」に畳むことで、Helper 群が引数爆発せずに済む。
    struct FFTOceanGpuTexture : GpuResource {
        DescriptorHandle srv{};
        DescriptorHandle uav{};
    };

    /// @brief ping-pong ペア（IFFT の read/write 交互切替・泡の前後フレーム）
    using FFTOceanPingPong = std::array<FFTOceanGpuTexture, 2>;

    /// @brief カスケード 1 本分の初期スペクトルバッファ一式
    /// @details CS が読むのは DEFAULT 常駐側（SRV も DEFAULT 側に作る）。
    ///          CPU は UPLOAD 側へ書き、dirty 時に次の Dispatch が
    ///          UPLOAD → DEFAULT のコピーを積む（毎フレーム PCIe 読みの禁止）。
    struct FFTOceanSpectrumBufferSet {
        GpuResource defaultBuffer;   ///< CS が読む DEFAULT 常駐側（ステート追跡込み）
        Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
        void* mapped = nullptr; // UPLOAD 側の常時マップ先（要素型は Manager が知る）
        DescriptorHandle srv{};
    };
}
