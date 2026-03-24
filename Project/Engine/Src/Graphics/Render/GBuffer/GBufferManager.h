#pragma once

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    class DescriptorManager;

    /// @brief G-Buffer管理クラス
    /// @note Deferred/Hybrid Rendering への移行基盤。
    ///       移行完了までは従来の OffScreenRenderTarget 系も互換経路として並存する。
    class GBufferManager {
    public:
        /// @brief G-Bufferのターゲットタイプ
        enum class Target : uint32_t {
            AlbedoAO = 0,       ///< PBR: rgb=アルベド,a=AO / 非PBR: rgb=アルベド,a=toonThreshold
            NormalRoughness,    ///< PBR: rgb=ワールド法線(encoded),a=ラフネス / 非PBR: a=shininess
            EmissiveMetallic,   ///< PBR: rgb=エミッシブ,a=メタリック / 非PBR: a=shadingMode*0.25
            WorldPosition,      ///< rgb=ワールド座標, a=ピクセルフラグ (0=背景, 1=非PBR, 2=PBR, 3=PBR+IBL)
            Count
        };

        // ターゲット数の定数
        static constexpr uint32_t kTargetCount = static_cast<uint32_t>(Target::Count);

        /// @brief GBuffer レンダーターゲットのフォーマット定義
        /// PSO 作成時と GBufferManager 初期化時の唯一の定義場所
        static constexpr DXGI_FORMAT kRenderTargetFormats[kTargetCount] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,       // AlbedoAO
            DXGI_FORMAT_R16G16B16A16_FLOAT,   // NormalRoughness
            DXGI_FORMAT_R8G8B8A8_UNORM,       // EmissiveMetallic
            DXGI_FORMAT_R32G32B32A32_FLOAT    // WorldPosition
        };

        /// @brief 初期化
        /// @param device D3D12デバイス 
        /// @param descriptorManager DescriptorManager（RTV/SRVの作成に使用）
        /// @param width 初期幅
        /// @param height 初期高さ
        void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, int32_t width, int32_t height);
        void Resize(int32_t width, int32_t height);

        void BeginGeometryPass(
            ID3D12GraphicsCommandList* cmdList,
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
            ID3D12DescriptorHeap* srvHeap,
            bool clearDepth = true);

        void EndGeometryPass(ID3D12GraphicsCommandList* cmdList);

        ID3D12Resource* GetResource(Target target) const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(Target target) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle(Target target) const;
        DXGI_FORMAT GetFormat(Target target) const;
        const DXGI_FORMAT* GetFormats() const;

        uint32_t GetTargetCount() const { return kTargetCount; }
        int32_t GetWidth() const { return currentWidth_; }
        int32_t GetHeight() const { return currentHeight_; }
        bool IsInitialized() const { return isInitialized_; }

    private:
        struct TargetResource {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
            D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        };

        void CreateOrResizeTarget(Target target);
        void CreateViews(TargetResource& targetResource, Target target, bool createNewDescriptors);
        void TransitionTarget(
            ID3D12GraphicsCommandList* cmdList,
            TargetResource& targetResource,
            D3D12_RESOURCE_STATES newState);
        void ValidateState() const;
        uint32_t ToIndex(Target target) const;

    private:
        std::array<TargetResource, kTargetCount> targets_{};
        ID3D12Device* device_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        bool isInitialized_ = false;
        int32_t currentWidth_ = 0;
        int32_t currentHeight_ = 0;
    };
}
