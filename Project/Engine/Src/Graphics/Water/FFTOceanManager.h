#pragma once

#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

#include <array>
#include <cstdint>
#include <vector>
#include <wrl.h>

namespace CoreEngine
{
    class DirectXCommon;
    class DescriptorManager;

    class FFTOceanManager
    {
    public:
        struct Settings {
            uint32_t resolution = 256;
            float patchLength = 96.0f;
            float amplitudeScale = 1.0f;
            float windDirection[2] = { 0.92f, 0.38f };
            float windSpeed = 24.0f;
            float choppiness = 1.35f;
            uint32_t activeComponentCount = 32;
            float gravity = 9.81f;
        };

        bool Initialize(DirectXCommon* dxCommon, DescriptorManager* descriptorManager);
        void Dispatch(ID3D12GraphicsCommandList* cmdList, float timeSeconds);
        void SetSettings(const Settings& settings);

        bool IsInitialized() const { return isInitialized_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetDisplacementSRVHandle() const { return displacementSrvHandle_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRVHandle() const { return normalSrvHandle_; }
        const Settings& GetSettings() const { return settings_; }

    private:
        static constexpr uint32_t kMaxSpectrumComponents = 64;
        static constexpr uint32_t kPingPongCount = 2;
        static constexpr uint32_t kMaxIFFTPassCount = 32;

        struct SpectrumSample {
            float h0[2] = {};
            float h0Minus[2] = {};
            float waveVector[2] = {};
            float angularFrequency = 0.0f;
            float directionalWeight = 0.0f;
            float padding[2] = {};
        };

        struct alignas(16) SimulationConstants {
            uint32_t resolution = 0;
            uint32_t activeComponentCount = 0;
            float patchLength = 1.0f;
            float timeSeconds = 0.0f;
            float choppiness = 1.0f;
            float gravity = 9.81f;
            float amplitudeScale = 1.0f;
            float padding = 0.0f;
        };

        struct IFFTConstants {
            uint32_t resolution = 0;
            uint32_t stageIndex = 0;
            uint32_t isHorizontal = 0;
            float normalizationScale = 1.0f;
            float padding[3] = {};
        };

        struct TimeEvolutionShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanTimeEvolution.CS.hlsl"; }
        };

        struct IFFTShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanIFFT.CS.hlsl"; }
        };

        struct FinalizeShaderProvider final : ICustomShaderProvider {
            std::wstring GetComputeShaderPath() const override { return L"FFTOceanFinalize.CS.hlsl"; }
        };

        bool CreatePipelines();
        bool CreateOutputTextures();
        bool CreateDebugReadbackBuffers();
        bool CreateSpectrumBuffer();
        bool CreateSimulationConstantBuffer();
        bool CreateIFFTConstantBuffer();
        bool CreateIntermediateTextures();
        void SanitizeSettings(Settings& settings) const;
        void BuildSpectrum();
        void UpdateSimulationConstants(float timeSeconds);
        D3D12_GPU_VIRTUAL_ADDRESS UpdateIFFTConstants(uint32_t stageIndex, bool isHorizontal, float normalizationScale);
        void DispatchEvolutionPass(ID3D12GraphicsCommandList* cmdList);
        void DispatchIFFTPass(
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
            float normalizationScale);
        uint32_t DispatchIFFTForTexture(
            ID3D12GraphicsCommandList* cmdList,
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount>& resources,
            std::array<D3D12_RESOURCE_STATES, kPingPongCount>& resourceStates,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& srvHandles,
            std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount>& uavHandles,
            uint32_t initialIndex);
        void DispatchFinalizePass(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* spectrumAResource,
            D3D12_RESOURCE_STATES& spectrumAState,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumASrv,
            ID3D12Resource* spectrumBResource,
            D3D12_RESOURCE_STATES& spectrumBState,
            D3D12_GPU_DESCRIPTOR_HANDLE spectrumBSrv);
        void LogPendingIFFTDebugReadback();
        void ScheduleIFFTDebugReadback(
            ID3D12GraphicsCommandList* cmdList,
            ID3D12Resource* spectrumAResource,
            D3D12_RESOURCE_STATES& spectrumAState,
            ID3D12Resource* spectrumBResource,
            D3D12_RESOURCE_STATES& spectrumBState);
        void LogPendingEvolutionDebugReadback();
        void ScheduleEvolutionDebugReadback(ID3D12GraphicsCommandList* cmdList);
        void LogPendingDebugReadback();
        void ScheduleDebugReadback(ID3D12GraphicsCommandList* cmdList);
        uint32_t GetLog2Resolution() const;

        DirectXCommon* dxCommon_ = nullptr;
        DescriptorManager* descriptorManager_ = nullptr;
        CustomShaderPipeline evolutionPipeline_{};
        CustomShaderPipeline ifftPipeline_{};
        CustomShaderPipeline finalizePipeline_{};
        TimeEvolutionShaderProvider timeEvolutionShaderProvider_{};
        IFFTShaderProvider ifftShaderProvider_{};
        FinalizeShaderProvider finalizeShaderProvider_{};
        Settings settings_{};
        bool isInitialized_ = false;

        Microsoft::WRL::ComPtr<ID3D12Resource> displacementTexture_;
        Microsoft::WRL::ComPtr<ID3D12Resource> normalTexture_;
        Microsoft::WRL::ComPtr<ID3D12Resource> displacementReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> normalReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> evolutionAReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> evolutionBReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> ifftAReadbackBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> ifftBReadbackBuffer_;
        D3D12_CPU_DESCRIPTOR_HANDLE displacementSrvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE displacementSrvHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE displacementUavCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE displacementUavHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE normalSrvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE normalSrvHandle_{};
        D3D12_CPU_DESCRIPTOR_HANDLE normalUavCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE normalUavHandle_{};
        D3D12_RESOURCE_STATES displacementState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_RESOURCE_STATES normalState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT displacementReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT normalReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT evolutionAReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT evolutionBReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT ifftAReadbackLayout_{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT ifftBReadbackLayout_{};
        UINT64 displacementReadbackBytes_ = 0;
        UINT64 normalReadbackBytes_ = 0;
        UINT64 evolutionAReadbackBytes_ = 0;
        UINT64 evolutionBReadbackBytes_ = 0;
        UINT64 ifftAReadbackBytes_ = 0;
        UINT64 ifftBReadbackBytes_ = 0;
        bool debugReadbackPending_ = false;
        bool evolutionDebugReadbackPending_ = false;
        bool ifftDebugReadbackPending_ = false;
        uint64_t debugReadbackSequence_ = 0;
        uint64_t evolutionDebugReadbackSequence_ = 0;
        uint64_t ifftDebugReadbackSequence_ = 0;

        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount> spectrumTextureA_;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kPingPongCount> spectrumTextureB_;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumASrvCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumASrvHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumAUavCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumAUavHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBSrvCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBSrvHandle_{};
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBUavCpuHandle_{};
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE, kPingPongCount> spectrumBUavHandle_{};
        std::array<D3D12_RESOURCE_STATES, kPingPongCount> spectrumAState_{};
        std::array<D3D12_RESOURCE_STATES, kPingPongCount> spectrumBState_{};

        Microsoft::WRL::ComPtr<ID3D12Resource> spectrumBuffer_;
        Microsoft::WRL::ComPtr<ID3D12Resource> spectrumUploadBuffer_;
        SpectrumSample* mappedSpectrumSamples_ = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE spectrumSrvCpuHandle_{};
        D3D12_GPU_DESCRIPTOR_HANDLE spectrumSrvHandle_{};
        D3D12_RESOURCE_STATES spectrumBufferState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        bool spectrumBufferDirty_ = false;

        Microsoft::WRL::ComPtr<ID3D12Resource> simulationConstantsBuffer_;
        SimulationConstants* mappedSimulationConstants_ = nullptr;
        Microsoft::WRL::ComPtr<ID3D12Resource> ifftConstantsBuffer_;
        uint8_t* mappedIFFTConstantsData_ = nullptr;
        uint32_t ifftConstantsWriteIndex_ = 0u;
    };
}
