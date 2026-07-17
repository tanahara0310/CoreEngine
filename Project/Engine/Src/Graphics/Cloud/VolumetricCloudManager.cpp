#include "pch.h"
#include "VolumetricCloudManager.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    namespace {
        /// @brief UAV 対応テクスチャ（2D/3D）の Desc を作る
        D3D12_RESOURCE_DESC MakeNoiseTextureDesc(uint32_t size, bool is3D)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = is3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                  : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = size;
            desc.Height = size;
            desc.DepthOrArraySize = is3D ? static_cast<UINT16>(size) : 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            return desc;
        }
    }
    void VolumetricCloudManager::Initialize(ID3D12Device* device, DescriptorManager* descriptorManager)
    {
        device_ = device;
        descriptorManager_ = descriptorManager;

        // 雲定数バッファ（永続マップ）
        constantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(VolumetricCloudShaderConstants));
        constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantData_));
        UploadConstants();

        // ゴッドレイ定数バッファ（永続マップ）
        godRayConstantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(GodRayShaderConstants));
        godRayConstantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&godRayConstantData_));

        // ノイズリソースと生成パイプライン（Phase 1）。
        // レイマーチ/合成パイプライン（Phase 2）は CreateRenderPipelines で別途構築する。
        const bool noiseResourcesReady = CreateNoiseResources(device, descriptorManager);
        noisePipelinesReady_ = noiseResourcesReady && CreateNoisePipelines(device);
        pipelinesReady_ = CreateRenderPipelines(device);

        // ゴッドレイ（失敗しても雲本体は無効化しない）
        const bool godRayResourcesReady = CreateGodRayResources(device, descriptorManager);
        godRayPipelinesReady_ = godRayResourcesReady && CreateGodRayPipelines(device);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "VolumetricCloudManager: 初期化完了 (雲底={:.0f}m, 層厚={:.0f}m, ノイズ生成={}, 描画={}, ゴッドレイ={})",
            parameters_.layerBottomAltitudeM, parameters_.layerThicknessM,
            noisePipelinesReady_ ? "OK" : "無効",
            pipelinesReady_ ? "OK" : "無効",
            godRayPipelinesReady_ ? "OK" : "無効");
    }

    void VolumetricCloudManager::Update(const Vector3& cameraWorldPosition,
        const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
        const AtmosphereManager* atmosphereManager,
        float deltaTimeSec)
    {
        // Update() を呼ぶのは雲を使うシーンのみ。このフレームは雲を有効にする。
        cloudsActive_ = true;

        // 風アニメーション用の時刻積算
        timeSec_ += deltaTimeSec;
        ++frameIndex_;

        // カメラ情報
        cameraWorldPos_ = cameraWorldPosition;
        invViewProj_ = MathCore::Matrix::Inverse(
            MathCore::Matrix::Multiply(viewMatrix, projMatrix));

        // 太陽情報・カメラ高度は AtmosphereManager から取得（単一情報源）。
        if (atmosphereManager) {
            sunDirection_ = atmosphereManager->GetSunDirection();
            const Vector4& sc = atmosphereManager->GetSunColor();
            sunColor_ = { sc.x, sc.y, sc.z };
            sunIntensity_ = atmosphereManager->GetSunIntensity();
            distanceFromPlanetCenter_ = atmosphereManager->GetDistanceFromPlanetCenter();
        }

        UploadConstants();
    }

    void VolumetricCloudManager::UploadConstants()
    {
        if (!constantData_) {
            return;
        }

        VolumetricCloudShaderConstants c{};
        c.invViewProj = invViewProj_;
        c.cameraWorldPos = cameraWorldPos_;
        c.timeSec = timeSec_;
        c.sunDirection = sunDirection_;
        c.sunIntensity = sunIntensity_;
        c.sunColor = sunColor_;
        // 雲層の球殻交差用に惑星半径 [m] を渡す（大気 CB は km なので別値）。
        // 惑星半径は AtmosphereParameters の既定と一致させる。
        c.planetRadiusM = 6360000.0f;
        c.layerBottomAltitudeM = parameters_.layerBottomAltitudeM;
        c.layerThicknessM = parameters_.layerThicknessM;
        c.groundLevelY = 0.0f;
        c.globalCoverage = parameters_.globalCoverage;
        c.baseNoiseScaleM = parameters_.baseNoiseScaleM;
        c.detailNoiseScaleM = parameters_.detailNoiseScaleM;
        c.detailErosionStrength = parameters_.detailErosionStrength;
        c.densityScale = parameters_.densityScale;
        c.windDirX = parameters_.windDirX;
        c.windDirZ = parameters_.windDirZ;
        c.windSpeedMPerS = parameters_.windSpeedMPerS;
        c.weatherMapScaleM = parameters_.weatherMapScaleM;
        c.phaseG0 = parameters_.phaseG0;
        c.phaseG1 = parameters_.phaseG1;
        c.phaseBlend = parameters_.phaseBlend;
        c.ambientIntensity = parameters_.ambientIntensity;
        c.beerPowderStrength = parameters_.beerPowderStrength;
        c.lightMarchStepM = parameters_.lightMarchStepM;
        c.earlyExitTransmittance = parameters_.earlyExitTransmittance;
        c.maxMarchDistanceM = parameters_.maxMarchDistanceM;
        c.maxSteps = parameters_.maxSteps;
        c.outputWidth = static_cast<uint32_t>(targetsWidth_);
        c.outputHeight = targetsHeight_;
        c.frameIndex = frameIndex_;
        c.sunLightScale = parameters_.sunLightScale;
        c.msAttenuation = parameters_.msAttenuation;
        c.msContribution = parameters_.msContribution;
        c.msEccentricity = parameters_.msEccentricity;

        *constantData_ = c;
    }

    void VolumetricCloudManager::GenerateNoiseTexturesIfNeeded(ID3D12GraphicsCommandList* cmdList)
    {
        if (!cmdList || !noisePipelinesReady_) {
            return;
        }
        if (!noiseDirty_) {
            return;
        }

        // 各ノイズ CS: UAV へ書き込み → 描画/レイマーチが読めるよう SRV 状態へ遷移。
        // ノイズシェーダーは定数バッファ不要（純手続き生成）。gOutput UAV のみバインドする。
        auto dispatchNoise = [&](CustomShaderPipeline& pipeline,
                                 ID3D12Resource* tex, D3D12_RESOURCE_STATES& state,
                                 D3D12_GPU_DESCRIPTOR_HANDLE uav,
                                 UINT gx, UINT gy, UINT gz)
        {
            ResourceBarrierHelper::Transition(cmdList, tex, state,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            cmdList->SetPipelineState(pipeline.GetComputePSO());
            cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());

            const int uavSlot = pipeline.GetComputeRootParamIndex("gOutput");
            if (uavSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(uavSlot), uav);
            }

            cmdList->Dispatch(gx, gy, gz);

            ResourceBarrierHelper::UAV(cmdList, tex);
            ResourceBarrierHelper::Transition(cmdList, tex, state,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };

        const UINT baseGroups = kBaseShapeNoiseSize / 4;   // numthreads(4,4,4)
        dispatchNoise(baseShapeNoisePipeline_, baseShapeNoise_.Get(), baseShapeNoiseState_,
            baseShapeNoiseUavHandle_, baseGroups, baseGroups, baseGroups);

        const UINT detailGroups = kDetailNoiseSize / 4;    // numthreads(4,4,4)
        dispatchNoise(detailNoisePipeline_, detailNoise_.Get(), detailNoiseState_,
            detailNoiseUavHandle_, detailGroups, detailGroups, detailGroups);

        const UINT weatherGroups = kWeatherMapSize / 8;    // numthreads(8,8,1)
        dispatchNoise(weatherMapPipeline_, weatherMap_.Get(), weatherMapState_,
            weatherMapUavHandle_, weatherGroups, weatherGroups, 1);

        noiseDirty_ = false;
        noiseGenerated_ = true;

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "VolumetricCloud: ノイズテクスチャ生成完了 (BaseShape 128^3 / Detail 32^3 / Weather 512^2)");
    }

    void VolumetricCloudManager::RenderClouds(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* sceneColor,
        D3D12_RESOURCE_STATES& sceneColorState,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
        const AtmosphereManager* atmosphereManager)
    {
        if (!cmdList || !pipelinesReady_ || !noiseGenerated_ || !atmosphereManager) {
            return;
        }
        if (!EnsureCloudTargets(sceneColor)) {
            return;
        }

        // 出力サイズ（半解像度）を CB へ反映してから Dispatch する。
        UploadConstants();

        // ===== レイマーチ CS: BaseShapeNoise + SceneDepth → 半解像度 CloudBuffer =====
        ResourceBarrierHelper::Transition(cmdList, cloudBuffer_.Get(),
            cloudBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(rayMarchPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(rayMarchPipeline_.GetComputeRootSignature());

        {
            const int cbSlot = rayMarchPipeline_.GetComputeRootParamIndex("gCloud");
            if (cbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
            }
            const int baseSlot = rayMarchPipeline_.GetComputeRootParamIndex("gBaseShapeNoise");
            if (baseSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(baseSlot), baseShapeNoiseSrvHandle_);
            }
            const int detailSlot = rayMarchPipeline_.GetComputeRootParamIndex("gDetailNoise");
            if (detailSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(detailSlot), detailNoiseSrvHandle_);
            }
            const int weatherSlot = rayMarchPipeline_.GetComputeRootParamIndex("gWeatherMap");
            if (weatherSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(weatherSlot), weatherMapSrvHandle_);
            }
            const int depthSlot = rayMarchPipeline_.GetComputeRootParamIndex("gSceneDepth");
            if (depthSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(depthSlot), depthSrvHandle);
            }
            // 大気散乱の定数バッファと LUT（太陽色・アンビエントの単一情報源）
            const int atmoCbSlot = rayMarchPipeline_.GetComputeRootParamIndex("gAtmosphere");
            if (atmoCbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(atmoCbSlot), atmosphereManager->GetConstantBufferGPUAddress());
            }
            const int transmittanceSlot = rayMarchPipeline_.GetComputeRootParamIndex("gTransmittanceLUT");
            if (transmittanceSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(
                    static_cast<UINT>(transmittanceSlot), atmosphereManager->GetTransmittanceLUTSRVHandle());
            }
            const int skyViewSlot = rayMarchPipeline_.GetComputeRootParamIndex("gSkyViewLUT");
            if (skyViewSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(
                    static_cast<UINT>(skyViewSlot), atmosphereManager->GetSkyViewLUTSRVHandle());
            }
            const int outSlot = rayMarchPipeline_.GetComputeRootParamIndex("gCloudOutput");
            if (outSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outSlot), cloudBufferUavHandle_);
            }
        }

        cmdList->Dispatch(
            (static_cast<UINT>(targetsWidth_) + 7) / 8,
            (targetsHeight_ + 7) / 8,
            1);

        // 合成 CS が SRV として読めるよう遷移
        ResourceBarrierHelper::UAV(cmdList, cloudBuffer_.Get());
        ResourceBarrierHelper::Transition(cmdList, cloudBuffer_.Get(),
            cloudBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== 合成 CS: SceneColor + CloudBuffer → 中間テクスチャ =====
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, compositeResult_.Get(),
            compositeResultState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(compositePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(compositePipeline_.GetComputeRootSignature());

        {
            const int cbSlot = compositePipeline_.GetComputeRootParamIndex("gCloud");
            if (cbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
            }
            const int sceneSlot = compositePipeline_.GetComputeRootParamIndex("gSceneColor");
            if (sceneSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(sceneSlot), sceneColorSrvHandle);
            }
            const int cloudSlot = compositePipeline_.GetComputeRootParamIndex("gCloudBuffer");
            if (cloudSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(cloudSlot), cloudBufferSrvHandle_);
            }
            const int depthSlot = compositePipeline_.GetComputeRootParamIndex("gSceneDepth");
            if (depthSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(depthSlot), depthSrvHandle);
            }
            const int outSlot = compositePipeline_.GetComputeRootParamIndex("gOutput");
            if (outSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outSlot), compositeResultUavHandle_);
            }
        }

        cmdList->Dispatch(
            (static_cast<UINT>(compositeResult_->GetDesc().Width) + 7) / 8,
            (compositeResult_->GetDesc().Height + 7) / 8,
            1);

        // ===== 結果を SceneColor へコピーバック =====
        ResourceBarrierHelper::UAV(cmdList, compositeResult_.Get());
        ResourceBarrierHelper::Transition(cmdList, compositeResult_.Get(),
            compositeResultState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor, compositeResult_.Get());

        // 後続パス（Transparent 等）に備えて元の想定状態へ戻す
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, compositeResult_.Get(),
            compositeResultState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, cloudBuffer_.Get(),
            cloudBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    bool VolumetricCloudManager::CreateNoiseResources(ID3D12Device* device, DescriptorManager* descriptorManager)
    {
        if (!device || !descriptorManager) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        // (tex, state, srvOut, uavOut, size, is3D, name) を確保するローカル関数
        auto createNoiseTexture = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& tex,
                                      D3D12_RESOURCE_STATES& state,
                                      D3D12_GPU_DESCRIPTOR_HANDLE& srvHandle,
                                      D3D12_GPU_DESCRIPTOR_HANDLE& uavHandle,
                                      uint32_t size, bool is3D, const char* name) -> bool
        {
            const D3D12_RESOURCE_DESC desc = MakeNoiseTextureDesc(size, is3D);
            try {
                tex = ResourceFactory::CreateTextureResource(
                    deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "VolumetricCloudManager: ノイズテクスチャ({})の生成に失敗", name);
                return false;
            }
            state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = desc.Format;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = desc.Format;
            if (is3D) {
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                srvDesc.Texture3D.MipLevels = 1;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                uavDesc.Texture3D.WSize = size;
            } else {
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
            descriptorManager->CreateSRV(tex.Get(), srvDesc, cpuHandle, srvHandle, (std::string(name) + "SRV").c_str());
            descriptorManager->CreateUAV(tex.Get(), uavDesc, cpuHandle, uavHandle, (std::string(name) + "UAV").c_str());
            return true;
        };

        if (!createNoiseTexture(baseShapeNoise_, baseShapeNoiseState_,
            baseShapeNoiseSrvHandle_, baseShapeNoiseUavHandle_,
            kBaseShapeNoiseSize, true, "CloudBaseShape")) {
            return false;
        }
        if (!createNoiseTexture(detailNoise_, detailNoiseState_,
            detailNoiseSrvHandle_, detailNoiseUavHandle_,
            kDetailNoiseSize, true, "CloudDetail")) {
            return false;
        }
        if (!createNoiseTexture(weatherMap_, weatherMapState_,
            weatherMapSrvHandle_, weatherMapUavHandle_,
            kWeatherMapSize, false, "CloudWeather")) {
            return false;
        }

        return true;
    }

    bool VolumetricCloudManager::CreateNoisePipelines(ID3D12Device* device)
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        struct Entry {
            CustomShaderPipeline& pipeline;
            const ICustomShaderProvider& provider;
            const char* name;
        };
        Entry entries[] = {
            { baseShapeNoisePipeline_, baseShapeNoiseShaderProvider_, "BaseShapeNoise" },
            { detailNoisePipeline_,    detailNoiseShaderProvider_,    "DetailNoise" },
            { weatherMapPipeline_,     weatherMapShaderProvider_,     "WeatherMap" },
        };

        for (Entry& e : entries) {
            const bool built = e.pipeline.Build(device, shaderCompiler, reflectionBuilder, e.provider);
            if (!built || !e.pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "VolumetricCloudManager: {} コンピュートパイプラインの構築に失敗", e.name);
                return false;
            }
        }
        return true;
    }

    bool VolumetricCloudManager::CreateRenderPipelines(ID3D12Device* device)
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool rayMarchBuilt = rayMarchPipeline_.Build(
            device, shaderCompiler, reflectionBuilder, rayMarchShaderProvider_);
        if (!rayMarchBuilt || !rayMarchPipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: RayMarch コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool compositeBuilt = compositePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, compositeShaderProvider_);
        if (!compositeBuilt || !compositePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: Composite コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool cubemapCaptureBuilt = cubemapCapturePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, cubemapCaptureShaderProvider_);
        if (!cubemapCaptureBuilt || !cubemapCapturePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: CloudCubemapCapture コンピュートパイプラインの構築に失敗");
            return false;
        }

        return true;
    }

    void VolumetricCloudManager::RenderCloudsToSkyCubemap(
        ID3D12GraphicsCommandList* cmdList,
        const AtmosphereManager* atmosphereManager)
    {
        if (!cmdList || !pipelinesReady_ || !noiseGenerated_ || !atmosphereManager) {
            return;
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE cubemapUav = atmosphereManager->GetSkyCubemapUAVHandle();
        if (cubemapUav.ptr == 0) {
            return;
        }

        cmdList->SetPipelineState(cubemapCapturePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(cubemapCapturePipeline_.GetComputeRootSignature());

        const int cbSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gCloud");
        if (cbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(cbSlot), constantBuffer_->GetGPUVirtualAddress());
        }
        const int atmoCbSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gAtmosphere");
        if (atmoCbSlot >= 0) {
            cmdList->SetComputeRootConstantBufferView(
                static_cast<UINT>(atmoCbSlot), atmosphereManager->GetConstantBufferGPUAddress());
        }
        const int baseSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gBaseShapeNoise");
        if (baseSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(baseSlot), baseShapeNoiseSrvHandle_);
        }
        const int detailSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gDetailNoise");
        if (detailSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(detailSlot), detailNoiseSrvHandle_);
        }
        const int weatherSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gWeatherMap");
        if (weatherSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(weatherSlot), weatherMapSrvHandle_);
        }
        const int transmittanceSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gTransmittanceLUT");
        if (transmittanceSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(transmittanceSlot), atmosphereManager->GetTransmittanceLUTSRVHandle());
        }
        const int skyViewSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gSkyViewLUT");
        if (skyViewSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(
                static_cast<UINT>(skyViewSlot), atmosphereManager->GetSkyViewLUTSRVHandle());
        }
        const int outSlot = cubemapCapturePipeline_.GetComputeRootParamIndex("gSkyCubemap");
        if (outSlot >= 0) {
            cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outSlot), cubemapUav);
        }

        constexpr uint32_t kCubemapSize = AtmosphereManager::kSkyCubemapSize;
        cmdList->Dispatch((kCubemapSize + 7) / 8, (kCubemapSize + 7) / 8, 6);
        // 後段のプリフィルタ（PrefilterSkyEnvironment）が SRV 遷移で同期するため、
        // ここでの UAV バリアは不要
    }

    bool VolumetricCloudManager::EnsureCloudTargets(ID3D12Resource* sceneColor)
    {
        if (!sceneColor || !device_ || !descriptorManager_) {
            return false;
        }

        const D3D12_RESOURCE_DESC sceneDesc = sceneColor->GetDesc();

        // 0 サイズ（ウィンドウ最小化時など）では確保しない（0 幅テクスチャ生成のクラッシュ回避）
        if (sceneDesc.Width == 0 || sceneDesc.Height == 0) {
            return false;
        }

        // SceneColor と同サイズで確保済みなら再利用
        if (compositeResult_ && godRayBuffer_ &&
            compositeResult_->GetDesc().Width == sceneDesc.Width &&
            compositeResult_->GetDesc().Height == sceneDesc.Height) {
            return true;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device_;

        const uint64_t div = std::max(parameters_.resolutionDivisor, 1u);
        const uint64_t halfW = (sceneDesc.Width + div - 1) / div;
        const uint32_t halfH = static_cast<uint32_t>((sceneDesc.Height + div - 1) / div);

        // ===== レイマーチ結果（R16G16B16A16, UAV+SRV） =====
        D3D12_RESOURCE_DESC cloudDesc{};
        cloudDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        cloudDesc.Width = halfW;
        cloudDesc.Height = halfH;
        cloudDesc.DepthOrArraySize = 1;
        cloudDesc.MipLevels = 1;
        cloudDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cloudDesc.SampleDesc.Count = 1;
        cloudDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        cloudDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            cloudBuffer_ = ResourceFactory::CreateTextureResource(
                deviceRef, cloudDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 半解像度 CloudBuffer の生成に失敗");
            return false;
        }
        cloudBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = cloudDesc.Format;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = cloudDesc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            // リサイズによる再生成時は既存スロットへ書き直す（毎回確保するとスロットリーク）
            descriptorManager_->CreateOrUpdateSRV(cloudBuffer_.Get(), srvDesc, cloudBufferSrvCpuHandle_, cloudBufferSrvHandle_, "CloudBufferSRV");
            descriptorManager_->CreateOrUpdateUAV(cloudBuffer_.Get(), uavDesc, cloudBufferUavCpuHandle_, cloudBufferUavHandle_, "CloudBufferUAV");
        }

        // ===== 半解像度ゴッドレイバッファ（CloudBuffer と同サイズ・同フォーマット） =====
        try {
            godRayBuffer_ = ResourceFactory::CreateTextureResource(
                deviceRef, cloudDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 半解像度 GodRayBuffer の生成に失敗");
            return false;
        }
        godRayBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = cloudDesc.Format;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = cloudDesc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            descriptorManager_->CreateOrUpdateSRV(godRayBuffer_.Get(), srvDesc, godRayBufferSrvCpuHandle_, godRayBufferSrvHandle_, "GodRayBufferSRV");
            descriptorManager_->CreateOrUpdateUAV(godRayBuffer_.Get(), uavDesc, godRayBufferUavCpuHandle_, godRayBufferUavHandle_, "GodRayBufferUAV");
        }

        // ===== 合成用中間テクスチャ（SceneColor と同サイズ・同フォーマット, UAV） =====
        D3D12_RESOURCE_DESC compositeDesc = sceneDesc;
        compositeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        try {
            compositeResult_ = ResourceFactory::CreateTextureResource(
                deviceRef, compositeDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 合成中間テクスチャの生成に失敗");
            return false;
        }
        compositeResultState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = compositeDesc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            descriptorManager_->CreateOrUpdateUAV(compositeResult_.Get(), uavDesc, compositeResultUavCpuHandle_, compositeResultUavHandle_, "CloudCompositeUAV");
        }

        targetsWidth_ = halfW;
        targetsHeight_ = halfH;
        return true;
    }

    bool VolumetricCloudManager::CreateGodRayResources(ID3D12Device* device, DescriptorManager* descriptorManager)
    {
        if (!device || !descriptorManager) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        // ===== 雲シャドウマップ（1024² R16_FLOAT） =====
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kCloudShadowMapSize;
        desc.Height = kCloudShadowMapSize;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            cloudShadowMap_ = ResourceFactory::CreateTextureResource(
                deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 雲シャドウマップテクスチャの生成に失敗");
            return false;
        }
        cloudShadowMapState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        descriptorManager->CreateSRV(cloudShadowMap_.Get(), srvDesc, cpuHandle, cloudShadowMapSrvHandle_, "CloudShadowMapSRV");
        descriptorManager->CreateUAV(cloudShadowMap_.Get(), uavDesc, cpuHandle, cloudShadowMapUavHandle_, "CloudShadowMapUAV");

        return true;
    }

    bool VolumetricCloudManager::CreateGodRayPipelines(ID3D12Device* device)
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        const bool shadowBuilt = cloudShadowPipeline_.Build(
            device, shaderCompiler, reflectionBuilder, cloudShadowShaderProvider_);
        if (!shadowBuilt || !cloudShadowPipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: CloudShadowMap コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool marchBuilt = godRayMarchPipeline_.Build(
            device, shaderCompiler, reflectionBuilder, godRayMarchShaderProvider_);
        if (!marchBuilt || !godRayMarchPipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: GodRayMarch コンピュートパイプラインの構築に失敗");
            return false;
        }

        const bool godRayCompositeBuilt = godRayCompositePipeline_.Build(
            device, shaderCompiler, reflectionBuilder, godRayCompositeShaderProvider_);
        if (!godRayCompositeBuilt || !godRayCompositePipeline_.HasComputePSO()) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: GodRayComposite コンピュートパイプラインの構築に失敗");
            return false;
        }

        return true;
    }

    void VolumetricCloudManager::UploadGodRayConstants(const AtmosphereManager* atmosphereManager)
    {
        if (!godRayConstantData_) {
            return;
        }

        const float groundY = atmosphereManager
            ? atmosphereManager->GetParameters().groundLevelY
            : 0.0f;

        GodRayShaderConstants g{};
        g.invViewProj = invViewProj_;
        g.cameraWorldPos = cameraWorldPos_;
        g.maxDistanceM = parameters_.godRayMaxDistanceM;

        // シャドウマップ範囲の中心はテクセルサイズへスナップする（カメラ移動での泳ぎ防止）
        const float texelM = parameters_.cloudShadowRegionSizeM / static_cast<float>(kCloudShadowMapSize);
        g.shadowRegionCenterX = std::floor(cameraWorldPos_.x / texelM) * texelM;
        g.shadowRegionCenterZ = std::floor(cameraWorldPos_.z / texelM) * texelM;
        g.shadowRegionSizeM = parameters_.cloudShadowRegionSizeM;
        g.shadowAnchorWorldY = groundY + parameters_.layerBottomAltitudeM;

        g.intensity = parameters_.godRayIntensity;
        g.mieBoost = parameters_.godRayMieBoost;
        g.groundLevelY = groundY;
        g.edgeFadeStart = 0.8f;
        g.stepCount = std::max(parameters_.godRayStepCount, 1u);
        g.outputWidth = static_cast<uint32_t>(targetsWidth_);
        g.outputHeight = targetsHeight_;

        *godRayConstantData_ = g;
    }

    void VolumetricCloudManager::RenderGodRays(
        ID3D12GraphicsCommandList* cmdList,
        ID3D12Resource* sceneColor,
        D3D12_RESOURCE_STATES& sceneColorState,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandle,
        const AtmosphereManager* atmosphereManager)
    {
        if (!cmdList || !godRayPipelinesReady_ || !noiseGenerated_ || !atmosphereManager) {
            return;
        }
        if (!parameters_.godRayEnabled) {
            return;
        }
        if (!EnsureCloudTargets(sceneColor)) {
            return;
        }

        // 出力サイズ（半解像度）確定後に CB を更新する
        UploadGodRayConstants(atmosphereManager);

        // ===== 雲シャドウマップ生成 =====
        // 風の移流・太陽移動・カメラ追従で毎フレーム変わるため、雲アクティブ中は毎回焼き直す
        // （1024²×24 サンプルの cheap 密度で Transmittance LUT 生成より軽い）。
        ResourceBarrierHelper::Transition(cmdList, cloudShadowMap_.Get(),
            cloudShadowMapState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(cloudShadowPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(cloudShadowPipeline_.GetComputeRootSignature());

        {
            const int cloudCbSlot = cloudShadowPipeline_.GetComputeRootParamIndex("gCloud");
            if (cloudCbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(cloudCbSlot), constantBuffer_->GetGPUVirtualAddress());
            }
            const int godRayCbSlot = cloudShadowPipeline_.GetComputeRootParamIndex("gGodRay");
            if (godRayCbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(godRayCbSlot), godRayConstantBuffer_->GetGPUVirtualAddress());
            }
            const int baseSlot = cloudShadowPipeline_.GetComputeRootParamIndex("gBaseShapeNoise");
            if (baseSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(baseSlot), baseShapeNoiseSrvHandle_);
            }
            const int detailSlot = cloudShadowPipeline_.GetComputeRootParamIndex("gDetailNoise");
            if (detailSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(detailSlot), detailNoiseSrvHandle_);
            }
            const int weatherSlot = cloudShadowPipeline_.GetComputeRootParamIndex("gWeatherMap");
            if (weatherSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(weatherSlot), weatherMapSrvHandle_);
            }
            const int outSlot = cloudShadowPipeline_.GetComputeRootParamIndex("gCloudShadowMap");
            if (outSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outSlot), cloudShadowMapUavHandle_);
            }
        }

        cmdList->Dispatch(
            (kCloudShadowMapSize + 7) / 8,
            (kCloudShadowMapSize + 7) / 8,
            1);

        // マーチ CS が SRV として読めるよう遷移
        ResourceBarrierHelper::UAV(cmdList, cloudShadowMap_.Get());
        ResourceBarrierHelper::Transition(cmdList, cloudShadowMap_.Get(),
            cloudShadowMapState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== ゴッドレイマーチ CS: 遮蔽差分を半解像度で積分 =====
        // 雲透過率（cloudBuffer_.a）で差分をスケールするため SRV として読む
        // （RenderClouds が末尾で UAV 状態へ戻している）
        ResourceBarrierHelper::Transition(cmdList, cloudBuffer_.Get(),
            cloudBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, godRayBuffer_.Get(),
            godRayBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(godRayMarchPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(godRayMarchPipeline_.GetComputeRootSignature());

        {
            const int cbSlot = godRayMarchPipeline_.GetComputeRootParamIndex("gGodRay");
            if (cbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(cbSlot), godRayConstantBuffer_->GetGPUVirtualAddress());
            }
            const int atmoCbSlot = godRayMarchPipeline_.GetComputeRootParamIndex("gAtmosphere");
            if (atmoCbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(atmoCbSlot), atmosphereManager->GetConstantBufferGPUAddress());
            }
            const int shadowSlot = godRayMarchPipeline_.GetComputeRootParamIndex("gCloudShadowMap");
            if (shadowSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(shadowSlot), cloudShadowMapSrvHandle_);
            }
            const int transmittanceSlot = godRayMarchPipeline_.GetComputeRootParamIndex("gTransmittanceLUT");
            if (transmittanceSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(
                    static_cast<UINT>(transmittanceSlot), atmosphereManager->GetTransmittanceLUTSRVHandle());
            }
            const int depthSlot = godRayMarchPipeline_.GetComputeRootParamIndex("gSceneDepth");
            if (depthSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(depthSlot), depthSrvHandle);
            }
            const int cloudBufSlot = godRayMarchPipeline_.GetComputeRootParamIndex("gCloudBuffer");
            if (cloudBufSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(cloudBufSlot), cloudBufferSrvHandle_);
            }
            const int outSlot = godRayMarchPipeline_.GetComputeRootParamIndex("gGodRayOutput");
            if (outSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outSlot), godRayBufferUavHandle_);
            }
        }

        cmdList->Dispatch(
            (static_cast<UINT>(targetsWidth_) + 7) / 8,
            (targetsHeight_ + 7) / 8,
            1);

        // 合成 CS が SRV として読めるよう遷移
        ResourceBarrierHelper::UAV(cmdList, godRayBuffer_.Get());
        ResourceBarrierHelper::Transition(cmdList, godRayBuffer_.Get(),
            godRayBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== 合成 CS: SceneColor + Δ輝度 → 中間テクスチャ =====
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, compositeResult_.Get(),
            compositeResultState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(godRayCompositePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(godRayCompositePipeline_.GetComputeRootSignature());

        {
            const int cbSlot = godRayCompositePipeline_.GetComputeRootParamIndex("gGodRay");
            if (cbSlot >= 0) {
                cmdList->SetComputeRootConstantBufferView(
                    static_cast<UINT>(cbSlot), godRayConstantBuffer_->GetGPUVirtualAddress());
            }
            const int sceneSlot = godRayCompositePipeline_.GetComputeRootParamIndex("gSceneColor");
            if (sceneSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(sceneSlot), sceneColorSrvHandle);
            }
            const int bufSlot = godRayCompositePipeline_.GetComputeRootParamIndex("gGodRayBuffer");
            if (bufSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(bufSlot), godRayBufferSrvHandle_);
            }
            const int outSlot = godRayCompositePipeline_.GetComputeRootParamIndex("gOutput");
            if (outSlot >= 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(outSlot), compositeResultUavHandle_);
            }
        }

        cmdList->Dispatch(
            (static_cast<UINT>(compositeResult_->GetDesc().Width) + 7) / 8,
            (compositeResult_->GetDesc().Height + 7) / 8,
            1);

        // ===== 結果を SceneColor へコピーバック =====
        ResourceBarrierHelper::UAV(cmdList, compositeResult_.Get());
        ResourceBarrierHelper::Transition(cmdList, compositeResult_.Get(),
            compositeResultState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor, compositeResult_.Get());

        // 後続パス（Transparent 等）に備えて元の想定状態へ戻す
        ResourceBarrierHelper::Transition(cmdList, sceneColor,
            sceneColorState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, compositeResult_.Get(),
            compositeResultState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, godRayBuffer_.Get(),
            godRayBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, cloudShadowMap_.Get(),
            cloudShadowMapState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, cloudBuffer_.Get(),
            cloudBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}
