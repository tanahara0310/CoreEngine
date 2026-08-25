#pragma once

#include "Graphics/RHI/Resource/GpuResource.h"
#include <d3d12.h>
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include <wrl.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Math/Geometry/Shapes.h"
#include "Math/Matrix/Matrix4x4.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class ShaderCompiler;
    class ShaderReflectionBuilder;
    class ShaderReflectionData;
    class RootSignatureManager;

    /// @brief Hi-Z（階層深度）オクルージョンカリングシステム
    /// @details SceneDepth から深度ピラミッドを作り、ワールド AABB を CS で遮蔽判定して Readback する。
    ///          結果は 2 フレーム後の Model::Draw で Submit スキップに使う。判定・適用は GameView のみ。
    class HiZOcclusionSystem {
    public:
        static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;
        static constexpr uint32_t kMaxQueries = 4096;

        HiZOcclusionSystem() = default;
        ~HiZOcclusionSystem() = default;
        HiZOcclusionSystem(const HiZOcclusionSystem&) = delete;
        HiZOcclusionSystem& operator=(const HiZOcclusionSystem&) = delete;

        // ---------------------------------------------------------------
        // CPU 側フレームフロー
        // ---------------------------------------------------------------

        /// @brief フレーム先頭で呼ぶ。完了済みフレームの可視性 Readback を反映する
        /// @param recordingFrameIndex 今フレームのスロット番号（FrameContext::frameIndex）
        void BeginFrame(uint32_t recordingFrameIndex);

        /// @brief AABB 収集と可視判定適用の有効化（メイン GameView の描画構築中のみ true にする）
        void SetCollectEnabled(bool enabled) { collectEnabled_ = enabled; }
        bool IsCollectEnabled() const { return enabled_ && collectEnabled_; }

        /// @brief 判定スロットを確保する（満杯時は kInvalidId）
        uint32_t RegisterTarget();

        /// @brief 判定スロットを解放する
        void UnregisterTarget(uint32_t id);

        /// @brief 判定に使うメインカメラの View * Projection を設定する
        void SetViewProjection(const Matrix4x4& viewProj) { viewProj_ = viewProj; }

        /// @brief 今フレームの判定対象としてワールド AABB を登録する
        /// @note 遮蔽スキップ中のモデルも毎フレーム登録し続けること（再可視化判定のため）
        void SubmitBounds(uint32_t id, const BoundingBox& worldBounds);

        /// @brief 直近の判定結果を返す（未測定・結果が古い・無効 ID は常に可視）
        bool IsVisible(uint32_t id) const;

        // ---------------------------------------------------------------
        // GPU 側（HiZOcclusionPass から呼ぶ）
        // ---------------------------------------------------------------

        /// @brief Hi-Z ピラミッド構築 → 遮蔽判定 CS → Readback コピーを記録する
        /// @param depthSRV 深度 SRV（NON_PIXEL_SHADER_RESOURCE 状態で渡すこと）
        void ExecuteCulling(
            ID3D12GraphicsCommandList* cmdList,
            GraphicsCore* dxCommon,
            ID3D12Resource* depthResource,
            D3D12_GPU_DESCRIPTOR_HANDLE depthSRV);

        // ---------------------------------------------------------------
        // 設定
        // ---------------------------------------------------------------

        /// @brief システム全体の有効/無効（無効時は IsVisible が常に true）
        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool IsEnabled() const { return enabled_; }

        /// @brief 全 GPU リソースを解放する
        /// @details EngineSystem::Finalize からデバイス破棄前に明示的に呼ぶこと
        ///          （呼ばないと LeakChecker の ReportLiveObjects にリソースが報告される）。
        ///          Shutdown 後もインスタンス自体は全 Model の破棄まで生存させること
        ///          （~ModelVisibility の UnregisterTarget は slots_ が空なので安全に空振りする）。
        void Shutdown();

    private:
        // フレームリング数（CommandManager の frameCount 最大値以上にする）
        static constexpr uint32_t kFrameRing = 3;
        // Hi-Z ピラミッドの最大ミップ数（8K 解像度でも 13 段で足りる）
        static constexpr uint32_t kMaxHiZMips = 16;
        // この経過フレーム数を超えて結果が更新されないスロットは可視扱いに戻す
        // （フラスタム外に出て判定対象から外れたモデルの stale 結果対策）
        static constexpr uint64_t kResultStaleFrames = 8;
        // 保守側に倒す深度バイアス（2 フレーム遅延適用のポップイン緩和）
        static constexpr float kDepthBias = 0.0005f;
        // 画面占有カリング閾値（Hi-Z mip0 テクセル単位 = スクリーン約 2px。
        // 投影矩形が縦横ともにこれ未満なら見えていても描画をスキップする。0 で無効）
        static constexpr float kMinRectTexels = 1.0f;

        /// @brief GPU 側 AABB データ（HiZOcclusionCull.CS.hlsl の BoundsData と一致必須）
        struct BoundsGPU {
            float minX, minY, minZ, pad0;
            float maxX, maxY, maxZ, pad1;
        };

        static constexpr Cb::Field kBoundsGPUFields[] = {
            CB_FIELD(BoundsGPU, minX), CB_FIELD(BoundsGPU, minY), CB_FIELD(BoundsGPU, minZ),
            CB_FIELD(BoundsGPU, pad0), CB_FIELD(BoundsGPU, maxX), CB_FIELD(BoundsGPU, maxY),
            CB_FIELD(BoundsGPU, maxZ), CB_FIELD(BoundsGPU, pad1),
        };
        CB_VERIFY_STRIDE(BoundsGPU, kBoundsGPUFields);

        /// @brief 判定 CS の定数（HiZOcclusionCull.CS.hlsl の HiZCullParams と一致必須）
        struct CullParamsGPU {
            Matrix4x4 viewProj;
            float hiZWidth, hiZHeight;
            uint32_t mipCount, queryCount;
            float depthBias;
            float minRectTexels;
            float pad[2];
        };

        static constexpr Cb::Field kCullParamsGPUFields[] = {
            CB_FIELD(CullParamsGPU, viewProj), CB_FIELD(CullParamsGPU, hiZWidth),
            CB_FIELD(CullParamsGPU, hiZHeight), CB_FIELD(CullParamsGPU, mipCount),
            CB_FIELD(CullParamsGPU, queryCount), CB_FIELD(CullParamsGPU, depthBias),
            CB_FIELD(CullParamsGPU, minRectTexels), CB_FIELD(CullParamsGPU, pad),
        };
        CB_VERIFY_LAYOUT(CullParamsGPU, kCullParamsGPUFields);
        CB_BIND_HLSL(CullParamsGPU, kCullParamsGPUFields, "HiZCullParams");

        /// @brief 構築 CS の定数（HiZBuild.CS.hlsl の HiZBuildParams と一致必須）
        struct BuildParamsGPU {
            uint32_t destW, destH, srcW, srcH;
        };

        static constexpr Cb::Field kBuildParamsGPUFields[] = {
            CB_FIELD(BuildParamsGPU, destW), CB_FIELD(BuildParamsGPU, destH), CB_FIELD(BuildParamsGPU, srcW),
            CB_FIELD(BuildParamsGPU, srcH),
        };
        CB_VERIFY_LAYOUT(BuildParamsGPU, kBuildParamsGPUFields);
        CB_BIND_HLSL(BuildParamsGPU, kBuildParamsGPUFields, "HiZBuildParams");

        /// @brief 判定スロット 1 つ分の状態（可視判定と、その結果が何フレーム目のものか）
        struct TargetSlot {
            bool allocated = false;
            bool visible = true;
            uint64_t lastResultFrame = 0; // 0 = 未測定
        };

        /// @brief 今フレームに判定を投げる AABB（GPU へ転送する前の待ち行列）
        struct PendingQuery {
            uint32_t id;
            BoundsGPU bounds;
        };

        bool InitializeIfNeeded(GraphicsCore* dxCommon);
        bool BuildPipelines(ID3D12Device* device);
        bool CreateBuffers(ID3D12Device* device);
        bool EnsureHiZResources(ID3D12Device* device, uint32_t depthWidth, uint32_t depthHeight);
        void BuildHiZPyramid(ID3D12GraphicsCommandList* cmdList, D3D12_GPU_DESCRIPTOR_HANDLE depthSRV);
        void DispatchCull(ID3D12GraphicsCommandList* cmdList, uint32_t frameIndex, uint32_t queryCount);

        // ---- CPU 状態 ----
        bool enabled_ = true;         // ユーザートグル（EngineStatsWindow から切替）
        bool collectEnabled_ = false; // メイン GameView 構築中のみ true
        bool initialized_ = false;
        bool initializeFailed_ = false;
        uint64_t frameCounter_ = 0;
        Matrix4x4 viewProj_{};

        std::vector<TargetSlot> slots_;
        std::vector<uint32_t> freeIds_;
        std::vector<PendingQuery> pendingQueries_;

        // ---- パイプライン ----
        std::unique_ptr<ShaderCompiler> shaderCompiler_;
        std::unique_ptr<ShaderReflectionBuilder> reflectionBuilder_;
        std::unique_ptr<RootSignatureManager> buildRootSignatureMg_;
        std::unique_ptr<RootSignatureManager> cullRootSignatureMg_;
        std::unique_ptr<ShaderReflectionData> buildReflectionData_;
        std::unique_ptr<ShaderReflectionData> cullReflectionData_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> buildPso_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> cullPso_;
        int buildSourceIdx_ = -1;
        int buildDestIdx_ = -1;
        int buildParamsIdx_ = -1;
        int cullBoundsIdx_ = -1;
        int cullHiZIdx_ = -1;
        int cullVisibilityIdx_ = -1;
        int cullParamsIdx_ = -1;

        // ---- GPU リソース ----
        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;

        // Hi-Z ピラミッド（R32_FLOAT、フルミップチェーン。フレーム間は全ミップ UAV 状態で均一化）
        /// @brief Hi-Z ピラミッド。ミップごとに状態が食い違うのでサブリソース単位で追跡する
        GpuResource hiZTexture_;
        uint32_t hiZWidth_ = 0;   // mip0 の幅（深度の 1/2）
        uint32_t hiZHeight_ = 0;  // mip0 の高さ
        uint32_t hiZMipCount_ = 0;
        uint32_t depthWidth_ = 0; // 元深度の解像度（リサイズ検出用）
        uint32_t depthHeight_ = 0;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxHiZMips> hiZMipSrvCpu_{};
        std::array<DescriptorHandle, kMaxHiZMips> hiZMipSrvGpu_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kMaxHiZMips> hiZMipUavCpu_{};
        std::array<DescriptorHandle, kMaxHiZMips> hiZMipUavGpu_{};
        D3D12_CPU_DESCRIPTOR_HANDLE hiZFullSrvCpu_{};
        DescriptorHandle hiZFullSrvGpu_{};

        // 構築 CS 定数（ミップ段ごとに 256B スロット。内容はリサイズ時のみ更新）
        Microsoft::WRL::ComPtr<ID3D12Resource> buildParamsBuffer_;
        uint8_t* buildParamsMapped_ = nullptr;

        // AABB アップロード（フレームリング、永続マップ）+ 判定 CS 定数
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameRing> boundsUpload_;
        std::array<BoundsGPU*, kFrameRing> boundsMapped_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kFrameRing> boundsSrvCpu_{};
        std::array<DescriptorHandle, kFrameRing> boundsSrvGpu_{};
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameRing> cullParamsUpload_;
        std::array<CullParamsGPU*, kFrameRing> cullParamsMapped_{};

        // 可視フラグ（Default ヒープ UAV）と Readback リング
        GpuResource visibilityBuffer_;
        D3D12_CPU_DESCRIPTOR_HANDLE visibilityUavCpu_{};
        DescriptorHandle visibilityUavGpu_{};
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kFrameRing> readback_;
        std::array<std::vector<uint32_t>, kFrameRing> ringIds_; // その回で判定した ID 列（Readback の並び）
        std::array<bool, kFrameRing> ringPending_{};
    };
}
