#include "pch.h"
#include "FFTOceanManagerLogHelper.h"

#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void FFTOceanManagerLogHelper::LogInitialize(uint32_t resolution, float patchLength, float windSpeed)
    {
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: initialized. resolution={} patchLength={:.2f} windSpeed={:.2f}",
            resolution,
            patchLength,
            windSpeed);
    }

    void FFTOceanManagerLogHelper::LogSettingsUpdated(
        float patchLength,
        float amplitudeScale,
        float windDirX,
        float windDirY,
        float windSpeed,
        float choppiness,
        uint32_t activeComponentCount,
        float gravity)
    {
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: settings updated. patchLength={:.2f} amplitudeScale={:.3f} windDir=({:.3f}, {:.3f}) windSpeed={:.3f} choppiness={:.3f} components={} gravity={:.3f}",
            patchLength,
            amplitudeScale,
            windDirX,
            windDirY,
            windSpeed,
            choppiness,
            activeComponentCount,
            gravity);
    }

    void FFTOceanManagerLogHelper::LogDispatchSummary(
        float timeSeconds,
        float deltaSeconds,
        float cbTimeSeconds,
        float amplitudeScale,
        float windSpeed,
        float choppiness,
        uint32_t activeComponentCount)
    {
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: dispatch time={:.3f} delta={:.3f} cb.time={:.3f} amp={:.3f} windSpeed={:.3f} choppiness={:.3f} components={}",
            timeSeconds,
            deltaSeconds,
            cbTimeSeconds,
            amplitudeScale,
            windSpeed,
            choppiness,
            activeComponentCount);
    }

    void FFTOceanManagerLogHelper::LogProbeSpectrum(
        uint32_t probeIndex,
        float waveVectorX,
        float waveVectorY,
        float angularFrequency,
        float real,
        float imag,
        float magnitude,
        float directionalWeight)
    {
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: probeSpectrum index={} k=({:.4f}, {:.4f}) omega={:.4f} height=({:.6f}, {:.6f}) |h|={:.6f} dirWeight={:.4f}",
            probeIndex,
            waveVectorX,
            waveVectorY,
            angularFrequency,
            real,
            imag,
            magnitude,
            directionalWeight);
    }

    void FFTOceanManagerLogHelper::LogIFFTCompleted(
        uint32_t finalSpectrumAIndex,
        uint32_t finalSpectrumBIndex,
        uint32_t resolution,
        uint32_t log2Resolution)
    {
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: IFFT completed. finalSpectrumAIndex={} finalSpectrumBIndex={} resolution={} log2Resolution={}",
            finalSpectrumAIndex,
            finalSpectrumBIndex,
            resolution,
            log2Resolution);
    }

    void FFTOceanManagerLogHelper::LogSpectrumUpload(uint64_t uploadBytes)
    {
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: spectrum buffer updated on upload heap. bytes={}",
            uploadBytes);
    }
}
