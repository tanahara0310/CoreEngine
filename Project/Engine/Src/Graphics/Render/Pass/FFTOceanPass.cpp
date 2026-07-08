#include "pch.h"
#include "FFTOceanPass.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    void FFTOceanPass::Execute(const RenderContext& context)
    {
        if (!context.dxCommon || !context.fftOceanManager) {
            return;
        }

        // 波面は時刻依存で View 非依存のため、フレーム内 1 回だけ計算する。
        // （View ごとの Graph 実行で同一時刻の FFT を二重計算していた無駄を排除）
        if (lastDispatchFrame_ == context.frameNumber) {
            return;
        }
        lastDispatchFrame_ = context.frameNumber;

        if (!context.fftOceanManager->IsInitialized()) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanPass: skipped. manager is not initialized.");
            return;
        }

        ID3D12GraphicsCommandList* cmdList = context.dxCommon->GetCommandList();
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "FFTOceanPass: skipped. command list is null.");
            return;
        }

        const float timeSeconds = context.waterRefractionSurfaceData
            ? context.waterRefractionSurfaceData->time
            : 0.0f;

        static uint32_t sLogFrameCounter = 0u;
        if ((sLogFrameCounter++ % 120u) == 0u) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanPass: dispatch time={:.3f} hasSurfaceData={} activeWaveCount={}",
                timeSeconds,
                context.waterRefractionSurfaceData != nullptr,
                context.waterRefractionSurfaceData ? context.waterRefractionSurfaceData->activeWaveCount : 0u);
        }

        context.fftOceanManager->Dispatch(cmdList, timeSeconds);
    }
}
