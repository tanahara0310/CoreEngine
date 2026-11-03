#include "pch.h"
#include "FFTOceanManager.h"

#include <algorithm>
#include <string>

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Debug/GpuMarker.h"
#include "Graphics/RHI/Debug/GpuStageScope.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Water/FFT/FFTOceanDispatchHelper.h"
#include "Graphics/Water/FFT/FFTOceanResourceFactory.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        // 計測スロット名。カスケードごとに別スロットにする必要がある
        // （同一スロットへ Begin/End を複数回積むと、最後の 1 回だけが残り
        //   他カスケード分の時間が計測から消えるため）。
        constexpr const char* kEvolutionStageNames[FFTOceanManager::kCascadeCount] = {
            "FFT C0 Evolution", "FFT C1 Evolution", "FFT C2 Evolution",
        };
        constexpr const char* kIFFTStageNames[FFTOceanManager::kCascadeCount] = {
            "FFT C0 IFFT", "FFT C1 IFFT", "FFT C2 IFFT",
        };
        constexpr const char* kFinalizeStageNames[FFTOceanManager::kCascadeCount] = {
            "FFT C0 Finalize", "FFT C1 Finalize", "FFT C2 Finalize",
        };
        // Readback は 1 フレームに 3 箇所ある。同一スロットへまとめると
        // 最後の 1 回で上書きされてしまうため、箇所ごとに別スロットにする。
        constexpr const char* kEvolutionReadbackStageName = "FFT Debug Readback (Evolution)";
        constexpr const char* kIFFTReadbackStageName = "FFT Debug Readback (IFFT)";
        constexpr const char* kSurfaceReadbackStageName = "FFT Debug Readback (Surface)";

        static_assert(FFTOceanManager::kCascadeCount == 3,
            "計測スロット名テーブルはカスケード 3 本前提。カスケード数を変えたら名前も追加すること");
    }

    bool FFTOceanManager::Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;

        if (!dxCommon_ || !descriptorAllocator_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: initialization failed. dxCommon={} descriptorAllocator={}",
                dxCommon_ != nullptr,
                descriptorAllocator_ != nullptr);
            return false;
        }

        // 実行に必要なパイプライン/リソースを順番に構築する。
        // （デバッグ用リードバックは FFTOceanDebugProbe が有効時に遅延生成する）
        if (!CreatePipelines()
            || !CreateOutputTextures()
            || !CreateIntermediateTextures()
            || !CreateSpectrumBuffer()) {
            return false;
        }

        // 定数を作成する。スペクトルは最初の SetSettings（水面の設定の反映）か Dispatch で作る
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            simulationConstants_[c].Initialize(dxCommon_->GetUploadRing());
            UpdateSimulationConstants(c, 0.0f);
        }
        isInitialized_ = true;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: initialized. resolution={} windSpeed={:.2f}",
            GetSettings().resolution,
            GetSettings().windSpeed);
        return true;
    }

    void FFTOceanManager::SetSettings(const Settings& settings)
    {
        // 設定（値を整えたもの）が同じで、スペクトルも作ってあれば何もしない
        if (!spectrum_.UpdateSettings(settings) && spectrumBuilt_) {
            return;
        }

        // GPU参照中のリソース更新を避けるため、フレーム完了を待機する（作る前は GPU が読んでいない）。
        if (dxCommon_ && spectrumBuilt_) {
            dxCommon_->WaitForGpuIdle();
            ++spectrumRevision_;
        }

        // 現在時刻を保ったままスペクトルと全カスケードの定数を作り直す
        const float currentTime = currentSimulationTime_;
        BuildSpectrum();
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            UpdateSimulationConstants(c, currentTime);
        }

        const Settings& applied = GetSettings();
        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: settings updated. amplitudeScale={:.3f} windDir=({:.3f}, {:.3f}) windSpeed={:.3f} choppiness={:.3f} components={} gravity={:.3f}",
            applied.amplitudeScale,
            applied.windDirection[0],
            applied.windDirection[1],
            applied.windSpeed,
            applied.choppiness,
            applied.activeComponentCount,
            applied.gravity);
    }

    void FFTOceanManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        float timeSeconds,
        GpuTimestampProfiler* profiler)
    {
        if (!isInitialized_ || !cmdList) {
            return;
        }

        if (!evolutionPipeline_.HasComputePSO() || !ifftPipeline_.HasComputePSO() || !finalizePipeline_.HasComputePSO()) {
            return;
        }

        // スペクトルをまだ作っていなければ、今の設定で作る
        if (!spectrumBuilt_) {
            BuildSpectrum();
        }

        // フレーム時刻・パッチ長を全カスケードの定数へ反映する。
        currentSimulationTime_ = timeSeconds;
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            UpdateSimulationConstants(c, timeSeconds);
        }

        // デバッグ計測（CVar オプトイン。無効時は全メソッドが即 return）
        debugProbe_.FlushPendingLogs(GetSettings().resolution);
        debugProbe_.LogDispatchSummary(
            timeSeconds,
            GetSettings().amplitudeScale,
            GetSettings().windSpeed,
            GetSettings().choppiness,
            (std::min)(GetSettings().activeComponentCount, FFTOceanSpectrum::kMaxComponents),
            MappedSpectrumSamples(0),
            GetSettings().resolution);

        // 出力配列テクスチャ（全スライス）をUAVへ遷移する。
        Barrier::Transition(cmdList, displacementMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, normalMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, jacobianMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）

        // カスケードごとに 時間発展 -> IFFT(2系統) -> 最終合成（スライスc へ書き込み）を実行する。
        // 中間ピンポンテクスチャ／IFFT定数リングはカスケード間で共有する（逐次実行）。
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            {
                GpuStageScope stage(cmdList, profiler, kEvolutionStageNames[c], GpuTimingCategory::WaterSimulation);
                DispatchEvolutionPass(cmdList, c);
            }

            // デバッグ用 Readback は本来の波形生成コストではないため、
            // 各ステージのスコープ外へ出して別スロットで計測する
            // （オプトイン機能のコストを本体の数値へ混ぜないため）。
            if (c == 0) {
                GpuStageScope stage(cmdList, profiler, kEvolutionReadbackStageName, GpuTimingCategory::WaterSimulation);
                debugProbe_.ScheduleEvolutionReadback(
                    cmdList,
                    spectrumPingPongA_[0], spectrumPingPongB_[0]);
            }

            uint32_t finalSpectrumAIndex = 0;
            uint32_t finalSpectrumBIndex = 0;
            {
                GpuStageScope stage(cmdList, profiler, kIFFTStageNames[c], GpuTimingCategory::WaterSimulation);
                finalSpectrumAIndex = DispatchIFFTForTexture(cmdList, spectrumPingPongA_, 0);
                finalSpectrumBIndex = DispatchIFFTForTexture(cmdList, spectrumPingPongB_, 0);
            }
            FFTOceanGpuTexture& finalSpectrumA = spectrumPingPongA_[finalSpectrumAIndex];
            FFTOceanGpuTexture& finalSpectrumB = spectrumPingPongB_[finalSpectrumBIndex];

            if (c == 0) {
                GpuStageScope stage(cmdList, profiler, kIFFTReadbackStageName, GpuTimingCategory::WaterSimulation);
                debugProbe_.OnIFFTCompleted(
                    cmdList,
                    finalSpectrumAIndex,
                    finalSpectrumBIndex,
                    GetSettings().resolution,
                    GetLog2Resolution(),
                    finalSpectrumA, finalSpectrumB);
            }

            {
                GpuStageScope stage(cmdList, profiler, kFinalizeStageNames[c], GpuTimingCategory::WaterSimulation);
                DispatchFinalizePass(cmdList, c, finalSpectrumA, finalSpectrumB);
            }

            // 次カスケードが中間ピンポンを書き換える前に、Finalizeの読み取り完了を保証する。
            Barrier::UAV(cmdList, displacementMap_);
            Barrier::UAV(cmdList, normalMap_);
            Barrier::UAV(cmdList, jacobianMap_);
        }

        // 後段シェーダー参照用にSRV状態へ戻す。変位は VS と DXR、法線とヤコビアンは水面の PS も読む
        constexpr D3D12_RESOURCE_STATES kAnyShaderResource =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        Barrier::Transition(cmdList, displacementMap_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, normalMap_, kAnyShaderResource);
        Barrier::Transition(cmdList, jacobianMap_, kAnyShaderResource);

        {
            GpuStageScope stage(cmdList, profiler, kSurfaceReadbackStageName, GpuTimingCategory::WaterSimulation);
            debugProbe_.ScheduleSurfaceReadback(
                cmdList,
                displacementMap_, normalMap_);
        }
    }

    bool FFTOceanManager::CreatePipelines()
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool evolutionBuilt = evolutionPipeline_.Build(
            dxCommon_->GetDevice(),
            shaderCompiler,
            reflectionBuilder,
            timeEvolutionShaderProvider_);
        const bool ifftBuilt = ifftPipeline_.Build(
            dxCommon_->GetDevice(),
            shaderCompiler,
            reflectionBuilder,
            ifftShaderProvider_);
        const bool finalizeBuilt = finalizePipeline_.Build(
            dxCommon_->GetDevice(),
            shaderCompiler,
            reflectionBuilder,
            finalizeShaderProvider_);

        if (!evolutionBuilt || !evolutionPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "FFTOceanManager: failed to build time evolution compute pipeline.");
            return false;
        }

        if (!ifftBuilt || !ifftPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "FFTOceanManager: failed to build IFFT compute pipeline.");
            return false;
        }

        if (!finalizeBuilt || !finalizePipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "FFTOceanManager: failed to build finalize compute pipeline.");
            return false;
        }

        return true;
    }

    bool FFTOceanManager::CreateOutputTextures()
    {
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorAllocator_) {
            return false;
        }

        // カスケードをスライスに持つ配列テクスチャを作る。法線ミップ連鎖は廃止済み
        // （AAは水面シェーダ側の距離フェード＋大カスケードで代替）、常にミップ0のみ。
        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        const DXGI_FORMAT format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = GetSettings().resolution;
        desc.Height = GetSettings().resolution;
        desc.DepthOrArraySize = static_cast<UINT16>(kCascadeCount);
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // 出力 1 本分の生成（リソース＋配列SRV＋スライスUAV）。
        // 配列SRVは全スライスを1ビューで見せる（名前ベースのバインドで水面/RTが参照）。
        // スライスUAVは Finalize が各カスケードのスライスへ書き込むために使う。
        auto createOutput = [&](CascadeOutputTexture& out, const std::string& name) -> bool {
            try {
                out.Reset(
                    ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                return false;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC s{};
            s.Format = format;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2DArray.MostDetailedMip = 0;
            s.Texture2DArray.MipLevels = 1;
            s.Texture2DArray.FirstArraySlice = 0;
            s.Texture2DArray.ArraySize = kCascadeCount;
            out.srv = descriptorAllocator_->CreateSRV(out.Get(), s, "FFTOcean" + name + "ArraySRV");

            for (uint32_t c = 0; c < kCascadeCount; ++c) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
                u.Format = format;
                u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                u.Texture2DArray.MipSlice = 0;
                u.Texture2DArray.FirstArraySlice = c;
                u.Texture2DArray.ArraySize = 1;
                out.sliceUav[c] = descriptorAllocator_->CreateUAV(
                    out.Get(), u, "FFTOcean" + name + "UAV_" + std::to_string(c));
            }

            return true;
        };

        return createOutput(displacementMap_, "Displacement")
            && createOutput(normalMap_, "Normal")
            && createOutput(jacobianMap_, "Jacobian");
    }

    bool FFTOceanManager::CreateIntermediateTextures()
    {
        return FFTOceanResourceFactory::CreateIntermediateTextures(
            dxCommon_->GetDevice(),
            descriptorAllocator_,
            GetSettings().resolution,
            spectrumPingPongA_,
            spectrumPingPongB_);
    }

    bool FFTOceanManager::CreateSpectrumBuffer()
    {
        // 初期スペクトル（h0）はパッチ長依存のため、カスケードごとに独立したバッファを作る。
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            if (!FFTOceanResourceFactory::CreateSpectrumBuffers(
                    dxCommon_->GetDevice(),
                    descriptorAllocator_,
                    GetSettings().resolution,
                    sizeof(SpectrumSample),
                    spectrumBuffers_[c])) {
                return false;
            }
        }
        spectrumBufferDirty_ = true;
        return true;
    }

    void FFTOceanManager::BuildSpectrum()
    {
        std::array<SpectrumSample*, kCascadeCount> samples{};
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            samples[c] = MappedSpectrumSamples(c);
        }
        const uint32_t resolution = GetSettings().resolution;
        spectrum_.Build(samples, static_cast<size_t>(resolution * resolution));

        spectrumBufferDirty_ = true;
        spectrumBuilt_ = isInitialized_;
    }

    void FFTOceanManager::UpdateSimulationConstants(uint32_t cascadeIndex, float timeSeconds)
    {
        if (cascadeIndex >= kCascadeCount) {
            return;
        }

        // カスケード固有の patchLength / 振幅を詰める。
        SimulationConstants constants{};
        constants.resolution = GetSettings().resolution;
        constants.activeComponentCount = (std::min)(GetSettings().activeComponentCount, FFTOceanSpectrum::kMaxComponents);
        constants.patchLength = FFTOceanSpectrum::GetCascadePatchLength(cascadeIndex);
        constants.timeSeconds = timeSeconds;
        constants.choppiness = GetSettings().choppiness;
        constants.gravity = GetSettings().gravity;
        // カスケード配分はスペクトル生成時の targetRmsHeight 正規化で織り込み済み。
        // ここはユーザー/プリセットの倍率（較正済み波高に対する相対値）だけを掛ける。
        constants.amplitudeScale = GetSettings().amplitudeScale;
        simulationConstants_[cascadeIndex].Set(constants);
    }

    D3D12_GPU_VIRTUAL_ADDRESS FFTOceanManager::GetSimulationConstantsAddress(uint32_t cascadeIndex) const
    {
        if (cascadeIndex >= kCascadeCount) {
            return 0;
        }
        return simulationConstants_[cascadeIndex].Address();
    }

    D3D12_GPU_VIRTUAL_ADDRESS FFTOceanManager::UploadIFFTConstants(
        uint32_t stageIndex, bool isHorizontal, float normalizationScale) const
    {
        IFFTConstants constants{};
        constants.resolution = GetSettings().resolution;
        constants.stageIndex = stageIndex;
        constants.isHorizontal = isHorizontal ? 1 : 0;
        constants.normalizationScale = normalizationScale;
        return dxCommon_->GetUploadRing().AllocateConstants(constants);
    }

    void FFTOceanManager::DispatchEvolutionPass(ID3D12GraphicsCommandList* cmdList, uint32_t cascadeIndex)
    {
        if (cascadeIndex >= kCascadeCount) {
            return;
        }

        if (spectrumBufferDirty_ && cascadeIndex == 0) {
            // スペクトル再構築後の最初の Dispatch で UPLOAD → DEFAULT へ全カスケードをコピーする。
            // SRV は DEFAULT 側（FFTOceanResourceFactory::CreateSpectrumBuffers 参照）。
            spectrumBufferDirty_ = false;
            uint64_t copiedBytes = 0;
            for (uint32_t c = 0; c < kCascadeCount; ++c) {
                FFTOceanSpectrumBufferSet& buffers = spectrumBuffers_[c];
                if (!buffers.defaultBuffer.IsValid() || !buffers.uploadBuffer) {
                    continue;
                }
                Barrier::Transition(
                    cmdList, buffers.defaultBuffer,
                    D3D12_RESOURCE_STATE_COPY_DEST);
                cmdList->CopyResource(buffers.defaultBuffer.Get(), buffers.uploadBuffer.Get());
                Barrier::Transition(
                    cmdList, buffers.defaultBuffer,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                copiedBytes += buffers.uploadBuffer->GetDesc().Width;
            }
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: spectrum buffers copied to default heap. bytes={}",
                copiedBytes);
        }

        FFTOceanDispatchHelper::DispatchEvolutionPass(
            cmdList,
            spectrumPingPongA_,
            spectrumPingPongB_,
            evolutionPipeline_,
            spectrumBuffers_[cascadeIndex].srv,
            GetSimulationConstantsAddress(cascadeIndex),
            GetSettings().resolution);
    }

    uint32_t FFTOceanManager::DispatchIFFTForTexture(
        ID3D12GraphicsCommandList* cmdList,
        FFTOceanPingPong& textures,
        uint32_t initialIndex)
    {
        uint32_t readIndex = initialIndex;
        uint32_t writeIndex = (initialIndex + 1) % kPingPongCount;
        const uint32_t log2Resolution = GetLog2Resolution();

        auto dispatchStage = [&](uint32_t stageIndex, bool isHorizontal, float normalizationScale) {
            const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress =
                UploadIFFTConstants(stageIndex, isHorizontal, normalizationScale);
            FFTOceanDispatchHelper::DispatchIFFTPass(
                cmdList,
                textures[readIndex],
                textures[writeIndex],
                ifftPipeline_,
                constantsAddress,
                GetSettings().resolution);
            std::swap(readIndex, writeIndex);
        };

        // 横方向IFFT。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            dispatchStage(stageIndex, true, 1.0f);
        }

        // 縦方向IFFT。最終ステージのみ1/N正規化を適用する。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            const float normalizationScale = (stageIndex + 1 == log2Resolution)
                ? 1.0f / static_cast<float>(GetSettings().resolution)
                : 1.0f;
            dispatchStage(stageIndex, false, normalizationScale);
        }

        return readIndex;
    }

    void FFTOceanManager::DispatchFinalizePass(
        ID3D12GraphicsCommandList* cmdList,
        uint32_t cascadeIndex,
        FFTOceanGpuTexture& spectrumA,
        FFTOceanGpuTexture& spectrumB)
    {
        if (cascadeIndex >= kCascadeCount) {
            return;
        }

        FFTOceanDispatchHelper::DispatchFinalizePass(
            cmdList,
            spectrumA,
            spectrumB,
            finalizePipeline_,
            displacementMap_.sliceUav[cascadeIndex].gpuHandle,
            normalMap_.sliceUav[cascadeIndex].gpuHandle,
            jacobianMap_.sliceUav[cascadeIndex].gpuHandle,
            GetSimulationConstantsAddress(cascadeIndex),
            GetSettings().resolution);
    }

    uint32_t FFTOceanManager::GetLog2Resolution() const
    {
        uint32_t log2Resolution = 0;
        uint32_t resolution = GetSettings().resolution;
        while (resolution > 1) {
            resolution >>= 1;
            ++log2Resolution;
        }
        return log2Resolution;
    }
}
