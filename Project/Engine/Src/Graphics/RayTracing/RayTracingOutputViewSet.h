#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <string>

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;

    /// @brief DXR 水面パス（屈折/コースティクス等）が使う view 毎の UAV/SRV 出力テクスチャを
    ///        まとめて管理するクラス。
    /// @details EnsureTexture() はサイズ変更時のみテクスチャを再確保し、UAV/SRV を再生成する。
    ///          各 view の現在ステートも保持し、ResourceBarrierHelper と組み合わせて使用する。
    ///          WaterRefractionRayTracingManager / WaterCausticsRayTracingManager など、
    ///          同一パターンの RT パスから共有利用されることを想定している。
    ///          view 数は固定上限 kMaxViewCount 以内で、呼び出し元が viewIndex で管理する
    ///          （現状は GameView/ReflectionView の 2 view のみ使用）。
    class RayTracingOutputViewSet {
    public:
        /// @brief 管理可能な view 数の上限（現状は GameView/ReflectionView の 2）
        static constexpr uint32_t kMaxViewCount = 2;

        /// @brief 指定 view の出力
        /// @param descriptorManager UAV/SRV 作成用
        /// @param width             テクスチャ幅
        /// @param height            テクスチャ高さ
        /// @param viewIndex         view インデックス（0 以上 ViewCount 未満）
        /// @param ownerName         ログ出力用の呼び出し元名（例: "WaterRefractionRayTracingManager"）
        /// @param uavDebugName      UAV 作成時のデバッグ名
        /// @param srvDebugName      SRV 作成時のデバッグ名
        /// @param format            テクスチャフォーマット（省略時 R16G16B16A16_FLOAT）
        /// @return 確保に成功した場合 true
        bool EnsureTexture(
            DirectXCommon* dxCommon,
            DescriptorManager* descriptorManager,
            UINT width,
            UINT height,
            uint32_t viewIndex,
            const char* ownerName,
            const std::string& uavDebugName,
            const std::string& srvDebugName,
            DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT);

        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle(uint32_t viewIndex) const { return views_[viewIndex].srvHandle; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetUAVHandle(uint32_t viewIndex) const { return views_[viewIndex].uavHandle; }
        ID3D12Resource* GetResource(uint32_t viewIndex) const { return views_[viewIndex].texture.Get(); }
        D3D12_RESOURCE_STATES& GetCurrentState(uint32_t viewIndex) { return views_[viewIndex].currentState; }

        /// @brief サイズが一致しない場合のみ、指定 view のテクスチャ/ハンドルを解放する
        /// @details 実際の再生成は次回の EnsureTexture() 呼び出しまで遅延される。
        ///          Resize() 通知を受けてから実際の Dispatch までテクスチャ確保を遅延させたい
        ///          呼び出し元（例: キャッシュ済み Resize 通知）向けのヘルパー。
        void ReleaseIfSizeMismatch(UINT width, UINT height, uint32_t viewIndex);

    private:
        struct View {
            Microsoft::WRL::ComPtr<ID3D12Resource> texture;
            D3D12_GPU_DESCRIPTOR_HANDLE uavHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
            UINT width = 0;
            UINT height = 0;
            D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        };

        View views_[kMaxViewCount]{};
    };
}
