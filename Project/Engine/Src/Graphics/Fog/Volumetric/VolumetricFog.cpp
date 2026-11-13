#include "pch.h"
#include "Graphics/Fog/Volumetric/VolumetricFog.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Fog/Settings/FogSettings.h"
#include "Graphics/Fog/Shader/VolumetricFogBindings.h"
#include "Graphics/Fog/Volumetric/VolumetricFogCVars.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ICustomShaderProvider.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <exception>
#include <span>

namespace CoreEngine
{
    namespace
    {
        /// @brief ファイル名で CS を 1 本指すシェーダー供給
        class ComputeShaderProvider final : public ICustomShaderProvider {
        public:
            explicit ComputeShaderProvider(const wchar_t* path) : path_(path) {}
            std::wstring GetComputeShaderPath() const override { return path_; }

        private:
            const wchar_t* path_;
        };

        constexpr uint32_t kCellGroupSize = 4;
        constexpr uint32_t kColumnGroupSize = 8;

        /// @brief CS を 1 本組み、契約からバインディング表を作る
        bool BuildCompute(
            ID3D12Device* device, ShaderCompiler& compiler, ShaderReflectionBuilder& reflectionBuilder,
            const wchar_t* path, const char* label, std::span<const ShaderBindingDecl> decls,
            CustomShaderPipeline& pipeline, BindingTable& bindings)
        {
            const ComputeShaderProvider provider(path);
            if (!pipeline.Build(device, compiler, reflectionBuilder, provider) || !pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "VolumetricFog: %s のコンピュートパイプラインの構築に失敗", label);
                return false;
            }
            const ShaderReflectionData* reflection = pipeline.GetComputeReflection();
            if (!reflection) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "VolumetricFog: %s のリフレクションを取得できません", label);
                return false;
            }
            try {
                bindings = BindingTable::Resolve(*reflection, decls.data(), decls.size(), label);
            }
            catch (const std::exception&) {
                return false;
            }
            return true;
        }

        uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    }

    bool VolumetricFog::Initialize(GraphicsCore* graphicsCore)
    {
        graphicsCore_ = graphicsCore;
        if (!graphicsCore_ || !graphicsCore_->GetDevice()) {
            return false;
        }
        ID3D12Device* device = graphicsCore_->GetDevice();

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();
        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        pipelineReady_ =
            BuildCompute(device, shaderCompiler, reflectionBuilder, L"VolumetricFogInject.CS.hlsl",
                "VolumetricFogInject", VolumetricFogInjectBind::kDecls,
                injectPipeline_, injectBindings_)
            && BuildCompute(device, shaderCompiler, reflectionBuilder, L"VolumetricFogLightScattering.CS.hlsl",
                "VolumetricFogLightScattering", VolumetricFogLightScatteringBind::kDecls,
                lightScatteringPipeline_, lightScatteringBindings_)
            && BuildCompute(device, shaderCompiler, reflectionBuilder, L"VolumetricFogIntegrate.CS.hlsl",
                "VolumetricFogIntegrate", VolumetricFogIntegrateBind::kDecls,
                integratePipeline_, integrateBindings_);
        return pipelineReady_;
    }

    bool VolumetricFog::CreateVolume(
        Volume& volume, uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ, const char* name)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        desc.Width = sizeX;
        desc.Height = sizeY;
        desc.DepthOrArraySize = static_cast<UINT16>(sizeZ);
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            const Microsoft::WRL::ComPtr<ID3D12Device> device = graphicsCore_->GetDevice();
            volume.texture.Reset(ResourceFactory::CreateTextureResource(
                device, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricFog: %s の 3D テクスチャの生成に失敗", name);
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture3D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = sizeZ;

        DescriptorAllocator* allocator = graphicsCore_->GetDescriptorAllocator();
        volume.srv = allocator->CreateSRV(volume.texture.Get(), srvDesc, name);
        volume.uav = allocator->CreateUAV(volume.texture.Get(), uavDesc, name);
        return volume.srv.IsValid() && volume.uav.IsValid();
    }

    void VolumetricFog::RetireVolume(Volume& volume)
    {
        DeferredReleaseQueue& deferredRelease = graphicsCore_->DeferredRelease();
        if (volume.texture) {
            deferredRelease.PushForCurrentFrame(Microsoft::WRL::ComPtr<ID3D12Resource>(volume.texture.Get()));
            volume.texture.Release();
        }
        DescriptorAllocator* allocator = graphicsCore_->GetDescriptorAllocator();
        deferredRelease.PushForCurrentFrame(*allocator, volume.srv);
        deferredRelease.PushForCurrentFrame(*allocator, volume.uav);
    }

    bool VolumetricFog::EnsureVolumes(uint32_t sizeX, uint32_t sizeY, uint32_t sizeZ)
    {
        if (gridSize_[0] == sizeX && gridSize_[1] == sizeY && gridSize_[2] == sizeZ && integrated_.texture) {
            return true;
        }

        RetireVolume(vBuffer_);
        RetireVolume(lightScattering_);
        RetireVolume(integrated_);
        gridSize_[0] = gridSize_[1] = gridSize_[2] = 0;

        if (!CreateVolume(vBuffer_, sizeX, sizeY, sizeZ, "VolumetricFogVBuffer")
            || !CreateVolume(lightScattering_, sizeX, sizeY, sizeZ, "VolumetricFogLightScattering")
            || !CreateVolume(integrated_, sizeX, sizeY, sizeZ, "VolumetricFogIntegrated")) {
            RetireVolume(vBuffer_);
            RetireVolume(lightScattering_);
            RetireVolume(integrated_);
            return false;
        }

        gridSize_[0] = sizeX;
        gridSize_[1] = sizeY;
        gridSize_[2] = sizeZ;
        return true;
    }

    void VolumetricFog::FillDirectionalLights(Constants& constants, const LightManager* lightManager) const
    {
        constants.directionalCount = 0;
        if (!lightManager) {
            return;
        }

        const uint32_t count = lightManager->GetLightCount(LightType::Directional);
        for (uint32_t i = 0; i < count && constants.directionalCount < kMaxDirectionalLights; ++i) {
            const Light* light = lightManager->GetDirectionalLight(i);
            if (!light || !light->enabled || LengthSquared(light->direction) < 1.0e-8f) {
                continue;
            }
            DirectionalLight& out = constants.directional[constants.directionalCount++];
            out.direction = Normalize(light->direction);
            out.irradiance = lightManager->GetEffectiveLightColorRGB(*light)
                * LightUnits::LuxToShader(light->intensity);
        }
    }

    bool VolumetricFog::PrepareFrame(const VolumetricFogFrameInput& input)
    {
        integratedThisFrame_ = false;
        frameConstants_ = 0;
        if (!pipelineReady_ || !graphicsCore_ || !input.view || !input.view->isValid || !input.fog
            || input.screenWidth == 0 || input.screenHeight == 0) {
            return false;
        }

        const uint32_t pixelSize = static_cast<uint32_t>(std::clamp(VolumetricFogCVars::GridPixelSize.Get(), 4, 64));
        const uint32_t sizeX = DivideRoundUp(input.screenWidth, pixelSize);
        const uint32_t sizeY = DivideRoundUp(input.screenHeight, pixelSize);
        const uint32_t sizeZ = static_cast<uint32_t>(std::clamp(VolumetricFogCVars::GridSizeZ.Get(), 8, 256));
        if (!EnsureVolumes(sizeX, sizeY, sizeZ)) {
            return false;
        }

        const ViewInfo& view = *input.view;
        const FogSettings& fog = *input.fog;
        const Vector4& albedo = VolumetricFogCVars::Albedo.Get();
        const float ambientScale = VolumetricFogCVars::AmbientScale.Get();

        Constants constants{};
        constants.invViewProj = view.invViewProjection;
        constants.cameraWorldPos = view.position;
        constants.distance = std::max(VolumetricFogCVars::Distance.Get(), 1.0f);
        constants.gridSize[0] = sizeX;
        constants.gridSize[1] = sizeY;
        constants.gridSize[2] = sizeZ;
        constants.depthDistribution = std::max(VolumetricFogCVars::DepthDistribution.Get(), 1.0e-3f);
        constants.screenToGrid = Vector2{
            static_cast<float>(input.screenWidth) / static_cast<float>(pixelSize * sizeX),
            static_cast<float>(input.screenHeight) / static_cast<float>(pixelSize * sizeY) };
        constants.gridPixelSize = Vector2{
            static_cast<float>(pixelSize) / static_cast<float>(input.screenWidth),
            static_cast<float>(pixelSize) / static_cast<float>(input.screenHeight) };
        constants.albedo = Vector3{ albedo.x, albedo.y, albedo.z };
        constants.phaseG = std::clamp(VolumetricFogCVars::Anisotropy.Get(), -0.99f, 0.99f);
        constants.density = fog.density;
        constants.heightFalloff = fog.heightFalloff;
        constants.heightRef = fog.heightRefM;
        constants.startDistance = fog.startDistanceM;
        constants.fallbackAmbient = input.fallbackAmbient * ambientScale;
        constants.debugView = static_cast<uint32_t>(std::clamp(VolumetricFogCVars::DebugView.Get(), 0, 3));

        const AtmosphereManager* atmosphere = input.atmosphere;
        const bool skyAmbientUsable = atmosphere
            && atmosphere->IsAtmosphereActive()
            && atmosphere->IsSkyAmbientEnabled()
            && atmosphere->IsSkyAmbientReady()
            && atmosphere->GetSkyIrradianceSHSRVHandle().ptr != 0;
        // 空の光は画面に描く空と同じ輝度の単位のまま使う（面の r.Atmosphere.SkyAmbientScale は掛けない）
        constants.skyAmbientEnabled = skyAmbientUsable ? 1u : 0u;
        constants.skyAmbientScale = skyAmbientUsable ? ambientScale : 0.0f;
        skyIrradianceSrv_ = atmosphere
            ? atmosphere->GetSkyIrradianceSHSRVHandle() : D3D12_GPU_DESCRIPTOR_HANDLE{};

        FillDirectionalLights(constants, input.lightManager);

        frameConstants_ = graphicsCore_->GetUploadRing().AllocateConstants(constants);
        return frameConstants_ != 0;
    }

    void VolumetricFog::DispatchInject(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || frameConstants_ == 0) {
            return;
        }
        namespace B = VolumetricFogInjectBind;

        Barrier::Transition(cmdList, vBuffer_.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetPipelineState(injectPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(injectPipeline_.GetComputeRootSignature());
        {
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(injectBindings_[B::gVolumetricFog], frameConstants_);
            binder.Set(injectBindings_[B::gVBuffer], vBuffer_.uav.gpuHandle);
            binder.ValidateBeforeDraw(injectBindings_);
        }
        cmdList->Dispatch(
            DivideRoundUp(gridSize_[0], kCellGroupSize),
            DivideRoundUp(gridSize_[1], kCellGroupSize),
            DivideRoundUp(gridSize_[2], kCellGroupSize));
        Barrier::UAV(cmdList, vBuffer_.texture);
        Barrier::Transition(cmdList, vBuffer_.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void VolumetricFog::DispatchLightScattering(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || frameConstants_ == 0) {
            return;
        }
        namespace B = VolumetricFogLightScatteringBind;

        Barrier::Transition(cmdList, lightScattering_.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetPipelineState(lightScatteringPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(lightScatteringPipeline_.GetComputeRootSignature());
        {
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(lightScatteringBindings_[B::gVolumetricFog], frameConstants_);
            binder.Set(lightScatteringBindings_[B::gVBuffer], vBuffer_.srv.gpuHandle);
            if (skyIrradianceSrv_.ptr != 0) {
                binder.Set(lightScatteringBindings_[B::gSkyIrradianceSH], skyIrradianceSrv_);
            }
            binder.Set(lightScatteringBindings_[B::gLightScattering], lightScattering_.uav.gpuHandle);
            binder.ValidateBeforeDraw(lightScatteringBindings_);
        }
        cmdList->Dispatch(
            DivideRoundUp(gridSize_[0], kCellGroupSize),
            DivideRoundUp(gridSize_[1], kCellGroupSize),
            DivideRoundUp(gridSize_[2], kCellGroupSize));
        Barrier::UAV(cmdList, lightScattering_.texture);
        Barrier::Transition(cmdList, lightScattering_.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void VolumetricFog::DispatchIntegrate(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || frameConstants_ == 0) {
            return;
        }
        namespace B = VolumetricFogIntegrateBind;

        Barrier::Transition(cmdList, integrated_.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetPipelineState(integratePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(integratePipeline_.GetComputeRootSignature());
        {
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(integrateBindings_[B::gVolumetricFog], frameConstants_);
            binder.Set(integrateBindings_[B::gLightScattering], lightScattering_.srv.gpuHandle);
            binder.Set(integrateBindings_[B::gIntegrated], integrated_.uav.gpuHandle);
            binder.ValidateBeforeDraw(integrateBindings_);
        }
        cmdList->Dispatch(
            DivideRoundUp(gridSize_[0], kColumnGroupSize),
            DivideRoundUp(gridSize_[1], kColumnGroupSize),
            1);
        Barrier::UAV(cmdList, integrated_.texture);
        Barrier::Transition(cmdList, integrated_.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        integratedThisFrame_ = true;
    }

    void VolumetricFog::EndFrame()
    {
        integratedThisFrame_ = false;
        frameConstants_ = 0;
        skyIrradianceSrv_ = {};
    }
}
