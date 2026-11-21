#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Graphics/RHI/Descriptor/DescriptorHandle.h"
#include "Graphics/RHI/Resource/GpuResource.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/Shader/ShaderBindingContract.h"
#include "Graphics/Water/Ripple/WaterRippleSimulation.h"
#include "Math/Vector/Vector4.h"

namespace CoreEngine
{
    class GraphicsCore;
    class DescriptorAllocator;
    class ShaderProgramCache;
    struct WaterFFTOceanInput;
    struct WaterObjectFoamBinding;

    /// @brief 物がかき回した所の泡を、物が起こした水の流れで運ぶ（カメラの前に置いた 1 段の格子）
    /// @details 1024×1024 マス・12.5 cm の格子に、流れ (ux, uz) と泡（水面の泡 F・水の中の気泡 B・模様を動かす速さ v）を持ち、
    ///          WaterObjectFoamAdvance.CS.hlsl で進める。流れは源が外へ押して物の速さへ引きずり、弱まりながらにじむ。
    ///          泡は流れと風下の流れで半ラグランジュに運んでにじませ、広がった所で薄くする。
    ///          格子の座標は FFT の水なら変位前の参照格子の座標、ほかの水なら世界の XZ。
    ///          格子は歩ごとにカメラの前へマス単位で寄せる（前の状態は世界の位置で読み直す）。
    ///          源が無いまま泡と流れが消えるまでの時間が過ぎたら止め、次の源で 0 から始め直す
    class WaterObjectFoam {
    public:
        /// @brief 岸と物に使う海の範囲マップ（波紋と同じもの）
        using ShoreMap = WaterRippleSimulation::ShoreMap;

        /// @brief 格子の一辺のマス数
        static constexpr uint32_t kResolution = 1024;
        /// @brief 1 マスの幅 [m]（一辺は 128 m）
        static constexpr float kCellSize = 0.125f;
        /// @brief 格子の中心をカメラの前へ進める量（格子の一辺に対する比）
        static constexpr float kForwardShare = 0.25f;
        /// @brief 1 フレームの源の数の上限
        static constexpr uint32_t kMaxSources = WaterRippleSimulation::kMaxFoamSourcesPerFrame;
        /// @brief 1 歩の時間の上限 [s]（これより長いフレームは 2 歩に割る）
        static constexpr float kMaxStepSeconds = 1.0f / 30.0f;
        /// @brief 1 フレームに進める歩数の上限
        static constexpr uint32_t kMaxStepsPerFrame = 2;
        /// @brief 模様を流れでずらす時刻を巻き戻す周期 [s]（WaterWhitecapCoverage.hlsli の kObjectFoamFlowPeriodSeconds の整数倍）
        static constexpr double kFlowTimePeriodSeconds = 60.0;

        struct Settings {
            bool enabled = true;
            float flowStrength = 1.0f;   ///< 物が起こす流れの倍率（0 = 運ばない）
            float decaySeconds = 3.0f;   ///< 水面の泡が e^-1 まで減る時間 [s]
            float bubbleSeconds = 0.6f;  ///< 水の中の気泡が浮き切るまでの時間 [s]
            float riseSeconds = 0.25f;   ///< 気泡が水面の泡になる時間 [s]（0 = 気泡と同時に泡を入れる）
            float windDriftVelocity[2] = { 0.0f, 0.0f }; ///< 泡が風下へ流れる速さ [m/s]
        };

        /// @brief 計算パイプラインとテクスチャを作る
        bool Initialize(
            GraphicsCore* dxCommon,
            DescriptorAllocator* descriptorAllocator,
            ShaderProgramCache* shaderProgramCache);

        bool IsInitialized() const { return isInitialized_; }

        /// @brief このフレームの源を受け取り、このフレームに進める歩数と格子の置き場所を決める（フレームに 1 回）
        /// @param sources 源ごとに 2 つ（WaterRippleSimulation::GetFoamSources）。kMaxSources 個まで
        /// @param cameraX, cameraZ カメラの XZ [m]
        /// @param viewX, viewZ カメラが見ている向きの水平の成分（長さ 1 以下）
        /// @details 止まっている格子は源があれば起こす。止まっている間と時刻が進まないフレームは歩を進めない
        void Advance(
            std::span<const Vector4> sources, float deltaSeconds,
            float cameraX, float cameraZ, float viewX, float viewZ, const Settings& settings);

        /// @brief 陸と物に使う海の範囲マップを決める（std::nullopt なら陸を扱わない。次の Dispatch から効く）
        void SetShoreMap(const std::optional<ShoreMap>& shoreMap) { shoreMap_ = shoreMap; }

        /// @brief Advance で決めた歩だけ、源を入れて泡と流れを進める（水面を描くパスより前に呼ぶ）
        /// @param frameNumber 同じフレームで 2 回進めないための番号
        /// @param fftOcean    主の水の FFT の海（enabled なら源を波の水平の変位で動かした位置で比べる。nullptr 可）
        void Dispatch(ID3D12GraphicsCommandList* cmdList, uint64_t frameNumber, const WaterFFTOceanInput* fftOcean);

        /// @brief このフレームの Dispatch の後の泡と流れの SRV と格子の範囲を書く（止まっている間は enabled = 0）
        void FillBinding(WaterObjectFoamBinding& out) const;

        /// @brief 泡と流れを進めているか
        bool IsActive() const { return active_; }

        /// @brief 止める（次の源で 0 から始め直す）
        void Stop()
        {
            active_ = false;
            plannedSteps_ = 0;
            plannedClear_ = false;
            sources_.clear();
        }

    private:
        bool CreatePipeline();
        bool CreateTextures();
        void Step(ID3D12GraphicsCommandList* cmdList, float stepSeconds, bool clear, const WaterFFTOceanInput* fftOcean);
        /// @brief 源を足すマスの範囲（xy = 最小・zw = 最大。最大は含まない。源が無ければ空）
        /// @param margin 源の範囲に足す余裕 [m]（波の水平の変位の分）
        std::array<int32_t, 4> ComputeSourceRect(float margin) const;
        /// @brief 源が無くなってから止めるまでの時間 [s]（泡と気泡が消えるまで）
        float ComputeIdleLimit() const;

        GraphicsCore* dxCommon_ = nullptr;
        DescriptorAllocator* descriptorAllocator_ = nullptr;
        ShaderProgramCache* shaderProgramCache_ = nullptr;
        bool isInitialized_ = false;

        RootSignatureManager rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
        BindingTable bindings_;

        /// @brief 泡 (F, B, vx, vz)（RGBA16F）と流れ (ux, uz)（RG16F）を 2 組ずつ。current_ が直近に書いた組
        std::array<GpuResource, 2> foam_{};
        std::array<DescriptorHandle, 2> foamSrv_{};
        std::array<DescriptorHandle, 2> foamUav_{};
        std::array<GpuResource, 2> flow_{};
        std::array<DescriptorHandle, 2> flowSrv_{};
        std::array<DescriptorHandle, 2> flowUav_{};
        uint32_t current_ = 0;

        std::vector<Vector4> sources_;
        std::optional<ShoreMap> shoreMap_;
        Settings settings_{};
        int32_t originCell_[2] = { 0, 0 };       ///< 直近に書いた状態の最小の角 [マス]
        int32_t targetOriginCell_[2] = { 0, 0 }; ///< 次の歩の後の最小の角 [マス]
        double driftOffset_[2] = { 0.0, 0.0 };   ///< 直近に書いた状態までに泡が風下へ流れた距離 [m]
        double flowTime_ = 0.0;                  ///< 直近に書いた状態までの時刻 [s]（kFlowTimePeriodSeconds で巻き戻す）
        float idleSeconds_ = 0.0f;               ///< 最後に源を入れてからの時間 [s]
        uint32_t plannedSteps_ = 0;              ///< 次の Dispatch で進める歩数
        float plannedStepSeconds_ = 0.0f;        ///< 次の Dispatch の 1 歩の時間 [s]
        bool plannedClear_ = false;              ///< 次の Dispatch の最初の歩で前の状態を捨てる
        bool active_ = false;
        bool hasWritten_ = false;                ///< 起こしてから一度でも書いたか
        uint64_t lastFrameNumber_ = UINT64_MAX;
    };
}
