#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class EngineSystem;
    class ShaderProgramCache;
    struct WaterRippleBinding;

    /// @brief 水の波紋をカメラの前に置いた 3 段の格子で進める
    /// @details 段ごとに 512×512 マスの高さの場を持ち、深い水の重力波の分散（ω² = g|k|）で進める
    ///          （WaterRipplePropagate.CS.hlsl）。源はくぼみで、段ごとに受け持つ波長の帯へ分けて入れる
    ///          （WaterRippleInject.CS.hlsl）。水面は全部の段の傾きを足して法線へ加える。
    ///          場は段ごとに 2 つあり、全部の波紋の場と、物が起こした波紋（AddObjectRipple）だけの場を同じ式で進める。
    ///          格子は段の 1 歩ごとにカメラの真下へマス単位で寄せる。
    ///          海の範囲マップ（SetShoreMap）があれば、陸と水の中の物の下のマスを壁にし、浅瀬で波を弱める。
    ///          しばらく源が無い段は止め、次の源で 0 から始め直す。
    class WaterRippleSimulation {
    public:
        /// @brief 波紋の岸と物に使う海の範囲マップ（WaterSeaMap.hlsli）
        struct ShoreMap {
            D3D12_GPU_DESCRIPTOR_HANDLE srv{};  ///< 地図の SRV
            uint32_t resolution[2] = { 0, 0 };  ///< X・Z のテクセル数
            float originXZ[2] = { 0.0f, 0.0f }; ///< 地図の範囲の XZ の最小の角 [m]
            float texelsPerMeter = 0.0f;        ///< 1 m あたりのテクセル数
            float depthOffset = 0.0f;           ///< 今の水面の高さ − 測ったときの水面の高さ [m]
        };

        static constexpr uint32_t kLevelCount = 3;
        /// @brief 段ごとの場の数（0 = 全部の波紋・1 = 物が起こした波紋）
        static constexpr uint32_t kFieldCount = 2;
        static constexpr uint32_t kResolution = 512;
        static constexpr uint32_t kMaxSourcesPerFrame = 256;
        /// @brief 1 フレームに足せる泡の源の数
        static constexpr uint32_t kMaxFoamSourcesPerFrame = 64;
        static constexpr uint32_t kMaxStepsPerFrame = 4;
        /// @brief 段ごとの 1 マスの幅 [m]（一辺は 32 m・128 m・512 m）
        static constexpr float kCellSizes[kLevelCount] = { 0.0625f, 0.25f, 1.0f };
        /// 格子の中心をカメラの前へ進める量（格子の一辺に対する比）
        static constexpr float kForwardShare = 0.25f;
        /// @brief 段ごとの 1 歩の時間 [s]
        static constexpr float kStepSeconds[kLevelCount] = { 1.0f / 60.0f, 1.0f / 60.0f, 1.0f / 30.0f };

        struct Settings {
            bool enabled = true;
            float dampingPerSecond = 0.4f; ///< 振幅が e 分の 1 になるまでの時間の逆数 [1/s]
        };

        /// @brief 計算パイプラインとテクスチャを作る
        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            ShaderProgramCache* shaderProgramCache);

        bool IsInitialized() const { return isInitialized_; }

        /// @brief 波紋の源を足す（水面を半径 radius・深さ depth のくぼみで押し下げて放す）
        /// @param position くぼみの中心（Y は使わない）
        /// @param radius くぼみの半径 [m]（ガウスの σ の 2 倍）
        /// @param depth くぼみの深さ [m]（負なら盛り上げて放す）
        /// @details 次の Dispatch で格子へ入れる。1 フレームに kMaxSourcesPerFrame 個まで
        void AddRipple(const Vector3& position, float radius, float depth);

        /// @brief 物が起こした波紋の源を足す（形は AddRipple と同じ。物が起こした波紋の場へも入れる）
        void AddObjectRipple(const Vector3& position, float radius, float depth);

        /// @brief 物が水をかき回した所の泡の源を足す（次の Advance で、そのフレームの泡の源になる）
        /// @param position    中心（Y は使わない）
        /// @param radius      半径 [m]（泡を広げるガウスの σ）
        /// @param churnMeters このフレームに水面を上下させた量 [m]
        void AddObjectFoam(const Vector3& position, float radius, float churnMeters);

        /// @brief 前の Advance で決めた泡の源（x, z, σ [m], 水面を上下させた速さ [m/s]）
        std::span<const Vector4> GetFoamSources() const { return foamSources_; }

        /// @brief このフレームにまだ足せる源の数（AddRipple と AddObjectRipple で共通）
        uint32_t GetFreeSourceCount() const
        {
            return kMaxSourcesPerFrame - (std::min)(static_cast<uint32_t>(pendingSources_.size()), kMaxSourcesPerFrame);
        }

        /// @brief 時刻を進め、段ごとにこのフレームで進める歩数と格子の置き場所を決める（フレームに 1 回）
        /// @param cameraX, cameraZ カメラの XZ [m]
        /// @param viewX, viewZ カメラが見ている向きの水平の成分（長さ 1 以下。真下を見ると 0）
        /// @details 格子の中心は、カメラから見ている向きへ格子の一辺の kForwardShare 倍だけ進めた所に置く。
        ///          足された泡の源を deltaSeconds で割った速さにして、GetFoamSources の中身と入れ替える（波紋を切っていても）
        void Advance(float deltaSeconds, float cameraX, float cameraZ, float viewX, float viewZ, const Settings& settings);

        /// @brief 岸と物に使う海の範囲マップを決める（std::nullopt なら岸と物を扱わない。次の Dispatch から効く）
        void SetShoreMap(const std::optional<ShoreMap>& shoreMap) { shoreMap_ = shoreMap; }

        /// @brief 源を入れて波を進める（Advance の後、水面を描くパスより前に呼ぶ）
        /// @param frameNumber 同じフレームで 2 回進めないための番号
        void Dispatch(ID3D12GraphicsCommandList* cmdList, uint64_t frameNumber);

        /// @brief このフレームの歩の後の格子の範囲と面の SRV の番号を書く（strength には触れない）
        void FillBinding(WaterRippleBinding& out) const;

        /// @brief 水面が読む面（段ごとの高さと傾きと、物が起こした波紋の高さ）の SRV
        D3D12_GPU_DESCRIPTOR_HANDLE GetSurfaceSrvHandle() const { return surfaceSrv_.gpuHandle; }

        /// @brief 波紋を進めている段があるか
        bool HasActiveLevel() const;

    private:
        struct Source {
            float x = 0.0f;
            float z = 0.0f;
            float sigma = 0.0f; ///< ガウスの幅 [m]
            float depth = 0.0f; ///< くぼみの深さ [m]
            bool fromObject = false; ///< 物が起こした波紋の場へも入れる
        };

        struct FoamSource {
            float x = 0.0f;
            float z = 0.0f;
            float sigma = 0.0f;       ///< ガウスの幅 [m]
            float churnMeters = 0.0f; ///< このフレームに水面を上下させた量 [m]
        };

        struct Level {
            int32_t originCell[2] = { 0, 0 };       ///< GPU の状態の最小の角 [マス]
            int32_t targetOriginCell[2] = { 0, 0 }; ///< このフレームの歩の後の最小の角 [マス]
            float accumulatedSeconds = 0.0f;        ///< まだ歩にしていない時間 [s]
            float idleSeconds = 0.0f;               ///< 最後に源を入れてからの時間 [s]
            uint32_t pendingSteps = 0;              ///< 次の Dispatch で進める歩数
            uint32_t readPair = 0;                  ///< 今の状態の組
            bool active = false;
            bool clearPending = false;              ///< 次の Dispatch で状態を 0 にする
        };

        bool CreatePipelines();
        bool CreateTextures();
        void AddSource(const Vector3& position, float radius, float depth, bool fromObject);
        void InjectSources(ID3D12GraphicsCommandList* cmdList, uint32_t levelIndex);
        void Step(ID3D12GraphicsCommandList* cmdList, uint32_t levelIndex, const int32_t shiftCells[2], bool clear);

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        bool isInitialized_ = false;

        RootSignatureManager injectRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> injectPipeline_;
        BindingTable injectBindings_;
        RootSignatureManager propagateRootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> propagatePipeline_;
        BindingTable propagateBindings_;

        /// @brief 段と場ごとに 2 組の (h_n, h_{n-1})（R32_FLOAT・24 枚）
        GpuResource state_{};
        DescriptorHandle stateUav_{};
        /// @brief 段ごとの (全部の波紋の高さ, ∂h/∂x, ∂h/∂z, 物が起こした波紋の高さ)（RGBA16F・3 枚）
        GpuResource surface_{};
        DescriptorHandle surfaceSrv_{};
        DescriptorHandle surfaceUav_{};

        std::vector<Source> pendingSources_;
        std::vector<FoamSource> pendingFoamSources_;
        std::vector<Vector4> foamSources_;
        std::array<Level, kLevelCount> levels_{};
        std::optional<ShoreMap> shoreMap_;
        float dampingPerSecond_ = 0.4f;
        uint64_t lastFrameNumber_ = UINT64_MAX;
    };

    /// @brief エンジンの水の波紋を引く（水面のあるシーンを開くまでは nullptr）
    WaterRippleSimulation* FindWaterRippleSimulation(EngineSystem* engine);
}
