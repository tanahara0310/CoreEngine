#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Graphics/RHI/Command/FrameSync.h"
#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Graphics/Water/Query/WaterQuery.h"
#include "Graphics/Water/RayTracing/WaterRayTracingPassBase.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class ShaderProgramCache;
    struct WaterSurfaceData;

    /// @brief WaterQuery の点の水面の高さを GPU で評価し、読み戻す
    /// @details 毎フレーム、点を定数バッファへ積んで水面ごとに WaterHeightQuery.CS.hlsl で評価し、
    ///          フレームの枠ごとの読み戻し用バッファの、水面ごとの場所へ写す。
    ///          GPU が済んだ枠から順に WaterQuery へ結果を返す。
    class WaterHeightQueryManager {
    public:
        /// @brief 傾きを差分で求める間隔 [m]
        static constexpr float kSlopeStepMeters = 0.5f;

        /// @brief 計算パイプラインと GPU 資源を作る
        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            ShaderProgramCache* shaderProgramCache,
            WaterQuery* query);

        bool IsInitialized() const { return isInitialized_; }

        /// @brief GPU が済んだ枠の結果を WaterQuery へ返す（何度呼んでもよい）
        void CollectResults();

        /// @brief 今の点を、範囲のある水面ごとに、範囲の狭い水面から順に評価する（FFT の波面を計算した後に呼ぶ）
        /// @param primarySurface 主の水の状態（水面が無ければ nullptr）
        /// @param otherSurfaces ほかの水の水面の状態
        /// @param fallbackDisplacementSRV FFT の海で描いていない水面の、変位テクスチャのスロットに差す SRV
        void Dispatch(
            ID3D12GraphicsCommandList* cmdList,
            const WaterSurfaceData* primarySurface,
            std::span<const WaterSurfaceData> otherSurfaces,
            D3D12_GPU_DESCRIPTOR_HANDLE fallbackDisplacementSRV);

    private:
        /// @brief 計算シェーダーの定数（WaterHeightQuery.CS.hlsl の WaterHeightQueryConstants）
        struct QueryConstants {
            uint32_t pointCount = 0;
            float slopeStep = kSlopeStepMeters;
            Vector2 padding{};
            Vector4 points[WaterQuery::kMaxPoints / 2]{}; ///< 点 2 つずつのワールド XZ (x0, z0, x1, z1)
        };

        static constexpr Cb::Field kQueryConstantsFields[] = {
            CB_FIELD(QueryConstants, pointCount), CB_FIELD(QueryConstants, slopeStep),
            CB_FIELD(QueryConstants, padding), CB_FIELD(QueryConstants, points),
        };
        CB_VERIFY_LAYOUT(QueryConstants, kQueryConstantsFields);
        CB_BIND_HLSL(QueryConstants, kQueryConstantsFields, "WaterHeightQueryConstants");

        /// @brief フレームの枠 1 つ分の、読み戻し待ちの記録
        struct PendingFrame {
            bool pending = false;
            /// @brief 記録したときの枠のフェンス値（この値から変われば、そのフレームを GPU へ投げた）
            uint64_t fenceBeforeSubmit = 0;
            std::vector<WaterQuery::Submitted> submitted;
            /// @brief 評価した水面の数（読み戻しには submitted.size() 個ずつ並ぶ）
            uint32_t surfaceCount = 0;
        };

        bool CreatePipeline();
        bool CreateBuffers();

        /// @brief 枠 frameIndex の記録が GPU で済んでいれば WaterQuery へ返す
        void CollectFrame(uint32_t frameIndex);

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        WaterQuery* query_ = nullptr;
        bool isInitialized_ = false;

        RootSignatureManager rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        BindingTable bindings_;

        /// @brief 計算シェーダーの出力（点ごとに WaterQuery::GpuResult）
        GpuResource results_{};
        DescriptorHandle resultsUav_{};
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kMaxFramesInFlight> readback_{};
        std::array<PendingFrame, kMaxFramesInFlight> pendingFrames_{};
        /// @brief 読み戻した結果の写し
        std::vector<WaterQuery::GpuResult> resultScratch_;
        /// @brief このフレームに評価する水面（毎フレーム使い回す）
        std::vector<const WaterSurfaceData*> surfaceScratch_;
        /// @brief 水面の数が上限を超えたことを知らせたか
        bool warnedSurfaceLimit_ = false;
    };
}
