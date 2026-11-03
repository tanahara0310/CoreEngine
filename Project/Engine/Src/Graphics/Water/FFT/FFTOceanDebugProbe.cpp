#include "pch.h"
#include "FFTOceanDebugProbe.h"

#include <algorithm>
#include <cmath>

#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/Water/FFT/FFTOceanReadbackHelper.h"
#include "Utility/CVar/CVar.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        // デバッグ計測のオプトイン。有効にすると 120 フレームごとに
        // サマリログ・スペクトルCPUプローブ・GPUリードバック統計が出る。
        // リードバックはフル解像度コピーを伴うため計測時以外は必ず off にする。
        static CVar<bool> cvFFTOceanDebugProbe{
            "d.FFTOcean.DebugProbe", false,
            "FFT海面のデバッグ計測（120F毎のサマリ/リードバック統計）" };

        constexpr uint32_t kLogIntervalFrames = 120;

        /// @brief 複素数（デバッグ表示用）
        struct ComplexValue {
            float real = 0.0f;
            float imag = 0.0f;
        };

        /// @brief H0/H0* と角周波数から時刻 t のスペクトル値を評価する
        ComplexValue EvaluateSpectrumSample(
            const float* h0,
            const float* h0Minus,
            float angularFrequency,
            float directionalWeight,
            float timeSeconds,
            float amplitudeScale,
            uint32_t activeComponentCount)
        {
            ComplexValue value{};
            if (!h0 || !h0Minus) {
                return value;
            }

            const float angularPhase = angularFrequency * timeSeconds;
            const float cosPhase = std::cos(angularPhase);
            const float sinPhase = std::sin(angularPhase);

            // h0Minus 側は共役回転 e^{-iωt} を掛ける: 実部 = x·cos + y·sin
            // （FFTOceanTimeEvolution.CS.hlsl の negativeRotation と一致させること）
            value.real =
                (h0[0] * cosPhase - h0[1] * sinPhase)
                + (h0Minus[0] * cosPhase + h0Minus[1] * sinPhase);
            value.imag =
                (h0[0] * sinPhase + h0[1] * cosPhase)
                + (-h0Minus[0] * sinPhase + h0Minus[1] * cosPhase);

            const float bandLimit = (std::max)(static_cast<float>(activeComponentCount) / 64.0f, 1.0f / 64.0f);
            const float bandFade = (std::clamp)((bandLimit - directionalWeight) * 16.0f + 1.0f, 0.0f, 1.0f);
            value.real *= bandFade * amplitudeScale;
            value.imag *= bandFade * amplitudeScale;
            return value;
        }
    }

    bool FFTOceanDebugProbe::IsEnabled()
    {
        return cvFFTOceanDebugProbe.Get();
    }

    bool FFTOceanDebugProbe::EnsureTarget(
        ReadbackTarget& target, ID3D12Resource* sourceTexture, const char* debugName)
    {
        if (target.buffer) {
            return true;
        }
        if (!sourceTexture) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(sourceTexture->GetDevice(IID_PPV_ARGS(&device)))) {
            return false;
        }

        const FFTOceanReadbackTextureCreateRequest request{
            device.Get(),
            sourceTexture,
            debugName,
            &target.buffer,
            &target.layout,
            &target.totalBytes };
        return FFTOceanReadbackHelper::CreateTextureReadbackBuffer(request);
    }

    void FFTOceanDebugProbe::FlushPendingLogs(uint32_t resolution)
    {
        // pending はプローブ有効時にしか立たないため、ここでは CVar を見ずに
        // 掃き出しだけ行う（有効→無効の切り替え直後でも積んだ分は回収する）。
        if (ifftPending_ && ifftA_.buffer && ifftB_.buffer) {
            const FFTOceanReadbackHelper::SpectrumReadbackRequest request{
                ifftA_.buffer.Get(), ifftB_.buffer.Get(),
                ifftA_.totalBytes, ifftB_.totalBytes,
                ifftA_.layout, ifftB_.layout,
                resolution, ifftSequence_, true };
            if (FFTOceanReadbackHelper::TryLogSpectrumReadback(request)) {
                ifftPending_ = false;
            }
        }

        if (evolutionPending_ && evolutionA_.buffer && evolutionB_.buffer) {
            const FFTOceanReadbackHelper::SpectrumReadbackRequest request{
                evolutionA_.buffer.Get(), evolutionB_.buffer.Get(),
                evolutionA_.totalBytes, evolutionB_.totalBytes,
                evolutionA_.layout, evolutionB_.layout,
                resolution, evolutionSequence_, false };
            if (FFTOceanReadbackHelper::TryLogSpectrumReadback(request)) {
                evolutionPending_ = false;
            }
        }

        if (surfacePending_ && surfaceDisplacement_.buffer && surfaceNormal_.buffer) {
            const FFTOceanReadbackHelper::SurfaceReadbackRequest request{
                surfaceDisplacement_.buffer.Get(), surfaceNormal_.buffer.Get(),
                surfaceDisplacement_.totalBytes, surfaceNormal_.totalBytes,
                surfaceDisplacement_.layout, surfaceNormal_.layout,
                resolution, surfaceSequence_ };
            if (FFTOceanReadbackHelper::TryLogSurfaceReadback(request)) {
                surfacePending_ = false;
            }
        }
    }

    void FFTOceanDebugProbe::LogDispatchSummary(
        float timeSeconds,
        float amplitudeScale,
        float windSpeed,
        float choppiness,
        uint32_t activeComponentCount,
        const SpectrumSample* samples,
        uint32_t resolution)
    {
        if (!IsEnabled()) {
            return;
        }
        if ((summaryCounter_++ % kLogIntervalFrames) != 0) {
            return;
        }

        const float loggedDelta = timeSeconds - previousLoggedTime_;
        previousLoggedTime_ = timeSeconds;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: dispatch time={:.3f} delta={:.3f} cb.time={:.3f} amp={:.3f} windSpeed={:.3f} choppiness={:.3f} components={}",
            timeSeconds,
            loggedDelta,
            timeSeconds,
            amplitudeScale,
            windSpeed,
            choppiness,
            activeComponentCount);

        if (samples && resolution > 0) {
            // 中央近傍の 1 サンプルを CPU 参照実装で評価して GPU 結果の妥当性を照合する。
            // samples は UPLOAD ヒープ（write-combined）なので、この読みはプローブ有効時のみ。
            const uint32_t probeX = resolution / 2 + 1;
            const uint32_t probeY = resolution / 2;
            const uint32_t probeIndex = probeY * resolution + probeX;
            const SpectrumSample& probeSample = samples[probeIndex];
            const ComplexValue probeHeight =
                EvaluateSpectrumSample(
                    probeSample.h0,
                    probeSample.h0Minus,
                    probeSample.angularFrequency,
                    probeSample.directionalWeight,
                    timeSeconds,
                    amplitudeScale,
                    activeComponentCount);

            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: probeSpectrum index={} k=({:.4f}, {:.4f}) omega={:.4f} height=({:.6f}, {:.6f}) |h|={:.6f} dirWeight={:.4f}",
                probeIndex,
                probeSample.waveVector[0],
                probeSample.waveVector[1],
                probeSample.angularFrequency,
                probeHeight.real,
                probeHeight.imag,
                std::sqrt(probeHeight.real * probeHeight.real + probeHeight.imag * probeHeight.imag),
                probeSample.directionalWeight);
        }
    }

    void FFTOceanDebugProbe::ScheduleEvolutionReadback(
        ID3D12GraphicsCommandList* cmdList,
        GpuResource& spectrumA,
        GpuResource& spectrumB)
    {
        // スペクトル（時間発展後）のリードバックを積む。毎フレームは重いので間引く
        if (!IsEnabled() || !cmdList || !spectrumA.IsValid() || !spectrumB.IsValid()) {
            return;
        }
        if ((evolutionCounter_++ % kLogIntervalFrames) != 0) {
            return;
        }
        if (!EnsureTarget(evolutionA_, spectrumA.Get(), "evolutionA")
            || !EnsureTarget(evolutionB_, spectrumB.Get(), "evolutionB")) {
            return;
        }

        D3D12_TEXTURE_COPY_LOCATION srcA{};
        srcA.pResource = spectrumA.Get();
        srcA.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcA.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dstA{};
        dstA.pResource = evolutionA_.buffer.Get();
        dstA.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstA.PlacedFootprint = evolutionA_.layout;

        D3D12_TEXTURE_COPY_LOCATION srcB{};
        srcB.pResource = spectrumB.Get();
        srcB.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcB.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dstB{};
        dstB.pResource = evolutionB_.buffer.Get();
        dstB.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstB.PlacedFootprint = evolutionB_.layout;

        Barrier::Transition(cmdList, spectrumA, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, spectrumB, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dstA, 0, 0, 0, &srcA, nullptr);
        cmdList->CopyTextureRegion(&dstB, 0, 0, 0, &srcB, nullptr);
        Barrier::Transition(cmdList, spectrumA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, spectrumB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        evolutionPending_ = true;
        ++evolutionSequence_;
    }

    void FFTOceanDebugProbe::OnIFFTCompleted(
        ID3D12GraphicsCommandList* cmdList,
        uint32_t finalSpectrumAIndex,
        uint32_t finalSpectrumBIndex,
        uint32_t resolution,
        uint32_t log2Resolution,
        GpuResource& spectrumA,
        GpuResource& spectrumB)
    {
        // 毎フレーム全解像度をコピーすると重いので、kLogIntervalFrames ごとに 1 回だけ拾う
        if (!IsEnabled() || !cmdList || !spectrumA.IsValid() || !spectrumB.IsValid()) {
            return;
        }
        if ((ifftCounter_++ % kLogIntervalFrames) != 0) {
            return;
        }

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: IFFT completed. finalSpectrumAIndex={} finalSpectrumBIndex={} resolution={} log2Resolution={}",
            finalSpectrumAIndex,
            finalSpectrumBIndex,
            resolution,
            log2Resolution);

        if (!EnsureTarget(ifftA_, spectrumA.Get(), "ifftA")
            || !EnsureTarget(ifftB_, spectrumB.Get(), "ifftB")) {
            return;
        }

        D3D12_TEXTURE_COPY_LOCATION srcA{};
        srcA.pResource = spectrumA.Get();
        srcA.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcA.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dstA{};
        dstA.pResource = ifftA_.buffer.Get();
        dstA.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstA.PlacedFootprint = ifftA_.layout;

        D3D12_TEXTURE_COPY_LOCATION srcB{};
        srcB.pResource = spectrumB.Get();
        srcB.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcB.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dstB{};
        dstB.pResource = ifftB_.buffer.Get();
        dstB.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstB.PlacedFootprint = ifftB_.layout;

        Barrier::Transition(cmdList, spectrumA, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, spectrumB, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dstA, 0, 0, 0, &srcA, nullptr);
        cmdList->CopyTextureRegion(&dstB, 0, 0, 0, &srcB, nullptr);
        Barrier::Transition(cmdList, spectrumA, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, spectrumB, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ifftPending_ = true;
        ++ifftSequence_;
    }

    void FFTOceanDebugProbe::ScheduleSurfaceReadback(
        ID3D12GraphicsCommandList* cmdList,
        GpuResource& displacement,
        GpuResource& normal)
    {
        // 波面（変位・法線）のリードバックを積む。毎フレームは重いので間引く
        if (!IsEnabled() || !cmdList || !displacement.IsValid() || !normal.IsValid()) {
            return;
        }
        if ((surfaceCounter_++ % kLogIntervalFrames) != 0) {
            return;
        }
        if (!EnsureTarget(surfaceDisplacement_, displacement.Get(), "displacement")
            || !EnsureTarget(surfaceNormal_, normal.Get(), "normal")) {
            return;
        }

        D3D12_TEXTURE_COPY_LOCATION displacementSrc{};
        displacementSrc.pResource = displacement.Get();
        displacementSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        displacementSrc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION displacementDst{};
        displacementDst.pResource = surfaceDisplacement_.buffer.Get();
        displacementDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        displacementDst.PlacedFootprint = surfaceDisplacement_.layout;

        D3D12_TEXTURE_COPY_LOCATION normalSrc{};
        normalSrc.pResource = normal.Get();
        normalSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        normalSrc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION normalDst{};
        normalDst.pResource = surfaceNormal_.buffer.Get();
        normalDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        normalDst.PlacedFootprint = surfaceNormal_.layout;

        Barrier::Transition(cmdList, displacement, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, normal, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&displacementDst, 0, 0, 0, &displacementSrc, nullptr);
        cmdList->CopyTextureRegion(&normalDst, 0, 0, 0, &normalSrc, nullptr);
        Barrier::Transition(cmdList, displacement, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, normal,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        surfacePending_ = true;
        ++surfaceSequence_;
    }
}
