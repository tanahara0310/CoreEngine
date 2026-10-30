#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <future>
#include <vector>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/Water/WaterSurfaceData.h"
#include "WaterRayTracingPassBase.h"
#include "WaterSeaMapBuilder.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class AccelerationStructureManager;

    /// @brief 海の範囲マップの今の地図
    struct WaterSeaMapView {
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandle{}; ///< 地図の SRV
        uint32_t srvHeapIndex = 0;               ///< 地図の SRV のシェーダー可視ヒープの中の番号
        uint32_t resolution[2] = { 0, 0 };       ///< X・Z のテクセル数
        float originXZ[2] = { 0.0f, 0.0f };      ///< 範囲の XZ の最小の角 [m]
        float texelsPerMeter = 0.0f;             ///< 1 m あたりのテクセル数
        float waterHeight = 0.0f;                ///< 測ったときの水面の高さ [m]
        bool valid = false;
    };

    /// @brief 水面の範囲全体の水深・上の物までの高さ・海とのつながりをレイトレーシングで測り、
    ///        波の変位を弱める地図（海の範囲マップ）を作るマネージャ
    /// @details 測るのは止まっている形の決まった物（RayTracingInstanceMask::kSolid）だけ。
    ///          測る物・水面の範囲・水面の高さ・細かさが変わって kStableFrames 続いたら測り直す。
    ///          測った値は読み戻して別スレッドで地図にし、できたら GPU へ送って差し替える
    class WaterSeaMapRayTracingManager : public WaterRayTracingPassBase {
    public:
        WaterSeaMapRayTracingManager() = default;
        ~WaterSeaMapRayTracingManager();

        WaterSeaMapRayTracingManager(const WaterSeaMapRayTracingManager&) = delete;
        WaterSeaMapRayTracingManager& operator=(const WaterSeaMapRayTracingManager&) = delete;

        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            AccelerationStructureManager* asMgr,
            ShaderProgramCache* shaderProgramCache);

        /// @brief 測り直しの判断・測定・読み戻し・できた地図の転送を進める（フレームに 1 回）
        /// @param cmdList 今フレームの記録先
        /// @param surfaceData 今フレームの水面（水面が無ければ nullptr。地図を捨てる）
        /// @param staticSceneSignature 止まっている形の決まった物の並びを表す値（AccelerationStructureManager::GetStaticSolidSignature）
        void Update(
            ID3D12GraphicsCommandList* cmdList,
            const WaterSurfaceData* surfaceData,
            uint64_t staticSceneSignature);

        /// @brief 今の地図（まだ無ければ valid = false）
        const WaterSeaMapView& GetView() const { return view_; }

    private:
        /// @brief 測った条件（どれかが変わったら測り直す）
        struct BakeKey {
            float originXZ[2] = { 0.0f, 0.0f };
            float texelSize = 0.0f;
            uint32_t width = 0;
            uint32_t height = 0;
            float waterHeight = 0.0f;
            uint32_t conservativeRadius = 0;
            uint64_t sceneSignature = 0;

            bool operator==(const BakeKey&) const = default;
        };

        /// @brief 別スレッドで作った地図
        struct BuildResult {
            std::vector<uint16_t> texels;
            BakeKey key{};
            WaterSeaMapBuilder::Stats stats{};
            uint64_t generation = 0;
            double milliseconds = 0.0;
        };

        /// @brief 測っている途中の段階
        enum class Stage {
            Idle,            ///< 何もしていない
            WaitingReadback, ///< 測った値の読み戻しを待っている
            Building,        ///< 別スレッドで地図を作っている
        };

        /// @brief 粗い格子の三角形が水平変位で動く分として、窓の半径に足す幅 [m]
        static constexpr float kHorizontalMargin = 2.0f;
        /// @brief 陸の上の面を探す上向きのレイの長さ [m]
        static constexpr float kUpProbeDistance = 2000.0f;
        /// @brief 測る条件がこのフレーム数変わらなかったら測り直す
        static constexpr uint64_t kStableFrames = 30;
        /// @brief 測った値の出力を置く RT 出力の番号
        static constexpr uint32_t kOutputSlot = 0;

        BakeKey MakeKey(const WaterSurfaceData& surfaceData, uint64_t staticSceneSignature) const;
        /// @brief 測って、読み戻しを積む
        void Dispatch(ID3D12GraphicsCommandList* cmdList, const WaterSurfaceData& surfaceData, const BakeKey& key);
        /// @brief 読み戻した値から、別スレッドで地図を作り始める
        void StartBuild();
        /// @brief できた地図を GPU へ送り、今の地図と差し替える
        void Upload(ID3D12GraphicsCommandList* cmdList, BuildResult&& result);
        /// @brief 今の地図と、読み戻し待ちの測定を捨てる（作っている途中の地図は届いても使わない）
        void Discard();

        WaterSeaMapView view_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> mapTexture_;
        DescriptorHandle mapSrv_{};
        BakeKey mapKey_{};

        BakeKey candidateKey_{};
        bool hasCandidate_ = false;
        uint64_t candidateSinceFrame_ = 0;

        Stage stage_ = Stage::Idle;
        BakeKey pendingKey_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT readbackLayout_{};
        uint64_t readbackFrame_ = 0;
        /// @brief Discard のたびに進む番号（古い測定から作った地図を見分ける）
        uint64_t generation_ = 0;
        std::future<BuildResult> buildTask_;
    };
}
