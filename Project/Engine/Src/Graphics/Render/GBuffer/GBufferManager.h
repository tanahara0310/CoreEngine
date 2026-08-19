#pragma once

#include <array>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

namespace CoreEngine
{
    class DescriptorManager;
    class DepthStencilManager;

    /// @brief G-Buffer管理クラス
    /// @note Deferred/Hybrid Rendering への移行基盤。
    ///       移行完了までは従来の OffScreenRenderTarget 系も互換経路として並存する。
    class GBufferManager {
    public:
        /// @brief G-Bufferのターゲットタイプ
        /// @note WorldPosition ターゲットは廃止済み（2026-07-21）。DeferredLighting 等は
        ///       SceneDepth + View*Projection の逆行列からワールド座標を復元する
        ///       （MRT 5枚→4枚・36B/px→20B/px の帯域削減が目的）。
        enum class Target : uint32_t {
            AlbedoAO = 0,       ///< PBR: rgb=アルベド,a=AO
            NormalRoughness,    ///< PBR: rgb=ワールド法線(encoded),a=符号付きラフネス（符号=IBL有効/無効, 0=アンリット）
            EmissiveMetallic,   ///< PBR: rgb=エミッシブ,a=メタリック
            MotionVector,       ///< rg=NDC空間モーションベクター（現フレーム-前フレーム）
            Count
        };

        // ターゲット数の定数
        static constexpr uint32_t kTargetCount = static_cast<uint32_t>(Target::Count);

        /// @brief GBuffer レンダーターゲットのフォーマット定義
        /// PSO 作成時と GBufferManager 初期化時の唯一の定義場所
        static constexpr DXGI_FORMAT kRenderTargetFormats[kTargetCount] = {
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,  // AlbedoAO
                DXGI_FORMAT_R16G16B16A16_FLOAT,   // NormalRoughness
                DXGI_FORMAT_R8G8B8A8_UNORM,       // EmissiveMetallic
                DXGI_FORMAT_R16G16_FLOAT,          // MotionVector
            };

        /// @brief 初期化
        /// @param device D3D12デバイス 
        /// @param descriptorManager DescriptorManager（RTV/SRVの作成に使用）
        /// @param width 初期幅
        /// @param height 初期高さ
        void Initialize(ID3D12Device* device, DescriptorManager* descriptorManager, int32_t width, int32_t height);
        /// @brief 画面サイズ変更に合わせて全ターゲットを作り直す
        void Resize(int32_t width, int32_t height);

        /// @brief ジオメトリパスの描画セットアップを行う
        /// @param cmdList コマンドリスト
        /// @param depthStencilManager 深度ステンシル管理
        /// @param srvHeap SRVデスクリプタヒープ
        void BeginGeometryPass(
            ID3D12GraphicsCommandList* cmdList,
            DepthStencilManager* depthStencilManager,
            ID3D12DescriptorHeap* srvHeap);

        /// @brief 指定ターゲットのリソース
        ID3D12Resource* GetResource(Target target) const;
        /// @brief 指定ターゲットの RTV ハンドル
        D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle(Target target) const;
        /// @brief 指定ターゲットの SRV ハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE GetSRVHandle(Target target) const;
        /// @brief 指定ターゲットのフォーマット
        DXGI_FORMAT GetFormat(Target target) const;
        /// @brief 全ターゲットのフォーマット配列（PSO の RTV 設定に渡す）
        const DXGI_FORMAT* GetFormats() const;
        /// @brief 指定ターゲットの現在ステートへの参照（バリア時に更新される）
        D3D12_RESOURCE_STATES& GetCurrentState(Target target);

        uint32_t GetTargetCount() const { return kTargetCount; }
        int32_t GetWidth() const { return currentWidth_; }
        int32_t GetHeight() const { return currentHeight_; }
        bool IsInitialized() const { return isInitialized_; }

    private:
        /// @brief G-Buffer の 1 枚分（実体・RTV/SRV・現在ステート）
        struct TargetResource {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
            D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{};
            D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{};
            D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        };

        void CreateOrResizeTarget(Target target);
        void CreateViews(TargetResource& targetResource, Target target, bool createNewDescriptors);
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
