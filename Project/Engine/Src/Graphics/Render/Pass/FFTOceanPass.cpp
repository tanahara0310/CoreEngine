#include "pch.h"
#include "FFTOceanPass.h"

#include "Graphics/RHI/Debug/GpuStageScope.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/Water/FFTOceanManager.h"
#include "Graphics/Water/Foam/WaterFoamSystem.h"
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

        ID3D12GraphicsCommandList* cmdList = context.cmdList;
        if (!cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Command,
                "FFTOceanPass: skipped. command list is null.");
            return;
        }

        // FFT の時刻は水面サーフェス状態から独立している。
        // 以前は surface data の time を流用していたため、水面を非表示にした
        // フレームで 0 秒へ巻き戻り、再表示時に波形が飛んでいた。
        // プロファイラを渡すと、FFT 内部の各 Compute ステージが個別スロットで計測される。
        context.fftOceanManager->Dispatch(cmdList, context.fftOceanSimulationTime, context.gpuProfiler);

        // 白波の泡の蓄積と減衰（ヤコビアンが読み取り状態になった後）
        if (auto* foam = context.waterFoamSystem; foam && foam->IsInitialized()) {
            GpuStageScope stage(cmdList, context.gpuProfiler, "FFT Foam", GpuTimingCategory::WaterSimulation);
            foam->DispatchWhitecap(
                cmdList,
                context.fftOceanManager->GetJacobianSRVHandle(),
                context.fftOceanSimulationTime,
                context.fftOceanManager->GetSpectrumRevision());
        }
    }
}
