#include "pch.h"
#include "FFTOceanManager.h"

#include <algorithm>
#include <cmath>

#include <string>

#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Water/FFTOceanDispatchHelper.h"
#include "Graphics/Water/FFTOceanManagerLogHelper.h"
#include "Graphics/Water/FFTOceanReadbackHelper.h"
#include "Graphics/Water/FFTOceanResourceFactory.h"
#include "Graphics/Water/FFTOceanSpectrumDebugHelper.h"
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
        // 全カスケードの格子軸が揃っていると、各カスケード固有のタイル周期が
        // 同じ向き・同じ位置で強め合い「格子状の同じパターン」として知覚される。
        // 格子を互いに回転させると残存周期が空間的に整列しなくなり視認できなくなる。
        // スペクトルの風向は逆回転で補正するため、波の進行方向はワールドで全カスケード共通のまま。
        constexpr float kCascadeRotCos[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_COS;
        constexpr float kCascadeRotSin[FFTOceanManager::kCascadeCount] = FFT_OCEAN_CASCADE_ROT_SIN;

        // カスケードごとの乱数シード。同一シードだと全カスケードの位相パターンが
        // 完全相関し「同じ波の配置が縮尺違いで繰り返される」自己相似模様になる。
        constexpr uint32_t kCascadeRandomSeed[FFTOceanManager::kCascadeCount] = {
            20260626u, 20260626u + 7919u, 20260626u + 2u * 7919u };

        // 各カスケードへ配分する波高RMSの比率。海洋の波高エネルギーは長波長側に
        // 集中するため、大パッチが全体波高を支配し、小パッチはさざ波の傾き
        // （きらめき・法線ディテール）として効く程度に抑える。
        constexpr float kCascadeRmsShare[FFTOceanManager::kCascadeCount] = { 1.0f, 0.35f, 0.12f };

        // 波高計算に使う実効風速の範囲 [m/s]。Pierson-Moskowitz の Hs ≈ 0.21 v²/g は
        // v² で成長するため、プリセットの強風（52m/s 等）をそのまま入れると
        // 有義波高 50m 超の非現実的な水の山になる。上限 32m/s（Hs ≈ 22m の猛烈な嵐）で飽和させる。
        constexpr float kMinHeightWindSpeed = 1.0f;
        constexpr float kMaxHeightWindSpeed = 32.0f;
    }

    bool FFTOceanManager::Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager)
    {
        // 外部依存を保持し、設定をGPU向けに正規化してから初期化を開始する。
        dxCommon_ = dxCommon;
        descriptorManager_ = descriptorManager;
        SanitizeSettings(settings_);

        if (!dxCommon_ || !descriptorManager_) {
            Logger::GetInstance().Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: initialization failed. dxCommon={} descriptorManager={}",
                dxCommon_ != nullptr,
                descriptorManager_ != nullptr);
            return false;
        }

        // 実行に必要なパイプライン/リソースを順番に構築する。
        if (!CreatePipelines()
            || !CreateOutputTextures()
            || !CreateFoamResources()
            || !CreateIntermediateTextures()
            || !CreateDebugReadbackBuffers()
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
            settings_.patchLength,
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
            sanitized.patchLength != settings_.patchLength ||
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
            dxCommon_->WaitForPreviousFrame();
        }

        settings_ = sanitized;

        // 波面が不連続に変わるため、蓄積済みの泡は次の泡パスで破棄する。
        foamResetPending_ = true;

        // 現在時刻を保ったままスペクトルと全カスケードの定数を作り直す。
        const float currentTime = mappedSimulationConstants_
            ? reinterpret_cast<const SimulationConstants*>(mappedSimulationConstants_)->timeSeconds
            : 0.0f;
        BuildSpectrum();
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            UpdateSimulationConstants(c, currentTime);
        }

        FFTOceanManagerLogHelper::LogSettingsUpdated(
            settings_.patchLength,
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

    void FFTOceanManager::Dispatch(ID3D12GraphicsCommandList* cmdList, float timeSeconds)
    {
        if (!isInitialized_ || !cmdList) {
            return;
        }

        if (!evolutionPipeline_.HasComputePSO() || !ifftPipeline_.HasComputePSO() || !finalizePipeline_.HasComputePSO()) {
            return;
        }

        // フレーム時刻・パッチ長を全カスケードのスロットへ書き込む。
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            UpdateSimulationConstants(c, timeSeconds);
        }
        ifftConstantsWriteIndex_ = 0;
        LogPendingIFFTDebugReadback();
        LogPendingEvolutionDebugReadback();
        LogPendingDebugReadback();

        const SimulationConstants* slot0 =
            mappedSimulationConstants_ ? reinterpret_cast<const SimulationConstants*>(mappedSimulationConstants_) : nullptr;

        static uint32_t sDispatchLogCounter = 0;
        if ((sDispatchLogCounter++ % 120) == 0 && slot0) {
            static float sPreviousLoggedTime = 0.0f;
            const float loggedDelta = timeSeconds - sPreviousLoggedTime;
            sPreviousLoggedTime = timeSeconds;

            FFTOceanManagerLogHelper::LogDispatchSummary(
                timeSeconds,
                loggedDelta,
                slot0->timeSeconds,
                slot0->amplitudeScale,
                settings_.windSpeed,
                slot0->choppiness,
                slot0->activeComponentCount);

            if (mappedSpectrumSamples_[0]) {
                const uint32_t probeX = settings_.resolution / 2 + 1;
                const uint32_t probeY = settings_.resolution / 2;
                const uint32_t probeIndex = probeY * settings_.resolution + probeX;
                const SpectrumSample& probeSample = mappedSpectrumSamples_[0][probeIndex];
                const FFTOceanSpectrumDebugHelper::ComplexValue probeHeight =
                    FFTOceanSpectrumDebugHelper::EvaluateSpectrumSample(
                        probeSample.h0,
                        probeSample.h0Minus,
                        probeSample.angularFrequency,
                        probeSample.directionalWeight,
                        timeSeconds,
                        slot0->amplitudeScale,
                        slot0->activeComponentCount);

                FFTOceanManagerLogHelper::LogProbeSpectrum(
                    probeIndex,
                    probeSample.waveVector[0],
                    probeSample.waveVector[1],
                    probeSample.angularFrequency,
                    probeHeight.real,
                    probeHeight.imag,
                    FFTOceanSpectrumDebugHelper::ComputeMagnitude(probeHeight),
                    probeSample.directionalWeight);
            }
        }

        // 出力配列テクスチャ（全スライス）をUAVへ遷移する。
        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, jacobianTexture_.Get(), jacobianState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        ID3D12DescriptorHeap* descriptorHeaps[] = { descriptorManager_->GetSRVHeap() };
        cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        // カスケードごとに 時間発展 -> IFFT(2系統) -> 最終合成（スライスc へ書き込み）を実行する。
        // 中間ピンポンテクスチャ／IFFT定数リングはカスケード間で共有する（逐次実行）。
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            DispatchEvolutionPass(cmdList, c);

            if (c == 0) {
                static uint32_t sEvolutionReadbackCounter = 0;
                if ((sEvolutionReadbackCounter++ % 120) == 0) {
                    ScheduleEvolutionDebugReadback(cmdList);
                }
            }

            const uint32_t finalSpectrumAIndex = DispatchIFFTForTexture(
                cmdList,
                spectrumTextureA_,
                spectrumAState_,
                spectrumASrvHandle_,
                spectrumAUavHandle_,
                0);

            const uint32_t finalSpectrumBIndex = DispatchIFFTForTexture(
                cmdList,
                spectrumTextureB_,
                spectrumBState_,
                spectrumBSrvHandle_,
                spectrumBUavHandle_,
                0);

            if (c == 0) {
                static uint32_t sIfftLogCounter = 0;
                if ((sIfftLogCounter++ % 120) == 0) {
                    FFTOceanManagerLogHelper::LogIFFTCompleted(
                        finalSpectrumAIndex,
                        finalSpectrumBIndex,
                        settings_.resolution,
                        GetLog2Resolution());

                    ScheduleIFFTDebugReadback(
                        cmdList,
                        spectrumTextureA_[finalSpectrumAIndex].Get(),
                        spectrumAState_[finalSpectrumAIndex],
                        spectrumTextureB_[finalSpectrumBIndex].Get(),
                        spectrumBState_[finalSpectrumBIndex]);
                }
            }

            DispatchFinalizePass(
                cmdList,
                c,
                spectrumTextureA_[finalSpectrumAIndex].Get(),
                spectrumAState_[finalSpectrumAIndex],
                spectrumASrvHandle_[finalSpectrumAIndex],
                spectrumTextureB_[finalSpectrumBIndex].Get(),
                spectrumBState_[finalSpectrumBIndex],
                spectrumBSrvHandle_[finalSpectrumBIndex]);

            // 次カスケードが中間ピンポンを書き換える前に、Finalizeの読み取り完了を保証する。
            ResourceBarrierHelper::UAV(cmdList, displacementTexture_.Get());
            ResourceBarrierHelper::UAV(cmdList, normalTexture_.Get());
            ResourceBarrierHelper::UAV(cmdList, jacobianTexture_.Get());
        }

        // 後段シェーダー参照用にSRV状態へ戻す（法線ミップ連鎖は廃止済み）。
        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, jacobianTexture_.Get(), jacobianState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // 泡の蓄積・減衰（ヤコビアンが SRV 状態になった後に実行する）
        DispatchFoamPass(cmdList, timeSeconds);

        static uint32_t sDebugReadbackCounter = 0;
        if ((sDebugReadbackCounter++ % 120) == 0) {
            ScheduleDebugReadback(cmdList);
        }
    }

    bool FFTOceanManager::CreateFoamResources()
    {
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorManager_) {
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
                foamTexture_[i] = ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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
            descriptorManager_->CreateSRV(foamTexture_[i].Get(), s, foamSrvCpuHandle_[i], foamSrvHandle_[i], "FFTOceanFoamSRV_" + idx);

            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = format;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            u.Texture2DArray.MipSlice = 0;
            u.Texture2DArray.FirstArraySlice = 0;
            u.Texture2DArray.ArraySize = kCascadeCount;
            descriptorManager_->CreateUAV(foamTexture_[i].Get(), u, foamUavCpuHandle_[i], foamUavHandle_[i], "FFTOceanFoamUAV_" + idx);

            foamState_[i] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
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
        ResourceBarrierHelper::Transition(
            cmdList, foamTexture_[writeIndex].Get(), foamState_[writeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(
            cmdList, foamTexture_[readIndex].Get(), foamState_[readIndex],
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        cmdList->SetPipelineState(foamPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(foamPipeline_.GetComputeRootSignature());

        const int jacobianSlot = foamPipeline_.GetComputeRootParamIndex("gJacobian");
        if (jacobianSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(jacobianSlot), jacobianSrvHandle_);
        }
        const int prevSlot = foamPipeline_.GetComputeRootParamIndex("gFoamPrev");
        if (prevSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(prevSlot), foamSrvHandle_[readIndex]);
        }
        const int outputSlot = foamPipeline_.GetComputeRootParamIndex("gFoamOutput");
        if (outputSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outputSlot), foamUavHandle_[writeIndex]);
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
        ResourceBarrierHelper::Transition(
            cmdList, foamTexture_[writeIndex].Get(), foamState_[writeIndex],
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
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorManager_) {
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

        try {
            displacementTexture_ = ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            normalTexture_ = ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            jacobianTexture_ = ResourceFactory::CreateTextureResource(device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            return false;
        }

        // 配列SRV（全スライスを1ビューで見せる。名前ベースのバインドで水面/RTが参照）。
        auto createArraySrv = [&](ID3D12Resource* tex,
            D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, const std::string& name) {
            D3D12_SHADER_RESOURCE_VIEW_DESC s{};
            s.Format = format;
            s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s.Texture2DArray.MostDetailedMip = 0;
            s.Texture2DArray.MipLevels = 1;
            s.Texture2DArray.FirstArraySlice = 0;
            s.Texture2DArray.ArraySize = kCascadeCount;
            descriptorManager_->CreateSRV(tex, s, cpu, gpu, name);
        };
        createArraySrv(displacementTexture_.Get(), displacementSrvCpuHandle_, displacementSrvHandle_, "FFTOceanDisplacementArraySRV");
        createArraySrv(normalTexture_.Get(), normalSrvCpuHandle_, normalSrvHandle_, "FFTOceanNormalArraySRV");
        createArraySrv(jacobianTexture_.Get(), jacobianSrvCpuHandle_, jacobianSrvHandle_, "FFTOceanJacobianArraySRV");

        // スライス単位のUAV（Finalizeが各カスケードのスライスへ書き込む）。
        auto createSliceUav = [&](ID3D12Resource* tex, uint32_t slice,
            D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu, const std::string& name) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
            u.Format = format;
            u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            u.Texture2DArray.MipSlice = 0;
            u.Texture2DArray.FirstArraySlice = slice;
            u.Texture2DArray.ArraySize = 1;
            descriptorManager_->CreateUAV(tex, u, cpu, gpu, name);
        };
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            const std::string idx = std::to_string(c);
            createSliceUav(displacementTexture_.Get(), c, displacementUavCpuHandle_[c], displacementUavHandle_[c], "FFTOceanDisplacementUAV_" + idx);
            createSliceUav(normalTexture_.Get(), c, normalUavCpuHandle_[c], normalUavHandle_[c], "FFTOceanNormalUAV_" + idx);
            createSliceUav(jacobianTexture_.Get(), c, jacobianUavCpuHandle_[c], jacobianUavHandle_[c], "FFTOceanJacobianUAV_" + idx);
        }

        displacementState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        normalState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        jacobianState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        return true;
    }

    bool FFTOceanManager::CreateDebugReadbackBuffers()
    {
        if (!dxCommon_ || !dxCommon_->GetDevice() || !displacementTexture_ || !normalTexture_) {
            return false;
        }

        const FFTOceanReadbackTextureCreateRequest requests[] = {
            { dxCommon_->GetDevice(), displacementTexture_.Get(), "displacement", &displacementReadbackBuffer_, &displacementReadbackLayout_, &displacementReadbackBytes_ },
            { dxCommon_->GetDevice(), normalTexture_.Get(), "normal", &normalReadbackBuffer_, &normalReadbackLayout_, &normalReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureA_[0].Get(), "evolutionA", &evolutionAReadbackBuffer_, &evolutionAReadbackLayout_, &evolutionAReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureB_[0].Get(), "evolutionB", &evolutionBReadbackBuffer_, &evolutionBReadbackLayout_, &evolutionBReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureA_[0].Get(), "ifftA", &ifftAReadbackBuffer_, &ifftAReadbackLayout_, &ifftAReadbackBytes_ },
            { dxCommon_->GetDevice(), spectrumTextureB_[0].Get(), "ifftB", &ifftBReadbackBuffer_, &ifftBReadbackLayout_, &ifftBReadbackBytes_ },
        };

        for (const FFTOceanReadbackTextureCreateRequest& request : requests) {
            if (!FFTOceanReadbackHelper::CreateTextureReadbackBuffer(request)) {
                return false;
            }
        }

        return true;
    }

    bool FFTOceanManager::CreateIntermediateTextures()
    {
        return FFTOceanResourceFactory::CreateIntermediateTextures(
            dxCommon_->GetDevice(),
            descriptorManager_,
            settings_.resolution,
            spectrumTextureA_,
            spectrumTextureB_,
            spectrumASrvCpuHandle_,
            spectrumASrvHandle_,
            spectrumAUavCpuHandle_,
            spectrumAUavHandle_,
            spectrumBSrvCpuHandle_,
            spectrumBSrvHandle_,
            spectrumBUavCpuHandle_,
            spectrumBUavHandle_,
            spectrumAState_,
            spectrumBState_);
    }

    bool FFTOceanManager::CreateSpectrumBuffer()
    {
        // 初期スペクトル（h0）はパッチ長依存のため、カスケードごとに独立したバッファを作る。
        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            void* mappedSpectrumSamples = nullptr;
            bool dirty = false;
            const bool created = FFTOceanResourceFactory::CreateSpectrumBuffers(
                dxCommon_->GetDevice(),
                descriptorManager_,
                settings_.resolution,
                sizeof(SpectrumSample),
                spectrumBuffer_[c],
                spectrumUploadBuffer_[c],
                mappedSpectrumSamples,
                spectrumSrvCpuHandle_[c],
                spectrumSrvHandle_[c],
                spectrumBufferState_[c],
                dirty);
            if (!created) {
                return false;
            }
            mappedSpectrumSamples_[c] = reinterpret_cast<SpectrumSample*>(mappedSpectrumSamples);
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
        builderSettings.patchLength = settings.patchLength;
        builderSettings.amplitudeScale = settings.amplitudeScale;
        builderSettings.windDirection[0] = settings.windDirection[0];
        builderSettings.windDirection[1] = settings.windDirection[1];
        builderSettings.windSpeed = settings.windSpeed;
        builderSettings.choppiness = settings.choppiness;
        builderSettings.activeComponentCount = settings.activeComponentCount;
        builderSettings.gravity = settings.gravity;

        FFTOceanSpectrumBuilder::SanitizeSettings(builderSettings, kMaxSpectrumComponents);

        settings.resolution = builderSettings.resolution;
        settings.patchLength = builderSettings.patchLength;
        settings.amplitudeScale = builderSettings.amplitudeScale;
        settings.windDirection[0] = builderSettings.windDirection[0];
        settings.windDirection[1] = builderSettings.windDirection[1];
        settings.windSpeed = builderSettings.windSpeed;
        settings.choppiness = builderSettings.choppiness;
        settings.activeComponentCount = builderSettings.activeComponentCount;
        settings.gravity = builderSettings.gravity;
    }

    void FFTOceanManager::BuildSpectrum()
    {
        // カスケードごとに固有のパッチ長で Phillips スペクトルを生成する。
        const uint32_t resolution = settings_.resolution;
        const uint32_t sampleCount = resolution * resolution;

        // 波高の物理較正: Pierson-Moskowitz の有義波高 Hs ≈ 0.21 v²/g から
        // 目標RMS = Hs/4 を求め、カスケードごとの配分比で分ける。
        // これにより波高は「風速だけ」で決まり、パッチ長には依存しない
        // （素の Phillips 離散和は波高が patchLength × v² で発散する）。
        const float heightWindSpeed = (std::clamp)(settings_.windSpeed, kMinHeightWindSpeed, kMaxHeightWindSpeed);
        const float significantWaveHeight = 0.21f * heightWindSpeed * heightWindSpeed / (std::max)(settings_.gravity, 0.1f);
        const float baseTargetRms = significantWaveHeight * 0.25f;

        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            if (!mappedSpectrumSamples_[c]) {
                continue;
            }

            FFTOceanSpectrumBuilder::Settings builderSettings{};
            builderSettings.resolution = settings_.resolution;
            builderSettings.patchLength = kCascadePatchLength[c];
            builderSettings.amplitudeScale = settings_.amplitudeScale;
            // サンプリング格子がカスケードごとに回転しているため、スペクトルの風向は
            // テクスチャ座標系（＝回転後の格子系）へ順回転して渡す。シェーダ側で
            // 変位・法線を逆回転してワールドへ戻すので、波の進行方向は全カスケードで一致する。
            builderSettings.windDirection[0] =
                kCascadeRotCos[c] * settings_.windDirection[0] - kCascadeRotSin[c] * settings_.windDirection[1];
            builderSettings.windDirection[1] =
                kCascadeRotSin[c] * settings_.windDirection[0] + kCascadeRotCos[c] * settings_.windDirection[1];
            builderSettings.windSpeed = settings_.windSpeed;
            builderSettings.choppiness = settings_.choppiness;
            builderSettings.activeComponentCount = settings_.activeComponentCount;
            builderSettings.gravity = settings_.gravity;
            builderSettings.randomSeed = kCascadeRandomSeed[c];
            builderSettings.targetRmsHeight = baseTargetRms * kCascadeRmsShare[c];

            FFTOceanSpectrumBuilder::BuildStats stats = FFTOceanSpectrumBuilder::BuildSpectrum(
                builderSettings,
                mappedSpectrumSamples_[c],
                static_cast<size_t>(sampleCount));

            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "FFTOceanManager: BuildSpectrum cascade={} patchLength={:.1f} resolution={} activeSamples={} targetRms={:.3f} rawRms={:.3f} heightScale={:.6f} maxAmp={:.4f}",
                c,
                kCascadePatchLength[c],
                resolution,
                stats.activeSpectrumSampleCount,
                builderSettings.targetRmsHeight,
                stats.measuredRmsHeight,
                stats.appliedHeightScale,
                stats.maxSpectralAmplitude);
        }

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
                if (!spectrumBuffer_[c] || !spectrumUploadBuffer_[c]) {
                    continue;
                }
                ResourceBarrierHelper::Transition(
                    cmdList, spectrumBuffer_[c].Get(), spectrumBufferState_[c],
                    D3D12_RESOURCE_STATE_COPY_DEST);
                cmdList->CopyResource(spectrumBuffer_[c].Get(), spectrumUploadBuffer_[c].Get());
                ResourceBarrierHelper::Transition(
                    cmdList, spectrumBuffer_[c].Get(), spectrumBufferState_[c],
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                copiedBytes += spectrumUploadBuffer_[c]->GetDesc().Width;
            }
            FFTOceanManagerLogHelper::LogSpectrumUpload(copiedBytes);
        }

        FFTOceanDispatchHelper::DispatchEvolutionPass(
            cmdList,
            spectrumTextureA_,
            spectrumAState_,
            spectrumTextureB_,
            spectrumBState_,
            evolutionPipeline_,
            spectrumSrvHandle_[cascadeIndex],
            spectrumAUavHandle_[0],
            spectrumBUavHandle_[0],
            GetSimulationConstantsAddress(cascadeIndex),
            settings_.resolution);
    }

    void FFTOceanManager::DispatchIFFTPass(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* inputResource,
        D3D12_RESOURCE_STATES& inputState,
        CustomShaderPipeline& pipeline,
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
        ID3D12Resource* outputResource,
        D3D12_RESOURCE_STATES& outputState,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUav,
        uint32_t stageIndex,
        bool isHorizontal,
        float normalizationScale)
    {
        const D3D12_GPU_VIRTUAL_ADDRESS ifftConstantsGpuAddress =
            UpdateIFFTConstants(stageIndex, isHorizontal, normalizationScale);

        FFTOceanDispatchHelper::DispatchIFFTPass(
            cmdList,
            inputResource,
            inputState,
            pipeline,
            inputSrv,
            outputResource,
            outputState,
            outputUav,
            ifftConstantsGpuAddress,
            settings_.resolution);
    }

    uint32_t FFTOceanManager::DispatchIFFTForTexture(
        ID3D12GraphicsCommandList* cmdList,
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount>& resources,
        std::array<D3D12_RESOURCE_STATES, kPingPongCount>& resourceStates,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& srvHandles,
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& uavHandles,
        uint32_t initialIndex)
    {
        uint32_t readIndex = initialIndex;
        uint32_t writeIndex = (initialIndex + 1) % kPingPongCount;
        const uint32_t log2Resolution = GetLog2Resolution();

        // 横方向IFFT。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            const float normalizationScale = 1.0f;
            DispatchIFFTPass(
                cmdList,
                resources[readIndex].Get(),
                resourceStates[readIndex],
                ifftPipeline_,
                srvHandles[readIndex],
                resources[writeIndex].Get(),
                resourceStates[writeIndex],
                uavHandles[writeIndex],
                stageIndex,
                true,
                normalizationScale);
            std::swap(readIndex, writeIndex);
        }

        // 縦方向IFFT。最終ステージのみ1/N正規化を適用する。
        for (uint32_t stageIndex = 0; stageIndex < log2Resolution; ++stageIndex) {
            const float normalizationScale = (stageIndex + 1 == log2Resolution)
                ? 1.0f / static_cast<float>(settings_.resolution)
                : 1.0f;
            DispatchIFFTPass(
                cmdList,
                resources[readIndex].Get(),
                resourceStates[readIndex],
                ifftPipeline_,
                srvHandles[readIndex],
                resources[writeIndex].Get(),
                resourceStates[writeIndex],
                uavHandles[writeIndex],
                stageIndex,
                false,
                normalizationScale);
            std::swap(readIndex, writeIndex);
        }

        return readIndex;
    }

    void FFTOceanManager::DispatchFinalizePass(
        ID3D12GraphicsCommandList* cmdList,
        uint32_t cascadeIndex,
        ID3D12Resource* spectrumAResource,
        D3D12_RESOURCE_STATES& spectrumAState,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumASrv,
        ID3D12Resource* spectrumBResource,
        D3D12_RESOURCE_STATES& spectrumBState,
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumBSrv)
    {
        if (cascadeIndex >= kCascadeCount) {
            return;
        }

        FFTOceanDispatchHelper::DispatchFinalizePass(
            cmdList,
            spectrumAResource,
            spectrumAState,
            spectrumASrv,
            spectrumBResource,
            spectrumBState,
            spectrumBSrv,
            finalizePipeline_,
            displacementUavHandle_[cascadeIndex],
            normalUavHandle_[cascadeIndex],
            jacobianUavHandle_[cascadeIndex],
            GetSimulationConstantsAddress(cascadeIndex),
            settings_.resolution);
    }

    void FFTOceanManager::LogPendingDebugReadback()
    {
        if (!debugReadbackPending_ || !displacementReadbackBuffer_ || !normalReadbackBuffer_) {
            return;
        }

        const FFTOceanReadbackHelper::SurfaceReadbackRequest request{
            displacementReadbackBuffer_.Get(),
            normalReadbackBuffer_.Get(),
            displacementReadbackBytes_,
            normalReadbackBytes_,
            displacementReadbackLayout_,
            normalReadbackLayout_,
            settings_.resolution,
            debugReadbackSequence_ };

        if (FFTOceanReadbackHelper::TryLogSurfaceReadback(request)) {
            debugReadbackPending_ = false;
        }
    }

    void FFTOceanManager::LogPendingEvolutionDebugReadback()
    {
        if (!evolutionDebugReadbackPending_ || !evolutionAReadbackBuffer_ || !evolutionBReadbackBuffer_) {
            return;
        }

        const FFTOceanReadbackHelper::SpectrumReadbackRequest request{
            evolutionAReadbackBuffer_.Get(),
            evolutionBReadbackBuffer_.Get(),
            evolutionAReadbackBytes_,
            evolutionBReadbackBytes_,
            evolutionAReadbackLayout_,
            evolutionBReadbackLayout_,
            settings_.resolution,
            evolutionDebugReadbackSequence_,
            false };

        if (FFTOceanReadbackHelper::TryLogSpectrumReadback(request)) {
            evolutionDebugReadbackPending_ = false;
        }
    }

    void FFTOceanManager::LogPendingIFFTDebugReadback()
    {
        if (!ifftDebugReadbackPending_ || !ifftAReadbackBuffer_ || !ifftBReadbackBuffer_) {
            return;
        }

        const FFTOceanReadbackHelper::SpectrumReadbackRequest request{
            ifftAReadbackBuffer_.Get(),
            ifftBReadbackBuffer_.Get(),
            ifftAReadbackBytes_,
            ifftBReadbackBytes_,
            ifftAReadbackLayout_,
            ifftBReadbackLayout_,
            settings_.resolution,
            ifftDebugReadbackSequence_,
            true };

        if (FFTOceanReadbackHelper::TryLogSpectrumReadback(request)) {
            ifftDebugReadbackPending_ = false;
        }
    }

    void FFTOceanManager::ScheduleIFFTDebugReadback(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* spectrumAResource,
        D3D12_RESOURCE_STATES& spectrumAState,
        ID3D12Resource* spectrumBResource,
        D3D12_RESOURCE_STATES& spectrumBState)
    {
        if (!cmdList || !ifftAReadbackBuffer_ || !ifftBReadbackBuffer_ || !spectrumAResource || !spectrumBResource) {
            return;
        }

        // IFFT完了後のスペクトルをReadbackバッファへコピーして次フレームで解析する。
        D3D12_TEXTURE_COPY_LOCATION srcA{};
        srcA.pResource = spectrumAResource;
        srcA.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcA.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstA{};
        dstA.pResource = ifftAReadbackBuffer_.Get();
        dstA.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstA.PlacedFootprint = ifftAReadbackLayout_;

        D3D12_TEXTURE_COPY_LOCATION srcB{};
        srcB.pResource = spectrumBResource;
        srcB.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcB.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstB{};
        dstB.pResource = ifftBReadbackBuffer_.Get();
        dstB.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstB.PlacedFootprint = ifftBReadbackLayout_;

        ResourceBarrierHelper::Transition(cmdList, spectrumAResource, spectrumAState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumBResource, spectrumBState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dstA, 0, 0, 0, &srcA, nullptr);
        cmdList->CopyTextureRegion(&dstB, 0, 0, 0, &srcB, nullptr);
        ResourceBarrierHelper::Transition(cmdList, spectrumAResource, spectrumAState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumBResource, spectrumBState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        ifftDebugReadbackPending_ = true;
        ++ifftDebugReadbackSequence_;
    }

    void FFTOceanManager::ScheduleEvolutionDebugReadback(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !evolutionAReadbackBuffer_ || !evolutionBReadbackBuffer_) {
            return;
        }

        // 時間発展直後の中間スペクトルをReadbackする。
        D3D12_TEXTURE_COPY_LOCATION srcA{};
        srcA.pResource = spectrumTextureA_[0].Get();
        srcA.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcA.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstA{};
        dstA.pResource = evolutionAReadbackBuffer_.Get();
        dstA.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstA.PlacedFootprint = evolutionAReadbackLayout_;

        D3D12_TEXTURE_COPY_LOCATION srcB{};
        srcB.pResource = spectrumTextureB_[0].Get();
        srcB.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcB.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dstB{};
        dstB.pResource = evolutionBReadbackBuffer_.Get();
        dstB.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dstB.PlacedFootprint = evolutionBReadbackLayout_;

        ResourceBarrierHelper::Transition(cmdList, spectrumTextureA_[0].Get(), spectrumAState_[0], D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, spectrumTextureB_[0].Get(), spectrumBState_[0], D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dstA, 0, 0, 0, &srcA, nullptr);
        cmdList->CopyTextureRegion(&dstB, 0, 0, 0, &srcB, nullptr);
        ResourceBarrierHelper::Transition(cmdList, spectrumTextureA_[0].Get(), spectrumAState_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, spectrumTextureB_[0].Get(), spectrumBState_[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        evolutionDebugReadbackPending_ = true;
        ++evolutionDebugReadbackSequence_;
    }

    void FFTOceanManager::ScheduleDebugReadback(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !displacementReadbackBuffer_ || !normalReadbackBuffer_) {
            return;
        }

        // 最終出力(変位/法線)をReadbackして統計ログに利用する。
        D3D12_TEXTURE_COPY_LOCATION displacementSrc{};
        displacementSrc.pResource = displacementTexture_.Get();
        displacementSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        displacementSrc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION displacementDst{};
        displacementDst.pResource = displacementReadbackBuffer_.Get();
        displacementDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        displacementDst.PlacedFootprint = displacementReadbackLayout_;

        D3D12_TEXTURE_COPY_LOCATION normalSrc{};
        normalSrc.pResource = normalTexture_.Get();
        normalSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        normalSrc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION normalDst{};
        normalDst.pResource = normalReadbackBuffer_.Get();
        normalDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        normalDst.PlacedFootprint = normalReadbackLayout_;

        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&displacementDst, 0, 0, 0, &displacementSrc, nullptr);
        cmdList->CopyTextureRegion(&normalDst, 0, 0, 0, &normalSrc, nullptr);
        ResourceBarrierHelper::Transition(cmdList, displacementTexture_.Get(), displacementState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, normalTexture_.Get(), normalState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        debugReadbackPending_ = true;
        ++debugReadbackSequence_;
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
