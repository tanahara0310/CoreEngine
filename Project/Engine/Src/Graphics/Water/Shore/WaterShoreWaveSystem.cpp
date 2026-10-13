#include "pch.h"
#include "WaterShoreWaveSystem.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        constexpr double kTwoPi = 6.283185307179586;
        constexpr float kMinPeriod = 1.0f;
        constexpr UINT kThreadGroupSize = 8;

        /// @brief シェーダー資源として読むときの状態（レイトレとピクセルシェーダーの両方）
        constexpr D3D12_RESOURCE_STATES kAnyShaderResource =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        /// @brief 波の時刻を折り返す周期の数
        constexpr double kPhaseWrapPeriods = 1024.0;
    }

    bool WaterShoreWaveSystem::Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorAllocator_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterShoreWaveSystem: initialization failed. dxCommon={} descriptorAllocator={}",
                dxCommon_ != nullptr, descriptorAllocator_ != nullptr);
            return false;
        }
        if (!CreatePipelines() || !CreateResources()) {
            return false;
        }
        isInitialized_ = true;
        return true;
    }

    void WaterShoreWaveSystem::SetSettings(const Settings& settings)
    {
        Settings next = settings;
        next.period = (std::max)(next.period, kMinPeriod);
        next.startDepth = (std::max)(next.startDepth, 0.1f);
        const float length = std::sqrt(next.direction[0] * next.direction[0] + next.direction[1] * next.direction[1]);
        if (length > 1.0e-4f) {
            next.direction[0] /= length;
            next.direction[1] /= length;
        } else {
            next.direction[0] = 0.0f;
            next.direction[1] = 1.0f;
        }

        // 到着時刻は周期・来始める水深・向きで決まるので、変わったら解き直す
        if (next.period != settings_.period || next.startDepth != settings_.startDepth
            || next.direction[0] != settings_.direction[0] || next.direction[1] != settings_.direction[1]
            || (next.enabled && !settings_.enabled)) {
            resetPending_ = true;
        }
        settings_ = next;
    }

    float WaterShoreWaveSystem::GetAngularFrequency() const
    {
        return static_cast<float>(kTwoPi / settings_.period);
    }

    float WaterShoreWaveSystem::ComputePhaseTime(double timeSeconds) const
    {
        return static_cast<float>(std::fmod(timeSeconds, kPhaseWrapPeriods * settings_.period));
    }

    bool WaterShoreWaveSystem::CreatePipelines()
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        struct Entry {
            CustomShaderPipeline* pipeline;
            const ICustomShaderProvider* provider;
            const char* name;
        };
        const Entry entries[] = {
            { &initPipeline_, &initShaderProvider_, "init" },
            { &solvePipeline_, &solveShaderProvider_, "solve" },
            { &finalizePipeline_, &finalizeShaderProvider_, "finalize" },
        };
        for (const Entry& entry : entries) {
            const bool built = entry.pipeline->Build(
                dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, *entry.provider);
            if (!built || !entry.pipeline->HasComputePSO()) {
                Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "WaterShoreWaveSystem: failed to build {} compute pipeline.", entry.name);
                return false;
            }
        }
        return true;
    }

    bool WaterShoreWaveSystem::CreateTexture(FFTOceanGpuTexture& texture, DXGI_FORMAT format, const char* name)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kResolution;
        desc.Height = kResolution;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            texture.Reset(
                ResourceFactory::CreateTextureResource(dxCommon_->GetDevice(), desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC s{};
        s.Format = format;
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Texture2D.MipLevels = 1;
        texture.srv = descriptorAllocator_->CreateSRV(texture.Get(), s, std::string(name) + "SRV");

        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = format;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        texture.uav = descriptorAllocator_->CreateUAV(texture.Get(), u, std::string(name) + "UAV");
        return true;
    }

    bool WaterShoreWaveSystem::CreateResources()
    {
        return CreateTexture(depth_, DXGI_FORMAT_R32_FLOAT, "WaterShoreWaveDepth")
            && CreateTexture(arrival_[0], DXGI_FORMAT_R32_FLOAT, "WaterShoreWaveArrival0")
            && CreateTexture(arrival_[1], DXGI_FORMAT_R32_FLOAT, "WaterShoreWaveArrival1")
            && CreateTexture(field_, DXGI_FORMAT_R32G32B32A32_FLOAT, "WaterShoreWaveField");
    }

    void WaterShoreWaveSystem::Dispatch(ID3D12GraphicsCommandList* cmdList, const Input& input)
    {
        if (!IsActive() || !cmdList || input.seabedHeightSRV.ptr == 0 || input.windowSize <= 0.0f) {
            return;
        }
        if (input.frameNumber == lastFrameNumber_) {
            return;
        }
        lastFrameNumber_ = input.frameNumber;

        const float texelSize = input.windowSize / static_cast<float>(kResolution);
        const UINT groups = (kResolution + kThreadGroupSize - 1) / kThreadGroupSize;

        // ---- 水深と到着時刻の初期値 ----
        uint32_t current = arrivalLatest_ ^ 1u;
        {
            InitConstants constants{};
            constants.windowOriginXZ[0] = input.windowOriginXZ[0];
            constants.windowOriginXZ[1] = input.windowOriginXZ[1];
            constants.windowSize = input.windowSize;
            constants.prevWindowOriginXZ[0] = prevWindowOriginXZ_[0];
            constants.prevWindowOriginXZ[1] = prevWindowOriginXZ_[1];
            constants.waterRestHeight = input.waterRestHeight;
            constants.startDepth = settings_.startDepth;
            constants.resetArrival = resetPending_ ? 1u : 0u;
            constants.direction[0] = settings_.direction[0];
            constants.direction[1] = settings_.direction[1];
            constants.angularFrequency = GetAngularFrequency();
            resetPending_ = false;
            prevWindowOriginXZ_[0] = input.windowOriginXZ[0];
            prevWindowOriginXZ_[1] = input.windowOriginXZ[1];

            Barrier::Transition(cmdList, depth_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier::Transition(cmdList, arrival_[current], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Barrier::Transition(cmdList, arrival_[arrivalLatest_], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            cmdList->SetPipelineState(initPipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(initPipeline_.GetComputeRootSignature());
            if (const int slot = initPipeline_.GetComputeRootParamIndex("gSeabedHeight"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), input.seabedHeightSRV);
            }
            if (const int slot = initPipeline_.GetComputeRootParamIndex("gPrevArrival"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), arrival_[arrivalLatest_].srv.gpuHandle);
            }
            if (const int slot = initPipeline_.GetComputeRootParamIndex("gDepthOutput"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), depth_.uav.gpuHandle);
            }
            if (const int slot = initPipeline_.GetComputeRootParamIndex("gArrivalOutput"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), arrival_[current].uav.gpuHandle);
            }
            if (const int slot = initPipeline_.GetComputeRootParamIndex("WaterShoreWaveInitConstants"); slot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(slot), dxCommon_->GetUploadRing().AllocateConstants(constants));
            }
            cmdList->Dispatch(groups, groups, 1);
            Barrier::Transition(cmdList, depth_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // ---- 到着時刻を解き進める（2 枚を交互に読み書きする）----
        {
            SolveConstants constants{};
            constants.texelSize = texelSize;
            constants.angularFrequency = GetAngularFrequency();
            constants.startDepth = settings_.startDepth;
            constants.windowOriginXZ[0] = input.windowOriginXZ[0];
            constants.windowOriginXZ[1] = input.windowOriginXZ[1];
            constants.direction[0] = settings_.direction[0];
            constants.direction[1] = settings_.direction[1];
            const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress = dxCommon_->GetUploadRing().AllocateConstants(constants);

            cmdList->SetPipelineState(solvePipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(solvePipeline_.GetComputeRootSignature());
            const int depthSlot = solvePipeline_.GetComputeRootParamIndex("gDepth");
            const int inSlot = solvePipeline_.GetComputeRootParamIndex("gArrivalIn");
            const int outSlot = solvePipeline_.GetComputeRootParamIndex("gArrivalOutput");
            const int constantsSlot = solvePipeline_.GetComputeRootParamIndex("WaterShoreWaveSolveConstants");
            for (uint32_t iteration = 0; iteration < kSolveIterationsPerFrame; ++iteration) {
                const uint32_t next = current ^ 1u;
                Barrier::Transition(cmdList, arrival_[current], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Barrier::Transition(cmdList, arrival_[next], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if (depthSlot >= 0) {
                    cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(depthSlot), depth_.srv.gpuHandle);
                }
                if (inSlot >= 0) {
                    cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(inSlot), arrival_[current].srv.gpuHandle);
                }
                if (outSlot >= 0) {
                    cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outSlot), arrival_[next].uav.gpuHandle);
                }
                if (constantsSlot >= 0) {
                    cmdList->SetComputeRootConstantBufferView(static_cast<UINT>(constantsSlot), constantsAddress);
                }
                cmdList->Dispatch(groups, groups, 1);
                current = next;
            }
            arrivalLatest_ = current;
            Barrier::Transition(cmdList, arrival_[current], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        // ---- 場にまとめる ----
        {
            FinalizeConstants constants{};
            constants.texelSize = texelSize;

            Barrier::Transition(cmdList, field_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmdList->SetPipelineState(finalizePipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(finalizePipeline_.GetComputeRootSignature());
            if (const int slot = finalizePipeline_.GetComputeRootParamIndex("gDepth"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), depth_.srv.gpuHandle);
            }
            if (const int slot = finalizePipeline_.GetComputeRootParamIndex("gArrival"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), arrival_[current].srv.gpuHandle);
            }
            if (const int slot = finalizePipeline_.GetComputeRootParamIndex("gFieldOutput"); slot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), field_.uav.gpuHandle);
            }
            if (const int slot = finalizePipeline_.GetComputeRootParamIndex("WaterShoreWaveFinalizeConstants"); slot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(slot), dxCommon_->GetUploadRing().AllocateConstants(constants));
            }
            cmdList->Dispatch(groups, groups, 1);
            Barrier::Transition(cmdList, field_, kAnyShaderResource);
        }
        fieldReady_ = true;
    }
}
