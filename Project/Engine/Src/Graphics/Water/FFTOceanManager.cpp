#include "pch.h"
#include "FFTOceanManager.h"

#include <algorithm>
#include <cmath>

#include <string>

#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Debug/GpuMarker.h"
#include "Graphics/RHI/Debug/GpuTimestampProfiler.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Water/FFTOceanDispatchHelper.h"
#include "Graphics/Water/FFTOceanManagerLogHelper.h"
#include "Graphics/Water/FFTOceanResourceFactory.h"
#include "Graphics/Water/FFTOceanSpectrumBuilder.h"
#include "Utility/Logger/Logger.h"

// カスケードの数値はシェーダーと共有する。マクロだけで構成されているため
// HLSL / C++ のどちらからも include でき、ここが唯一の情報源になる。
// （実行時のシェーダーコンパイルは配備先の同ファイルを読むので、
//   xcopy 済みのツリーとソースツリーの内容は常に一致する）
#include "../../../Assets/Shaders/Water/Common/FFTOceanCascadeValues.hlsli"

namespace CoreEngine
{
    namespace
    {
        constexpr UINT Align256(UINT value)
        {
            return (value + 255) & ~255;
        }

        /// @brief PIX マーカーと GPU/CPU タイムスタンプを同じ範囲で積むスコープ
        /// @details FFT の内訳（時間発展 / IFFT / 合成 / 泡）は RenderGraph から見ると
        ///          FFTOceanPass 1 パスの中身なので、パス単位の自動計測では分解できない。
        ///          ここで名前付きスロットを直接取り、PIX 側にも同名のイベントを出す。
        class FFTStageScope
        {
        public:
            FFTStageScope(
                ID3D12GraphicsCommandList* cmdList,
                GpuTimestampProfiler* profiler,
                const char* name)
                : cmdList_(cmdList)
                , profiler_(profiler)
            {
                BeginGpuMarker(cmdList_, name);
                if (profiler_ && cmdList_) {
                    slot_ = profiler_->GetOrCreateNamedSlot(name, GpuTimingCategory::WaterSimulation);
                    if (slot_ != UINT32_MAX) {
                        profiler_->BeginCpuTimestamp(slot_);
                        profiler_->BeginGpuTimestamp(slot_, cmdList_);
                    }
                }
            }
            ~FFTStageScope()
            {
                if (profiler_ && slot_ != UINT32_MAX) {
                    profiler_->EndGpuTimestamp(slot_, cmdList_);
                    profiler_->EndCpuTimestamp(slot_);
                }
                EndGpuMarker(cmdList_);
            }
            FFTStageScope(const FFTStageScope&) = delete;
            FFTStageScope& operator=(const FFTStageScope&) = delete;

        private:
            ID3D12GraphicsCommandList* cmdList_ = nullptr;
            GpuTimestampProfiler* profiler_ = nullptr;
            uint32_t slot_ = UINT32_MAX;
        };

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
        constexpr const char* kFoamStageName = "FFT Foam";
        // Readback は 1 フレームに 3 箇所ある。同一スロットへまとめると
        // 最後の 1 回で上書きされてしまうため、箇所ごとに別スロットにする。
        constexpr const char* kEvolutionReadbackStageName = "FFT Debug Readback (Evolution)";
        constexpr const char* kIFFTReadbackStageName = "FFT Debug Readback (IFFT)";
        constexpr const char* kSurfaceReadbackStageName = "FFT Debug Readback (Surface)";

        static_assert(FFTOceanManager::kCascadeCount == 3,
            "計測スロット名テーブルはカスケード 3 本前提。カスケード数を変えたら名前も追加すること");

        // ---- カスケードの幾何定数（値は FFTOceanCascadeValues.hlsli が唯一の情報源）----
        // 以前はこれらの数値がシェーダー 3 本とここの計 4 箇所へ手コピーされ、
        // 「一致必須」というコメントだけで守られていた。共有マクロ経由にしたことで、
        // 片方だけ変えるという事故が起こりえなくなっている。
        static_assert(FFTOceanManager::kCascadeCount == FFT_OCEAN_CASCADE_COUNT,
            "FFTOceanManager::kCascadeCount must match FFT_OCEAN_CASCADE_COUNT in FFTOceanCascadeValues.hlsli");

        // ワールドパッチ長（m）。すべて素数にして互いに素とし、
        // 合成波形の繰り返し周期を LCM = 521×127×31 ≈ 205万m まで引き延ばす。
        constexpr float kCascadePatchLength[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_PATCH_LENGTHS;

        // サンプリング格子回転（cos/sin）。0° / +26° / -49°。
        // 格子軸が揃っていると各カスケードのタイル周期が同じ向きで強め合い、格子模様として見える。
        // 風向はスペクトル側の逆回転で補正するので、波の進行方向はワールドで共通のまま。
        constexpr float kCascadeRotCos[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_COS;
        constexpr float kCascadeRotSin[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_SIN;

        // カスケードごとの乱数シード。同一シードだと全カスケードの位相パターンが
        // 完全相関し「同じ波の配置が縮尺違いで繰り返される」自己相似模様になる。
        constexpr uint32_t kCascadeRandomSeed[FFTOceanManager::kCascadeCount] = {
            20260626u, 20260626u + 7919u, 20260626u + 2u * 7919u };

        // カスケード間の相対エネルギーは帯域制限後の Phillips スペクトル自身が決めるので、
        // 配分比の手調整定数は持たない（較正は全カスケード共通の 1 スケールだけ）。

        // 帯域境界（波長 m → 波数 rad/m）。数値の情報源は FFTOceanCascadeValues.hlsli。
        constexpr float kTwoPiValue = 6.28318530718f;
        constexpr float kBandBoundaryLowK = kTwoPiValue / FFT_OCEAN_BAND_LAMBDA_01;
        constexpr float kBandBoundaryHighK = kTwoPiValue / FFT_OCEAN_BAND_LAMBDA_12;
        constexpr float kBandLogHalfWidth = FFT_OCEAN_BAND_LOG_HALFWIDTH;

        // 波高計算に使う実効風速の範囲 [m/s]。Pierson-Moskowitz の Hs ≈ 0.21 v²/g は
        // v² で成長するため、プリセットの強風（52m/s 等）をそのまま入れると
        // 有義波高 50m 超の非現実的な水の山になる。上限 32m/s（Hs ≈ 22m の猛烈な嵐）で飽和させる。
        constexpr float kMinHeightWindSpeed = 1.0f;
        constexpr float kMaxHeightWindSpeed = 32.0f;
    }

    bool FFTOceanManager::Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator)
    {
        // 外部依存を保持し、設定をGPU向けに正規化してから初期化を開始する。
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        SanitizeSettings(settings_);

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
            || !CreateFoamResources()
            || !CreateIntermediateTextures()
            || !CreateSpectrumBuffer()
            || !CreateSimulationConstantBuffer()
            || !CreateIFFTConstantBuffer()) {
            return false;
        }

        // 初期スペクトルと定数を作成して、初回Dispatch可能な状態へ揃える。
        BuildSpectrum();
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            UpdateSimulationConstants(c, 0.0f);
        }
        UpdateIFFTConstants(0, true, 1.0f);
        isInitialized_ = true;

        FFTOceanManagerLogHelper::LogInitialize(
            settings_.resolution,
            settings_.windSpeed);
        return true;
    }

    void FFTOceanManager::SetSettings(const Settings& settings)
    {
        // 外部入力を安全なレンジへ補正する。
        Settings sanitized = settings;
        SanitizeSettings(sanitized);

        // 解像度変更はリソース再生成が必要なため現時点では固定。
        sanitized.resolution = settings_.resolution;

        const bool settingsChanged =
            sanitized.amplitudeScale != settings_.amplitudeScale ||
            sanitized.windDirection[0] != settings_.windDirection[0] ||
            sanitized.windDirection[1] != settings_.windDirection[1] ||
            sanitized.windSpeed != settings_.windSpeed ||
            sanitized.choppiness != settings_.choppiness ||
            sanitized.activeComponentCount != settings_.activeComponentCount ||
            sanitized.gravity != settings_.gravity;

        if (!settingsChanged) {
            return;
        }

        // GPU参照中のリソース更新を避けるため、フレーム完了を待機する。
        if (dxCommon_) {
            dxCommon_->WaitForGpuIdle();
        }

        settings_ = sanitized;

        // 波面が不連続に変わるため、蓄積済みの泡は次の泡パスで破棄する。
        foamResetPending_ = true;

        // 現在時刻を保ったままスペクトルと全カスケードの定数を作り直す。
        // （以前はマップ済み UPLOAD バッファ（write-combined）から読み戻していた。
        //   WC ページの CPU 読みは非常に遅いためメンバ変数で保持する）
        const float currentTime = currentSimulationTime_;
        BuildSpectrum();
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            UpdateSimulationConstants(c, currentTime);
        }

        FFTOceanManagerLogHelper::LogSettingsUpdated(
            settings_.amplitudeScale,
            settings_.windDirection[0],
            settings_.windDirection[1],
            settings_.windSpeed,
            settings_.choppiness,
            settings_.activeComponentCount,
            settings_.gravity);
    }

    void FFTOceanManager::SetFoamSettings(const FoamSettings& settings)
    {
        // 無効 → 有効の遷移では古い蓄積が残らないよう破棄してから再開する
        if (!foamSettings_.enabled && settings.enabled) {
            foamResetPending_ = true;
        }
        foamSettings_ = settings;
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

        // フレーム時刻・パッチ長を全カスケードのスロットへ書き込む。
        currentSimulationTime_ = timeSeconds;
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            UpdateSimulationConstants(c, timeSeconds);
        }
        ifftConstantsWriteIndex_ = 0;

        // デバッグ計測（CVar オプトイン。無効時は全メソッドが即 return）
        debugProbe_.FlushPendingLogs(settings_.resolution);
        debugProbe_.LogDispatchSummary(
            timeSeconds,
            settings_.amplitudeScale,
            settings_.windSpeed,
            settings_.choppiness,
            (std::min)(settings_.activeComponentCount, kMaxSpectrumComponents),
            MappedSpectrumSamples(0),
            settings_.resolution);

        // 出力配列テクスチャ（全スライス）をUAVへ遷移する。
        Barrier::Transition(cmdList, displacementMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, normalMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, jacobianMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // SRV ヒープはフレーム先頭で CommandContext が 1 回バインドする（個別バインドは不要）

        // カスケードごとに 時間発展 -> IFFT(2系統) -> 最終合成（スライスc へ書き込み）を実行する。
        // 中間ピンポンテクスチャ／IFFT定数リングはカスケード間で共有する（逐次実行）。
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            {
                FFTStageScope stage(cmdList, profiler, kEvolutionStageNames[c]);
                DispatchEvolutionPass(cmdList, c);
            }

            // デバッグ用 Readback は本来の波形生成コストではないため、
            // 各ステージのスコープ外へ出して別スロットで計測する
            // （オプトイン機能のコストを本体の数値へ混ぜないため）。
            if (c == 0) {
                FFTStageScope stage(cmdList, profiler, kEvolutionReadbackStageName);
                debugProbe_.ScheduleEvolutionReadback(
                    cmdList,
                    spectrumPingPongA_[0], spectrumPingPongB_[0]);
            }

            uint32_t finalSpectrumAIndex = 0;
            uint32_t finalSpectrumBIndex = 0;
            {
                FFTStageScope stage(cmdList, profiler, kIFFTStageNames[c]);
                finalSpectrumAIndex = DispatchIFFTForTexture(cmdList, spectrumPingPongA_, 0);
                finalSpectrumBIndex = DispatchIFFTForTexture(cmdList, spectrumPingPongB_, 0);
            }
            FFTOceanGpuTexture& finalSpectrumA = spectrumPingPongA_[finalSpectrumAIndex];
            FFTOceanGpuTexture& finalSpectrumB = spectrumPingPongB_[finalSpectrumBIndex];

            if (c == 0) {
                FFTStageScope stage(cmdList, profiler, kIFFTReadbackStageName);
                debugProbe_.OnIFFTCompleted(
                    cmdList,
                    finalSpectrumAIndex,
                    finalSpectrumBIndex,
                    settings_.resolution,
                    GetLog2Resolution(),
                    finalSpectrumA, finalSpectrumB);
            }

            {
                FFTStageScope stage(cmdList, profiler, kFinalizeStageNames[c]);
                DispatchFinalizePass(cmdList, c, finalSpectrumA, finalSpectrumB);
            }

            // 次カスケードが中間ピンポンを書き換える前に、Finalizeの読み取り完了を保証する。
            Barrier::UAV(cmdList, displacementMap_);
            Barrier::UAV(cmdList, normalMap_);
            Barrier::UAV(cmdList, jacobianMap_);
        }

        // 後段シェーダー参照用にSRV状態へ戻す（法線ミップ連鎖は廃止済み）。
        Barrier::Transition(cmdList, displacementMap_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, normalMap_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, jacobianMap_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // 泡の蓄積・減衰（ヤコビアンが SRV 状態になった後に実行する）
        {
            FFTStageScope stage(cmdList, profiler, kFoamStageName);
            DispatchFoamPass(cmdList, timeSeconds);
        }

        {
            FFTStageScope stage(cmdList, profiler, kSurfaceReadbackStageName);
            debugProbe_.ScheduleSurfaceReadback(
                cmdList,
                displacementMap_, normalMap_);
        }
    }

    bool FFTOceanManager::CreateFoamResources()
    {
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorAllocator_) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> device = dxCommon_->GetDevice();
        // 泡はスカラー被覆率のみなので R16_FLOAT で足りる（帯域は RGBA16 の 1/4）
        const DXGI_FORMAT format = DXGI_FORMAT_R16_FLOAT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = settings_.resolution;
        desc.Height = settings_.resolution;
        desc.DepthOrArraySize = static_cast<UINT16>(kCascadeCount);
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            for (uint32_t i = 0; i < kPingPongCount; ++i) {
                foam_[i].Reset(
                    ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
        }
        catch (const std::exception&) {
            return false;
        }

        for (uint32_t i = 0; i < kPingPongCount; ++i) {
            const std::string idx = std::to_string(i);

            // SRV / UAV とも全スライスを 1 ビューで見せる
            // （泡パスは Dispatch z = カスケードで全スライスを一括処理する）。
            D3D12_SHADER_RESOURCE_VIEW_DESC s{};
            s.Format = format;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2DArray.MostDetailedMip = 0;
            s.Texture2DArray.MipLevels = 1;
            s.Texture2DArray.FirstArraySlice = 0;
            s.Texture2DArray.ArraySize = kCascadeCount;
            foam_[i].srv = descriptorAllocator_->CreateSRV(foam_[i].Get(), s, "FFTOceanFoamSRV_" + idx);

            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = format;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            u.Texture2DArray.MipSlice = 0;
            u.Texture2DArray.FirstArraySlice = 0;
            u.Texture2DArray.ArraySize = kCascadeCount;
            foam_[i].uav = descriptorAllocator_->CreateUAV(foam_[i].Get(), u, "FFTOceanFoamUAV_" + idx);
        }

        // 定数は 1 スロットのみ（毎フレーム上書き。simulationConstants と同じ運用）。
        void* mappedFoamConstants = nullptr;
        const bool created = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            dxCommon_->GetDevice(),
            Align256(sizeof(FoamConstants)),
            foamConstantsBuffer_,
            mappedFoamConstants);
        mappedFoamConstants_ = reinterpret_cast<uint8_t*>(mappedFoamConstants);

        foamResetPending_ = true;
        return created;
    }

    void FFTOceanManager::DispatchFoamPass(ID3D12GraphicsCommandList* cmdList, float timeSeconds)
    {
        if (!foamPipeline_.HasComputePSO() || !mappedFoamConstants_) {
            return;
        }

        // 無効時はパス自体をスキップする（PS 側も gFoamEnabled=0 で読まない）。
        // 再有効化時に古い泡が残らないよう SetFoamSettings がリセットを積む。
        if (!foamSettings_.enabled) {
            foamPreviousTimeSeconds_ = timeSeconds;
            return;
        }

        // シミュレーション時刻から dt を得る（ポーズ中は 0 = 泡凍結。
        // シーン切替やスペクトル再構築直後の巨大な dt は上限で丸める）。
        const float rawDelta = timeSeconds - foamPreviousTimeSeconds_;
        const float deltaSeconds = (std::clamp)(rawDelta, 0.0f, 0.1f);
        foamPreviousTimeSeconds_ = timeSeconds;

        FoamConstants* slot = reinterpret_cast<FoamConstants*>(mappedFoamConstants_);
        slot->resolution = settings_.resolution;
        slot->deltaSeconds = deltaSeconds;
        slot->foamBias = foamSettings_.bias;
        slot->foamGain = foamSettings_.gain;
        slot->cascadeWeights[0] = foamSettings_.cascadeWeights[0];
        slot->cascadeWeights[1] = foamSettings_.cascadeWeights[1];
        slot->cascadeWeights[2] = foamSettings_.cascadeWeights[2];
        slot->decaySeconds = foamSettings_.decaySeconds;
        slot->resetFoam = foamResetPending_ ? 1u : 0u;
        foamResetPending_ = false;

        // 書き込み先はフレームカウンタの偶奇で決める（純粋関数。トグル変数は持たない）。
        const uint32_t writeIndex = foamFrameIndex_ & 1u;
        const uint32_t readIndex = writeIndex ^ 1u;

        // read 側は本パスの gFoamPrev（CS）に加えて、同一フレームの水面描画（PS の t21）
        // からも読まれるため、PIXEL を含む読み取り状態にする。
        Barrier::Transition(
            cmdList, foam_[writeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(
            cmdList, foam_[readIndex],
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(foamPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(foamPipeline_.GetComputeRootSignature());

        const int jacobianSlot = foamPipeline_.GetComputeRootParamIndex("gJacobian");
        if (jacobianSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(jacobianSlot), jacobianMap_.srv.gpuHandle);
        }
        const int prevSlot = foamPipeline_.GetComputeRootParamIndex("gFoamPrev");
        if (prevSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(prevSlot), foam_[readIndex].srv.gpuHandle);
        }
        const int outputSlot = foamPipeline_.GetComputeRootParamIndex("gFoamOutput");
        if (outputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outputSlot), foam_[writeIndex].uav.gpuHandle);
        }
        const int constantsSlot = foamPipeline_.GetComputeRootParamIndex("FFTOceanFoamConstants");
        if (constantsSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(constantsSlot), foamConstantsBuffer_->GetGPUVirtualAddress());
        }

        const UINT dispatchX = (settings_.resolution + 7) / 8;
        const UINT dispatchY = (settings_.resolution + 7) / 8;
        cmdList->Dispatch(dispatchX, dispatchY, kCascadeCount);

        // 書き込んだ側は Water.PS（ピクセルシェーダー）が t21 で読むため、
        // PIXEL を含む読み取り状態へ遷移させる（来フレームは CS の gFoamPrev としても読む）。
        Barrier::Transition(
            cmdList, foam_[writeIndex],
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        ++foamFrameIndex_;
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
        const bool foamBuilt = foamPipeline_.Build(
            dxCommon_->GetDevice(),
            shaderCompiler,
            reflectionBuilder,
            foamShaderProvider_);

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

        if (!foamBuilt || !foamPipeline_.HasComputePSO()) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "FFTOceanManager: failed to build foam accumulate compute pipeline.");
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
        desc.Width = settings_.resolution;
        desc.Height = settings_.resolution;
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
            settings_.resolution,
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
                    settings_.resolution,
                    sizeof(SpectrumSample),
                    spectrumBuffers_[c])) {
                return false;
            }
        }
        spectrumBufferDirty_ = true;
        return true;
    }

    bool FFTOceanManager::CreateSimulationConstantBuffer()
    {
        // 1フレームで全カスケードのDispatchを積むため、256B境界の kCascadeCount スロットを確保する。
        void* mappedSimulationConstants = nullptr;
        const bool created = FFTOceanResourceFactory::CreateSimulationConstantBuffer(
            dxCommon_->GetDevice(),
            Align256(sizeof(SimulationConstants)) * kCascadeCount,
            simulationConstantsBuffer_,
            mappedSimulationConstants);
        mappedSimulationConstants_ = reinterpret_cast<uint8_t*>(mappedSimulationConstants);
        return created;
    }

    bool FFTOceanManager::CreateIFFTConstantBuffer()
    {
        return FFTOceanResourceFactory::CreateIFFTConstantBuffer(
            dxCommon_->GetDevice(),
            sizeof(IFFTConstants),
            kMaxIFFTPassCount,
            ifftConstantsBuffer_,
            mappedIFFTConstantsData_);
    }

    void FFTOceanManager::SanitizeSettings(Settings& settings) const
    {
        FFTOceanSpectrumBuilder::Settings builderSettings{};
        builderSettings.resolution = settings.resolution;
        // パッチ長は Manager 設定から撤去済み（実値はカスケード定数）。
        // Builder のサニタイズには有効な代表値を渡す。
        builderSettings.patchLength = kCascadePatchLength[0];
        builderSettings.amplitudeScale = settings.amplitudeScale;
        builderSettings.windDirection[0] = settings.windDirection[0];
        builderSettings.windDirection[1] = settings.windDirection[1];
        builderSettings.windSpeed = settings.windSpeed;
        builderSettings.fetchMeters = settings.fetchMeters;
        builderSettings.choppiness = settings.choppiness;
        builderSettings.activeComponentCount = settings.activeComponentCount;
        builderSettings.gravity = settings.gravity;

        FFTOceanSpectrumBuilder::SanitizeSettings(builderSettings, kMaxSpectrumComponents);

        settings.resolution = builderSettings.resolution;
        settings.amplitudeScale = builderSettings.amplitudeScale;
        settings.windDirection[0] = builderSettings.windDirection[0];
        settings.windDirection[1] = builderSettings.windDirection[1];
        settings.windSpeed = builderSettings.windSpeed;
        settings.fetchMeters = builderSettings.fetchMeters;

        // うねりのサニタイズ（ビルダーは方向が正規化済みである前提で走る）
        settings.swellHeightMeters = (std::clamp)(settings.swellHeightMeters, 0.0f, 20.0f);
        settings.swellPeriodSeconds = (std::clamp)(settings.swellPeriodSeconds, 2.0f, 30.0f);
        settings.swellRelativeWidth = (std::clamp)(settings.swellRelativeWidth, 0.01f, 0.5f);
        settings.swellSpreadExponent = (std::clamp)(settings.swellSpreadExponent, 1.0f, 60.0f);
        const float swellDirLength = std::sqrt(
            settings.swellDirection[0] * settings.swellDirection[0]
            + settings.swellDirection[1] * settings.swellDirection[1]);
        if (swellDirLength <= 1.0e-4f) {
            settings.swellDirection[0] = 1.0f;
            settings.swellDirection[1] = 0.0f;
        } else {
            settings.swellDirection[0] /= swellDirLength;
            settings.swellDirection[1] /= swellDirLength;
        }
        settings.choppiness = builderSettings.choppiness;
        settings.activeComponentCount = builderSettings.activeComponentCount;
        settings.gravity = builderSettings.gravity;
    }

    void FFTOceanManager::BuildSpectrum()
    {
        // カスケードごとに固有のパッチ長で、帯域制限した JONSWAP スペクトルを生成する。
        // 海面は「風波（wind sea）＋ うねり（swell）」の重ね合わせで作る。
        const uint32_t resolution = settings_.resolution;
        const uint32_t sampleCount = resolution * resolution;

        // 風波の波高較正: JONSWAP のフェッチ制限成長則から有義波高 Hs を求め、
        // 目標RMS = Hs/4 とする。カスケード間の配分はスペクトル自身が決めるので
        // 手調整の配分比は要らない（帯域制限 + Δk² 正規化が前提）。
        const float heightWindSpeed = (std::clamp)(settings_.windSpeed, kMinHeightWindSpeed, kMaxHeightWindSpeed);
        const float gravityValue = (std::max)(settings_.gravity, 0.1f);
        // JONSWAP のフェッチ制限成長則（無次元量）: F* = gF/U², Hs* = 0.0016·√F*。
        // 完全発達（Pierson-Moskowitz）の Hs* = 0.21 を上限にクランプする。
        // ピーク周波数側と同じフェッチで同時に飽和するので、波高とピーク波長の整合が保たれる。
        const float dimensionlessFetch =
            gravityValue * settings_.fetchMeters / (heightWindSpeed * heightWindSpeed);
        const float dimensionlessWaveHeight =
            (std::min)(0.0016f * std::sqrt(dimensionlessFetch), 0.21f);
        const float significantWaveHeight =
            dimensionlessWaveHeight * heightWindSpeed * heightWindSpeed / gravityValue;
        const float windSeaTargetRms = significantWaveHeight * 0.25f;

        // うねりの目標 RMS（Hs/4）とピーク角周波数（周期から）。
        // 風波と違い、うねりは「遠方で作られて到達したもの」なので、その場の風速では
        // なくユーザー指定の波高・周期・方向で与える。
        const bool swellActive =
            settings_.swellEnabled && settings_.swellHeightMeters > 1.0e-3f
            && settings_.swellPeriodSeconds > 0.1f;
        const float swellTargetRms = swellActive ? settings_.swellHeightMeters * 0.25f : 0.0f;
        const float swellPeakAngularFrequency =
            swellActive ? (kTwoPiValue / settings_.swellPeriodSeconds) : 0.0f;
        const float swellWaveLength = swellActive
            ? kTwoPiValue * gravityValue / (swellPeakAngularFrequency * swellPeakAngularFrequency)
            : 0.0f;

        // カスケード c 用のビルダー設定を組む（成分重みだけを差し替えて 3 回使う）
        const auto makeBuilderSettings =
            [&](uint32_t c, float windWeight, float swellWeight) {
                FFTOceanSpectrumBuilder::Settings s{};
                s.resolution = settings_.resolution;
                s.patchLength = kCascadePatchLength[c];
                s.amplitudeScale = settings_.amplitudeScale;
                // サンプリング格子がカスケードごとに回転しているため、スペクトルの向きは
                // テクスチャ座標系（＝回転後の格子系）へ順回転して渡す。
                // ★うねりの向きも必ず同じ回転を掛けること★
                //   （片方だけだと風波とうねりの相対角度がカスケードごとに変わる）
                s.windDirection[0] =
                    kCascadeRotCos[c] * settings_.windDirection[0] - kCascadeRotSin[c] * settings_.windDirection[1];
                s.windDirection[1] =
                    kCascadeRotSin[c] * settings_.windDirection[0] + kCascadeRotCos[c] * settings_.windDirection[1];
                s.swellDirection[0] =
                    kCascadeRotCos[c] * settings_.swellDirection[0] - kCascadeRotSin[c] * settings_.swellDirection[1];
                s.swellDirection[1] =
                    kCascadeRotSin[c] * settings_.swellDirection[0] + kCascadeRotCos[c] * settings_.swellDirection[1];
                s.windSpeed = settings_.windSpeed;
                s.fetchMeters = settings_.fetchMeters;
                s.choppiness = settings_.choppiness;
                s.activeComponentCount = settings_.activeComponentCount;
                s.gravity = settings_.gravity;
                s.randomSeed = kCascadeRandomSeed[c];
                // 帯域制限（相補窓）。境界は全カスケード共通で、担当帯は index で決まる。
                s.cascadeIndex = c;
                s.bandBoundaryLowK = kBandBoundaryLowK;
                s.bandBoundaryHighK = kBandBoundaryHighK;
                s.bandLogHalfWidth = kBandLogHalfWidth;
                s.swellPeakAngularFrequency = swellPeakAngularFrequency;
                s.swellRelativeWidth = settings_.swellRelativeWidth;
                s.swellSpreadExponent = settings_.swellSpreadExponent;
                s.windSeaVarianceWeight = windWeight;
                s.swellVarianceWeight = swellWeight;
                // 較正は呼び出し側で行うのでビルダー内の正規化は使わない
                s.targetRmsHeight = 0.0f;
                return s;
            };

        // 全カスケードを一度生成して総RMS（二乗和平方根）を返す。
        // カスケードは互いに素な波数帯 × 独立位相なので分散が加算される。
        const auto buildAndMeasure =
            [&](float windWeight, float swellWeight, float* outPerCascadeRms) {
                double sumOfSquares = 0.0;
                for (uint32_t c = 0; c < kCascadeCount; ++c) {
                    if (!MappedSpectrumSamples(c)) {
                        continue;
                    }
                    const FFTOceanSpectrumBuilder::Settings s =
                        makeBuilderSettings(c, windWeight, swellWeight);
                    const FFTOceanSpectrumBuilder::BuildStats stats =
                        FFTOceanSpectrumBuilder::BuildSpectrum(
                            s, MappedSpectrumSamples(c), static_cast<size_t>(sampleCount));
                    if (outPerCascadeRms) {
                        outPerCascadeRms[c] = stats.measuredRmsHeight;
                    }
                    sumOfSquares +=
                        static_cast<double>(stats.measuredRmsHeight) * stats.measuredRmsHeight;
                }
                return static_cast<float>(std::sqrt(sumOfSquares));
            };

        // ---- パス1: 風波のみの生RMS ----
        const float rawWindSeaRms = buildAndMeasure(1.0f, 0.0f, nullptr);
        // ---- パス2: うねりのみの生RMS ----
        const float rawSwellRms = swellActive ? buildAndMeasure(0.0f, 1.0f, nullptr) : 0.0f;

        // 成分ごとの分散重み。密度に掛かるので (目標RMS / 生RMS)² になる。
        // これで合成後の総RMSが sqrt(windTarget² + swellTarget²) に厳密に一致する。
        const float windSeaWeight = (rawWindSeaRms > 1.0e-9f)
            ? (windSeaTargetRms / rawWindSeaRms) * (windSeaTargetRms / rawWindSeaRms) : 0.0f;
        const float swellWeight = (rawSwellRms > 1.0e-9f)
            ? (swellTargetRms / rawSwellRms) * (swellTargetRms / rawSwellRms) : 0.0f;

        // ---- パス3: 較正済みの重みで本生成 ----
        float finalRmsHeight[kCascadeCount] = {};
        const float finalTotalRms = buildAndMeasure(windSeaWeight, swellWeight, finalRmsHeight);

        // 診断用のピーク波長 λp = 2πg/ωp²。ビルダーと同じ式でピーク角周波数を求める
        // （ここが帯 [60,521]m の中に入っていないと、支配波がカスケード0 から外れる）。
        const float peakAngularFrequency = (std::max)(
            22.0f * std::pow(
                gravityValue * gravityValue / (heightWindSpeed * settings_.fetchMeters), 1.0f / 3.0f),
            0.877f * gravityValue / heightWindSpeed);
        const float peakWaveLength =
            kTwoPiValue * gravityValue / (peakAngularFrequency * peakAngularFrequency);

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "FFTOceanManager: spectrum calibrated (JONSWAP). windSpeed={:.2f} fetch={:.0f}km windSeaLambda={:.1f}m windSeaHs={:.2f} | "
            "swell={} swellHs={:.2f} swellPeriod={:.1f}s swellLambda={:.1f}m | totalRms={:.3f} (target {:.3f}) "
            "cascadeRms=[{:.3f}, {:.3f}, {:.3f}] share=[{:.1f}%, {:.1f}%, {:.1f}%]",
            heightWindSpeed,
            settings_.fetchMeters / 1000.0f,
            peakWaveLength,
            significantWaveHeight,
            swellActive ? "on" : "off",
            settings_.swellHeightMeters,
            settings_.swellPeriodSeconds,
            swellWaveLength,
            finalTotalRms,
            std::sqrt(windSeaTargetRms * windSeaTargetRms + swellTargetRms * swellTargetRms),
            finalRmsHeight[0],
            finalRmsHeight[1],
            finalRmsHeight[2],
            100.0f * finalRmsHeight[0] / (std::max)(finalTotalRms, 1.0e-6f),
            100.0f * finalRmsHeight[1] / (std::max)(finalTotalRms, 1.0e-6f),
            100.0f * finalRmsHeight[2] / (std::max)(finalTotalRms, 1.0e-6f));

        spectrumBufferDirty_ = true;
    }

    void FFTOceanManager::UpdateSimulationConstants(uint32_t cascadeIndex, float timeSeconds)
    {
        if (!mappedSimulationConstants_ || cascadeIndex >= kCascadeCount) {
            return;
        }

        // カスケード固有の patchLength / 振幅を各スロットへ転送する。
        const UINT slotSize = Align256(sizeof(SimulationConstants));
        SimulationConstants* slot = reinterpret_cast<SimulationConstants*>(
            mappedSimulationConstants_ + static_cast<size_t>(slotSize) * cascadeIndex);
        slot->resolution = settings_.resolution;
        slot->activeComponentCount = (std::min)(settings_.activeComponentCount, kMaxSpectrumComponents);
        slot->patchLength = kCascadePatchLength[cascadeIndex];
        slot->timeSeconds = timeSeconds;
        slot->choppiness = settings_.choppiness;
        slot->gravity = settings_.gravity;
        // カスケード配分はスペクトル生成時の targetRmsHeight 正規化で織り込み済み。
        // ここはユーザー/プリセットの倍率（較正済み波高に対する相対値）だけを掛ける。
        slot->amplitudeScale = settings_.amplitudeScale;
    }

    D3D12_GPU_VIRTUAL_ADDRESS FFTOceanManager::GetSimulationConstantsAddress(uint32_t cascadeIndex) const
    {
        if (!simulationConstantsBuffer_ || cascadeIndex >= kCascadeCount) {
            return 0;
        }
        const UINT slotSize = Align256(sizeof(SimulationConstants));
        return simulationConstantsBuffer_->GetGPUVirtualAddress() + static_cast<UINT64>(slotSize) * cascadeIndex;
    }

    D3D12_GPU_VIRTUAL_ADDRESS FFTOceanManager::UpdateIFFTConstants(uint32_t stageIndex, bool isHorizontal, float normalizationScale)
    {
        if (!mappedIFFTConstantsData_ || !ifftConstantsBuffer_) {
            return 0;
        }

        if (ifftConstantsWriteIndex_ >= kMaxIFFTPassCount) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "FFTOceanManager: IFFT constant slot overflow. writeIndex={} maxPassCount={}",
                ifftConstantsWriteIndex_,
                kMaxIFFTPassCount);
            ifftConstantsWriteIndex_ = kMaxIFFTPassCount - 1;
        }

        // IFFTの1パス分定数をリングバッファ上に書き込み、GPUアドレスを返す。
        const UINT slotSize = Align256(sizeof(IFFTConstants));
        IFFTConstants* constants = reinterpret_cast<IFFTConstants*>(
            mappedIFFTConstantsData_ + static_cast<size_t>(slotSize) * ifftConstantsWriteIndex_);
        constants->resolution = settings_.resolution;
        constants->stageIndex = stageIndex;
        constants->isHorizontal = isHorizontal ? 1 : 0;
        constants->normalizationScale = normalizationScale;

        const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress =
            ifftConstantsBuffer_->GetGPUVirtualAddress() + static_cast<UINT64>(slotSize) * ifftConstantsWriteIndex_;
        ++ifftConstantsWriteIndex_;
        return gpuAddress;
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
            FFTOceanManagerLogHelper::LogSpectrumUpload(copiedBytes);
        }

        FFTOceanDispatchHelper::DispatchEvolutionPass(
            cmdList,
            spectrumPingPongA_,
            spectrumPingPongB_,
            evolutionPipeline_,
            spectrumBuffers_[cascadeIndex].srv,
            GetSimulationConstantsAddress(cascadeIndex),
            settings_.resolution);
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
                UpdateIFFTConstants(stageIndex, isHorizontal, normalizationScale);
            FFTOceanDispatchHelper::DispatchIFFTPass(
                cmdList,
                textures[readIndex],
                textures[writeIndex],
                ifftPipeline_,
                constantsAddress,
                settings_.resolution);
            std::swap(readIndex, writeIndex);
        };

        // 横方向IFFT。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            dispatchStage(stageIndex, true, 1.0f);
        }

        // 縦方向IFFT。最終ステージのみ1/N正規化を適用する。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            const float normalizationScale = (stageIndex + 1 == log2Resolution)
                ? 1.0f / static_cast<float>(settings_.resolution)
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
            settings_.resolution);
    }

    uint32_t FFTOceanManager::GetLog2Resolution() const
    {
        uint32_t log2Resolution = 0;
        uint32_t resolution = settings_.resolution;
        while (resolution > 1) {
            resolution >>= 1;
            ++log2Resolution;
        }
        return log2Resolution;
    }
}
