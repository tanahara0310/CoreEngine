#include "pch.h"
#include "Graphics/Fog/Volumetric/VolumetricFog.h"

#include "Camera/View/ViewInfo.h"
#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/Shader/CloudShaderConstants.h"
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
#include <cmath>
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

        /// @brief カメラが 1 フレームでこれより動いたら前フレームの光を混ぜない [m]
        constexpr float kHistoryResetDistance = 10.0f;

        /// @brief 格子の中のずらし方が一巡するフレーム数
        constexpr uint32_t kJitterSequenceLength = 16;

        /// @brief base 進の各桁を小数点の後ろへ折り返した値（Halton 列）
        float RadicalInverse(uint32_t base, uint32_t index)
        {
            const float inverseBase = 1.0f / static_cast<float>(base);
            float fraction = inverseBase;
            float result = 0.0f;
            while (index > 0) {
                result += static_cast<float>(index % base) * fraction;
                index /= base;
                fraction *= inverseBase;
            }
            return result;
        }

        /// @brief TAA のジッタを外した View*Projection
        Matrix4x4 UnjitteredViewProjection(const ViewInfo& view)
        {
            Matrix4x4 projection = view.projection;
            const int row = (std::fabs(projection.m[2][3]) > 0.5f) ? 2 : 3;
            projection.m[row][0] -= view.jitterNdc.x;
            projection.m[row][1] -= view.jitterNdc.y;
            return view.viewMatrix * projection;
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

        const auto retireAll = [this]() {
            RetireVolume(vBuffer_);
            RetireVolume(lightScattering_[0]);
            RetireVolume(lightScattering_[1]);
            RetireVolume(integrated_);
        };
        retireAll();
        gridSize_[0] = gridSize_[1] = gridSize_[2] = 0;
        historyAvailable_ = false;

        if (!CreateVolume(vBuffer_, sizeX, sizeY, sizeZ, "VolumetricFogVBuffer")
            || !CreateVolume(lightScattering_[0], sizeX, sizeY, sizeZ, "VolumetricFogLightScattering0")
            || !CreateVolume(lightScattering_[1], sizeX, sizeY, sizeZ, "VolumetricFogLightScattering1")
            || !CreateVolume(integrated_, sizeX, sizeY, sizeZ, "VolumetricFogIntegrated")) {
            retireAll();
            return false;
        }

        gridSize_[0] = sizeX;
        gridSize_[1] = sizeY;
        gridSize_[2] = sizeZ;
        return true;
    }

    void VolumetricFog::FillCloudShadow(Constants& constants, const VolumetricFogFrameInput& input)
    {
        cloudShadowSrv_ = {};
        constants.cloudShadowEnabled = 0;
        const CloudShadowShaderConstants* cloudShadow = input.cloudShadow;
        if (!cloudShadow || input.cloudShadowMapSrv.ptr == 0
            || cloudShadow->sceneStrength <= 0.0f || cloudShadow->regionSizeM <= 0.0f) {
            return;
        }
        constants.cloudShadowRegionCenterXZ = Vector2{ cloudShadow->regionCenterX, cloudShadow->regionCenterZ };
        constants.cloudShadowRegionSize = cloudShadow->regionSizeM;
        constants.cloudShadowAnchorY = cloudShadow->anchorWorldY;
        constants.cloudShadowEdgeFadeStart = cloudShadow->edgeFadeStart;
        constants.cloudShadowStrength = std::clamp(cloudShadow->sceneStrength, 0.0f, 1.0f);
        constants.cloudShadowEnabled = 1;
        cloudShadowSrv_ = input.cloudShadowMapSrv;
    }

    void VolumetricFog::FillSunDepth(Constants& constants, const VolumetricFogFrameInput& input)
    {
        sunDepthSrv_ = {};
        const VolumetricFogSunDepth* sunDepth = input.sunDepth;
        if (!sunDepth || sunDepth->srv.ptr == 0 || sunDepth->size <= 0.0f
            || constants.sunDepthLightIndex == kNoSunDepthLight) {
            constants.sunDepthLightIndex = kNoSunDepthLight;
            return;
        }
        constants.sunDepthOrigin = sunDepth->origin;
        constants.sunDepthInvSize = 1.0f / sunDepth->size;
        constants.sunDepthRight = sunDepth->right;
        constants.sunDepthBias = sunDepth->bias;
        constants.sunDepthUp = sunDepth->up;
        constants.sunDepthRange = sunDepth->range;
        constants.sunDepthDir = sunDepth->direction;
        sunDepthSrv_ = sunDepth->srv;
    }

    void VolumetricFog::FillHistory(Constants& constants, const Matrix4x4& viewProj, const Vector3& cameraPos)
    {
        const float historyWeight = std::clamp(VolumetricFogCVars::HistoryWeight.Get(), 0.0f, 0.98f);
        const bool temporal = historyWeight > 0.0f;

        // 混ぜないときは格子の中心で引く
        constants.frameJitter = Vector3{ 0.0f, 0.0f, 0.5f };
        if (temporal) {
            const uint32_t sample = (frameIndex_ % kJitterSequenceLength) + 1;
            constants.frameJitter = Vector3{
                RadicalInverse(2, sample) - 0.5f,
                RadicalInverse(3, sample) - 0.5f,
                std::fmod(0.5f + static_cast<float>(sample) * 0.6180339887f, 1.0f) };
        }

        const bool sameGrid = (prevDistance_ == constants.distance)
            && (prevDepthDistribution_ == constants.depthDistribution);
        const bool cameraJumped = Length(cameraPos - prevCameraPos_) > kHistoryResetDistance;
        constants.historyValid = (temporal && historyAvailable_ && sameGrid && !cameraJumped) ? 1u : 0u;
        constants.historyWeight = historyWeight;
        constants.prevViewProj = prevViewProj_;
        constants.prevCameraWorldPos = prevCameraPos_;

        prevViewProj_ = viewProj;
        prevCameraPos_ = cameraPos;
        prevDistance_ = constants.distance;
        prevDepthDistribution_ = constants.depthDistribution;
    }

    void VolumetricFog::FillDirectionalLights(
        Constants& constants, const LightManager* lightManager, const Light* sunDepthLight) const
    {
        constants.directionalCount = 0;
        constants.sunDepthLightIndex = kNoSunDepthLight;
        if (!lightManager) {
            return;
        }

        const uint32_t count = lightManager->GetLightCount(LightType::Directional);
        for (uint32_t i = 0; i < count && constants.directionalCount < kMaxDirectionalLights; ++i) {
            const Light* light = lightManager->GetDirectionalLight(i);
            if (!light || !light->enabled || LengthSquared(light->direction) < 1.0e-8f
                || light->volumetricScatteringIntensity <= 0.0f) {
                continue;
            }
            if (light == sunDepthLight) {
                constants.sunDepthLightIndex = constants.directionalCount;
            }
            DirectionalLight& out = constants.directional[constants.directionalCount++];
            out.direction = Normalize(light->direction);
            out.irradiance = lightManager->GetEffectiveLightColorRGB(*light)
                * (LightUnits::LuxToShader(light->intensity) * light->volumetricScatteringIntensity);
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
        const Matrix4x4 viewProj = UnjitteredViewProjection(view);

        currentLight_ ^= 1u;
        ++frameIndex_;

        Constants constants{};
        constants.invViewProj = MathCore::Matrix::Inverse(viewProj);
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
        constants.debugView = static_cast<uint32_t>(std::clamp(VolumetricFogCVars::DebugView.Get(), 0, 5));

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

        FillDirectionalLights(constants, input.lightManager, input.sunDepth ? input.sunDepth->light : nullptr);
        FillCloudShadow(constants, input);
        FillSunDepth(constants, input);
        FillHistory(constants, viewProj, view.position);

        // 物の影を作る光源が替わったら前フレームの光は混ぜない
        const Light* sunDepthLight = (constants.sunDepthLightIndex != kNoSunDepthLight) ? input.sunDepth->light : nullptr;
        if (sunDepthLight != prevSunDepthLight_) {
            constants.historyValid = 0;
        }
        prevSunDepthLight_ = sunDepthLight;

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
        Volume& current = lightScattering_[currentLight_];
        Volume& previous = lightScattering_[currentLight_ ^ 1u];

        Barrier::Transition(cmdList, previous.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, current.texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->SetPipelineState(lightScatteringPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(lightScatteringPipeline_.GetComputeRootSignature());
        {
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(lightScatteringBindings_[B::gVolumetricFog], frameConstants_);
            binder.Set(lightScatteringBindings_[B::gVBuffer], vBuffer_.srv.gpuHandle);
            if (skyIrradianceSrv_.ptr != 0) {
                binder.Set(lightScatteringBindings_[B::gSkyIrradianceSH], skyIrradianceSrv_);
            }
            if (cloudShadowSrv_.ptr != 0) {
                binder.Set(lightScatteringBindings_[B::gCloudShadowMap], cloudShadowSrv_);
            }
            if (sunDepthSrv_.ptr != 0) {
                binder.Set(lightScatteringBindings_[B::gSunDepth], sunDepthSrv_);
            }
            binder.Set(lightScatteringBindings_[B::gLightScatteringHistory], previous.srv.gpuHandle);
            binder.Set(lightScatteringBindings_[B::gLightScattering], current.uav.gpuHandle);
            binder.ValidateBeforeDraw(lightScatteringBindings_);
        }
        cmdList->Dispatch(
            DivideRoundUp(gridSize_[0], kCellGroupSize),
            DivideRoundUp(gridSize_[1], kCellGroupSize),
            DivideRoundUp(gridSize_[2], kCellGroupSize));
        Barrier::UAV(cmdList, current.texture);
        Barrier::Transition(cmdList, current.texture, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
            binder.Set(integrateBindings_[B::gLightScattering], lightScattering_[currentLight_].srv.gpuHandle);
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
        historyAvailable_ = integratedThisFrame_;
        integratedThisFrame_ = false;
        frameConstants_ = 0;
        skyIrradianceSrv_ = {};
        cloudShadowSrv_ = {};
        sunDepthSrv_ = {};
    }
}
