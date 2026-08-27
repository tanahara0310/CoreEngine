#include "pch.h"
#include "VolumetricCloudManager.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/CloudBindings.h"
#include "Graphics/Cloud/CloudCVars.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <exception>

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

        /// @brief CS パイプラインを構築し、宣言表を解決する
        /// @return 構築と解決の両方に成功したら true（失敗時は呼び出し側が機能を無効化する）
        template <size_t N>
        bool BuildComputePass(ID3D12Device* device,
                              ShaderCompiler& compiler,
                              ShaderReflectionBuilder& reflectionBuilder,
                              CustomShaderPipeline& pipeline,
                              const ICustomShaderProvider& provider,
                              const ShaderBindingDecl (&decls)[N],
                              BindingTable& outBindings,
                              const char* name)
        {
            if (!pipeline.Build(device, compiler, reflectionBuilder, provider)
                || !pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "VolumetricCloudManager: {} コンピュートパイプラインの構築に失敗", name);
                return false;
            }

            const ShaderReflectionData* reflection = pipeline.GetComputeReflection();
            if (!reflection) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "VolumetricCloudManager: {} のリフレクションを取得できません", name);
                return false;
            }

            try {
                outBindings = BindingTable::Resolve(*reflection, decls, name);
            }
            catch (const std::exception&) {
                // 違反の内訳は BindingTable::Resolve が error ログへ出している
                return false;
            }
            return true;
        }
    }
    void VolumetricCloudManager::Initialize(GraphicsCore* graphicsCore, DescriptorAllocator* descriptorAllocator)
    {
        graphicsCore_ = graphicsCore;
        device_ = graphicsCore ? graphicsCore->GetDevice() : nullptr;
        descriptorAllocator_ = descriptorAllocator;

        ID3D12Device* device = device_;
        if (!device) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: デバイスが無いため初期化を中止");
            return;
        }

        // CB を作る前に設定を取り込む（既定値の実体は CVar なので、読む前は全て 0）
        CloudCVars::LoadInto(parameters_);
        enabled_ = CloudCVars::Enabled.Get();

        // 雲定数バッファ（永続マップ）
        constantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(VolumetricCloudShaderConstants));
        constantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&constantData_));
        UploadConstants();

        // ゴッドレイ定数バッファ（永続マップ）
        godRayConstantBuffer_ = ResourceFactory::CreateBufferResource(device, sizeof(GodRayShaderConstants));
        godRayConstantBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&godRayConstantData_));

        // ノイズリソースと生成パイプライン（Phase 1）。
        // レイマーチ/合成パイプライン（Phase 2）は CreateRenderPipelines で別途構築する。
        const bool noiseResourcesReady = CreateNoiseResources(device, descriptorAllocator);
        noisePipelinesReady_ = noiseResourcesReady && CreateNoisePipelines(device);
        pipelinesReady_ = CreateRenderPipelines(device);

        // ゴッドレイ（失敗しても雲本体は無効化しない）
        const bool godRayResourcesReady = CreateGodRayResources(device, descriptorAllocator);
        godRayPipelinesReady_ = godRayResourcesReady && CreateGodRayPipelines(device);

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "VolumetricCloudManager: 初期化完了 (雲底={:.0f}m, 層厚={:.0f}m, ノイズ生成={}, 描画={}, ゴッドレイ={})",
            parameters_.layerBottomAltitudeM, parameters_.layerThicknessM,
            noisePipelinesReady_ ? "OK" : "無効",
            pipelinesReady_ ? "OK" : "無効",
            godRayPipelinesReady_ ? "OK" : "無効");
    }

    void VolumetricCloudManager::SetEnabled(bool enabled)
    {
        // 実体は CVar が保持する。書き戻すことで UI 表示・自動保存にも反映される
        CloudCVars::Enabled.Set(enabled);
        enabled_ = enabled;
    }

    void VolumetricCloudManager::Update(const Vector3& cameraWorldPosition,
        const Matrix4x4& viewMatrix, const Matrix4x4& projMatrix,
        const AtmosphereManager* atmosphereManager,
        float deltaTimeSec)
    {
        // 設定は CVar が保持する。UI・設定復元のどの経路で変わってもここで取り込む
        CloudCVars::LoadInto(parameters_);
        enabled_ = CloudCVars::Enabled.Get();

        // Update() を呼ぶのは雲を使うシーンのみ。このフレームは雲を有効にする。
        cloudsActive_ = true;

        // 風アニメーション用の時刻積算
        timeSec_ += deltaTimeSec;

        // カメラ情報
        cameraWorldPos_ = cameraWorldPosition;
        invViewProj_ = MathCore::Matrix::Inverse(
            viewMatrix * projMatrix);

        // 太陽・月情報・カメラ高度は AtmosphereManager から取得（単一情報源）。
        if (atmosphereManager) {
            sunDirection_ = atmosphereManager->GetSunDirection();
            const Vector4& sc = atmosphereManager->GetSunColor();
            sunColor_ = { sc.x, sc.y, sc.z };
            sunIntensity_ = atmosphereManager->GetSunIntensity();
            hasMoon_ = atmosphereManager->HasMoonLight();
            moonDirection_ = atmosphereManager->GetMoonDirection();
            const Vector4& mc = atmosphereManager->GetMoonColor();
            moonColor_ = { mc.x, mc.y, mc.z };
            moonIntensity_ = atmosphereManager->GetMoonIntensity();
            // 雲層シェルの原点と半径は大気と同じ値を使う（食い違うと雲底と地平線がずれる）
            planetRadiusM_ = atmosphereManager->GetParameters().planetRadius;
            groundLevelY_ = atmosphereManager->GetParameters().groundLevelY;
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
        // 雲層の球殻交差用に惑星半径 [m] を渡す（大気 CB は km なので別値）
        c.planetRadiusM = planetRadiusM_;
        c.layerBottomAltitudeM = parameters_.layerBottomAltitudeM;
        c.layerThicknessM = parameters_.layerThicknessM;
        c.groundLevelY = groundLevelY_;
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
        c.outputWidth = targetsWidth_;
        c.outputHeight = targetsHeight_;
        c.pad0 = 0;
        c.sunLightScale = parameters_.sunLightScale;
        c.msAttenuation = parameters_.msAttenuation;
        c.msContribution = parameters_.msContribution;
        c.msEccentricity = parameters_.msEccentricity;
        c.moonDirection = moonDirection_;
        c.moonIntensity = moonIntensity_;
        c.moonColor = moonColor_;
        c.hasMoon = hasMoon_ ? 1.0f : 0.0f;

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
                                 const BindingTable& bindings,
                                 GpuResource& tex,
                                 D3D12_GPU_DESCRIPTOR_HANDLE uav,
                                 UINT gx, UINT gy, UINT gz)
        {
            Barrier::Transition(cmdList, tex,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            cmdList->SetPipelineState(pipeline.GetComputePSO());
            cmdList->SetComputeRootSignature(pipeline.GetComputeRootSignature());

            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(bindings[CloudNoiseBind::gOutput], uav);
            binder.ValidateBeforeDraw(bindings);

            cmdList->Dispatch(gx, gy, gz);

            Barrier::UAV(cmdList, tex);
            Barrier::Transition(cmdList, tex,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };

        const UINT baseGroups = kBaseShapeNoiseSize / 4;   // numthreads(4,4,4)
        dispatchNoise(baseShapeNoisePipeline_, baseShapeNoiseBindings_, baseShapeNoise_,
            baseShapeNoiseUavHandle_.gpuHandle, baseGroups, baseGroups, baseGroups);

        const UINT detailGroups = kDetailNoiseSize / 4;    // numthreads(4,4,4)
        dispatchNoise(detailNoisePipeline_, detailNoiseBindings_, detailNoise_,
            detailNoiseUavHandle_.gpuHandle, detailGroups, detailGroups, detailGroups);

        const UINT weatherGroups = kWeatherMapSize / 8;    // numthreads(8,8,1)
        dispatchNoise(weatherMapPipeline_, weatherMapBindings_, weatherMap_,
            weatherMapUavHandle_.gpuHandle, weatherGroups, weatherGroups, 1);

        noiseDirty_ = false;
        noiseGenerated_ = true;

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "VolumetricCloud: ノイズテクスチャ生成完了 (BaseShape 128^3 / Detail 32^3 / Weather 512^2)");
    }

    void VolumetricCloudManager::RenderClouds(
        ID3D12GraphicsCommandList* cmdList,
        GpuResource& sceneColor,
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
        Barrier::Transition(cmdList, cloudBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(rayMarchPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(rayMarchPipeline_.GetComputeRootSignature());

        {
            namespace B = CloudRayMarchBind;
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(rayMarchBindings_[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            // 大気散乱の定数バッファと LUT（太陽色・アンビエントの単一情報源）
            binder.Set(rayMarchBindings_[B::gAtmosphere], atmosphereManager->GetConstantBufferGPUAddress());
            binder.Set(rayMarchBindings_[B::gBaseShapeNoise], baseShapeNoiseSrvHandle_.gpuHandle);
            binder.Set(rayMarchBindings_[B::gDetailNoise], detailNoiseSrvHandle_.gpuHandle);
            binder.Set(rayMarchBindings_[B::gWeatherMap], weatherMapSrvHandle_.gpuHandle);
            binder.Set(rayMarchBindings_[B::gSceneDepth], depthSrvHandle);
            binder.Set(rayMarchBindings_[B::gTransmittanceLUT], atmosphereManager->GetTransmittanceLUTSRVHandle());
            binder.Set(rayMarchBindings_[B::gSkyViewLUT], atmosphereManager->GetSkyViewLUTSRVHandle());
            binder.Set(rayMarchBindings_[B::gCloudOutput], cloudBufferUavHandle_.gpuHandle);
            binder.ValidateBeforeDraw(rayMarchBindings_);
        }

        cmdList->Dispatch(
            (targetsWidth_ + 7) / 8,
            (targetsHeight_ + 7) / 8,
            1);

        // 合成 CS が SRV として読めるよう遷移
        Barrier::UAV(cmdList, cloudBuffer_);
        Barrier::Transition(cmdList, cloudBuffer_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== 合成 CS: SceneColor + CloudBuffer → 中間テクスチャ =====
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, compositeResult_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(compositePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(compositePipeline_.GetComputeRootSignature());

        {
            namespace B = CloudCompositeBind;
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(compositeBindings_[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            binder.Set(compositeBindings_[B::gSceneColor], sceneColorSrvHandle);
            binder.Set(compositeBindings_[B::gCloudBuffer], cloudBufferSrvHandle_.gpuHandle);
            binder.Set(compositeBindings_[B::gSceneDepth], depthSrvHandle);
            binder.Set(compositeBindings_[B::gOutput], compositeResultUavHandle_.gpuHandle);
            binder.ValidateBeforeDraw(compositeBindings_);
        }

        cmdList->Dispatch(
            (static_cast<UINT>(compositeResult_.Desc().Width) + 7) / 8,
            (compositeResult_.Desc().Height + 7) / 8,
            1);

        // ===== 結果を SceneColor へコピーバック =====
        Barrier::UAV(cmdList, compositeResult_);
        Barrier::Transition(cmdList, compositeResult_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor.Get(), compositeResult_.Get());

        // 後続パス（Transparent 等）に備えて元の想定状態へ戻す
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, compositeResult_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, cloudBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    bool VolumetricCloudManager::CreateNoiseResources(ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        if (!device || !descriptorAllocator) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device;

        // (tex, srvOut, uavOut, size, is3D, name) を確保するローカル関数
        auto createNoiseTexture = [&](GpuResource& tex,
                                      DescriptorHandle& srvHandle,
                                      DescriptorHandle& uavHandle,
                                      uint32_t size, bool is3D, const char* name) -> bool
        {
            const D3D12_RESOURCE_DESC desc = MakeNoiseTextureDesc(size, is3D);
            try {
                tex.Reset(
                    ResourceFactory::CreateTextureResource(
                        deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "VolumetricCloudManager: ノイズテクスチャ({})の生成に失敗", name);
                return false;
            }

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

            srvHandle = descriptorAllocator->CreateSRV(tex.Get(), srvDesc, (std::string(name) + "SRV").c_str());
            uavHandle = descriptorAllocator->CreateUAV(tex.Get(), uavDesc, (std::string(name) + "UAV").c_str());
            return true;
        };

        if (!createNoiseTexture(baseShapeNoise_,
            baseShapeNoiseSrvHandle_, baseShapeNoiseUavHandle_,
            kBaseShapeNoiseSize, true, "CloudBaseShape")) {
            return false;
        }
        if (!createNoiseTexture(detailNoise_,
            detailNoiseSrvHandle_, detailNoiseUavHandle_,
            kDetailNoiseSize, true, "CloudDetail")) {
            return false;
        }
        if (!createNoiseTexture(weatherMap_,
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
            BindingTable& bindings;
            const char* name;
        };
        Entry entries[] = {
            { baseShapeNoisePipeline_, baseShapeNoiseShaderProvider_, baseShapeNoiseBindings_, "BaseShapeNoise" },
            { detailNoisePipeline_,    detailNoiseShaderProvider_,    detailNoiseBindings_,    "DetailNoise" },
            { weatherMapPipeline_,     weatherMapShaderProvider_,     weatherMapBindings_,     "WeatherMap" },
        };

        for (Entry& e : entries) {
            if (!BuildComputePass(device, shaderCompiler, reflectionBuilder,
                    e.pipeline, e.provider, CloudNoiseBind::kDecls, e.bindings, e.name)) {
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

        if (!BuildComputePass(device, shaderCompiler, reflectionBuilder,
                rayMarchPipeline_, rayMarchShaderProvider_,
                CloudRayMarchBind::kDecls, rayMarchBindings_, "RayMarch")) {
            return false;
        }
        if (!BuildComputePass(device, shaderCompiler, reflectionBuilder,
                compositePipeline_, compositeShaderProvider_,
                CloudCompositeBind::kDecls, compositeBindings_, "Composite")) {
            return false;
        }
        if (!BuildComputePass(device, shaderCompiler, reflectionBuilder,
                cubemapCapturePipeline_, cubemapCaptureShaderProvider_,
                CloudCubemapCaptureBind::kDecls, cubemapCaptureBindings_, "CloudCubemapCapture")) {
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

        {
            namespace B = CloudCubemapCaptureBind;
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(cubemapCaptureBindings_[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            binder.Set(cubemapCaptureBindings_[B::gAtmosphere], atmosphereManager->GetConstantBufferGPUAddress());
            binder.Set(cubemapCaptureBindings_[B::gBaseShapeNoise], baseShapeNoiseSrvHandle_.gpuHandle);
            binder.Set(cubemapCaptureBindings_[B::gDetailNoise], detailNoiseSrvHandle_.gpuHandle);
            binder.Set(cubemapCaptureBindings_[B::gWeatherMap], weatherMapSrvHandle_.gpuHandle);
            binder.Set(cubemapCaptureBindings_[B::gTransmittanceLUT], atmosphereManager->GetTransmittanceLUTSRVHandle());
            binder.Set(cubemapCaptureBindings_[B::gSkyViewLUT], atmosphereManager->GetSkyViewLUTSRVHandle());
            binder.Set(cubemapCaptureBindings_[B::gSkyCubemap], cubemapUav);
            binder.ValidateBeforeDraw(cubemapCaptureBindings_);
        }

        constexpr uint32_t kCubemapSize = AtmosphereManager::kSkyCubemapSize;
        cmdList->Dispatch((kCubemapSize + 7) / 8, (kCubemapSize + 7) / 8, 6);
        // 後段のプリフィルタ（PrefilterSkyEnvironment）が SRV 遷移で同期するため、
        // ここでの UAV バリアは不要
    }

    bool VolumetricCloudManager::EnsureCloudTargets(GpuResource& sceneColor)
    {
        if (!sceneColor || !device_ || !descriptorAllocator_) {
            return false;
        }

        const D3D12_RESOURCE_DESC sceneDesc = sceneColor.Desc();

        // 0 サイズ（ウィンドウ最小化時など）では確保しない（0 幅テクスチャ生成のクラッシュ回避）
        if (sceneDesc.Width == 0 || sceneDesc.Height == 0) {
            return false;
        }

        const uint64_t div = std::max(parameters_.resolutionDivisor, 1u);
        const uint32_t halfW = static_cast<uint32_t>((sceneDesc.Width + div - 1) / div);
        const uint32_t halfH = static_cast<uint32_t>((sceneDesc.Height + div - 1) / div);

        // SceneColor と同サイズ・同分割数で確保済みなら再利用する。
        // 半解像度側も見ないと r.Cloud.ResolutionDivisor の変更が反映されない
        // （合成中間は常に SceneColor と同サイズなので、それだけでは判定にならない）
        if (compositeResult_ && godRayBuffer_ && cloudBuffer_ &&
            compositeResult_.Desc().Width == sceneDesc.Width &&
            compositeResult_.Desc().Height == sceneDesc.Height &&
            cloudBuffer_.Desc().Width == halfW &&
            cloudBuffer_.Desc().Height == halfH) {
            return true;
        }

        // 作り直す前に投入済みの描画完了を待つ。待たずに解放すると、まだ前フレームの
        // ディスパッチが参照しているテクスチャを落とすことになる
        if (graphicsCore_ && (cloudBuffer_ || godRayBuffer_ || compositeResult_)) {
            graphicsCore_->WaitForGpuIdle();
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = device_;

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
            cloudBuffer_.Reset(ResourceFactory::CreateTextureResource(
                deviceRef, cloudDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 半解像度 CloudBuffer の生成に失敗");
            return false;
        }

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
            descriptorAllocator_->EnsureSRV(cloudBufferSrvHandle_, cloudBuffer_.Get(), srvDesc, "CloudBufferSRV");
            descriptorAllocator_->EnsureUAV(cloudBufferUavHandle_, cloudBuffer_.Get(), uavDesc, "CloudBufferUAV");
        }

        // ===== 半解像度ゴッドレイバッファ（CloudBuffer と同サイズ・同フォーマット） =====
        try {
            godRayBuffer_.Reset(ResourceFactory::CreateTextureResource(
                deviceRef, cloudDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 半解像度 GodRayBuffer の生成に失敗");
            return false;
        }

        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = cloudDesc.Format;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = cloudDesc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            descriptorAllocator_->EnsureSRV(godRayBufferSrvHandle_, godRayBuffer_.Get(), srvDesc, "GodRayBufferSRV");
            descriptorAllocator_->EnsureUAV(godRayBufferUavHandle_, godRayBuffer_.Get(), uavDesc, "GodRayBufferUAV");
        }

        // ===== 合成用中間テクスチャ（SceneColor と同サイズ・同フォーマット, UAV） =====
        D3D12_RESOURCE_DESC compositeDesc = sceneDesc;
        compositeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        try {
            compositeResult_.Reset(ResourceFactory::CreateTextureResource(
                deviceRef, compositeDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 合成中間テクスチャの生成に失敗");
            return false;
        }

        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = compositeDesc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            descriptorAllocator_->EnsureUAV(compositeResultUavHandle_, compositeResult_.Get(), uavDesc, "CloudCompositeUAV");
        }

        targetsWidth_ = halfW;
        targetsHeight_ = halfH;

        Logger::GetInstance().Infof(LogCategory::Graphics,
            "VolumetricCloud: 描画ターゲット確保 ({}x{} / 分割数 {} → 半解像度 {}x{})",
            static_cast<uint32_t>(sceneDesc.Width), sceneDesc.Height,
            static_cast<uint32_t>(div), halfW, halfH);
        return true;
    }

    bool VolumetricCloudManager::CreateGodRayResources(ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        if (!device || !descriptorAllocator) {
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
            cloudShadowMap_.Reset(ResourceFactory::CreateTextureResource(
                deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "VolumetricCloudManager: 雲シャドウマップテクスチャの生成に失敗");
            return false;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        cloudShadowMapSrvHandle_ = descriptorAllocator->CreateSRV(cloudShadowMap_.Get(), srvDesc, "CloudShadowMapSRV");
        cloudShadowMapUavHandle_ = descriptorAllocator->CreateUAV(cloudShadowMap_.Get(), uavDesc, "CloudShadowMapUAV");

        return true;
    }

    bool VolumetricCloudManager::CreateGodRayPipelines(ID3D12Device* device)
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        if (!BuildComputePass(device, shaderCompiler, reflectionBuilder,
                cloudShadowPipeline_, cloudShadowShaderProvider_,
                CloudShadowMapBind::kDecls, cloudShadowBindings_, "CloudShadowMap")) {
            return false;
        }
        if (!BuildComputePass(device, shaderCompiler, reflectionBuilder,
                godRayMarchPipeline_, godRayMarchShaderProvider_,
                GodRayMarchBind::kDecls, godRayMarchBindings_, "GodRayMarch")) {
            return false;
        }
        if (!BuildComputePass(device, shaderCompiler, reflectionBuilder,
                godRayCompositePipeline_, godRayCompositeShaderProvider_,
                GodRayCompositeBind::kDecls, godRayCompositeBindings_, "GodRayComposite")) {
            return false;
        }

        return true;
    }

    void VolumetricCloudManager::UploadGodRayConstants()
    {
        if (!godRayConstantData_) {
            return;
        }

        // 地表 Y は雲 CB と同じ値を使う（食い違うとシャドウ基準面が雲底からずれる）
        const float groundY = groundLevelY_;

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
        g.outputWidth = targetsWidth_;
        g.outputHeight = targetsHeight_;

        *godRayConstantData_ = g;
    }

    void VolumetricCloudManager::RenderGodRays(
        ID3D12GraphicsCommandList* cmdList,
        GpuResource& sceneColor,
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
        UploadGodRayConstants();

        // ===== 雲シャドウマップ生成 =====
        // 風の移流・太陽移動・カメラ追従で毎フレーム変わるため、雲アクティブ中は毎回焼き直す
        // （1024²×24 サンプルの cheap 密度で Transmittance LUT 生成より軽い）。
        Barrier::Transition(cmdList, cloudShadowMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(cloudShadowPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(cloudShadowPipeline_.GetComputeRootSignature());

        {
            namespace B = CloudShadowMapBind;
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(cloudShadowBindings_[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            binder.Set(cloudShadowBindings_[B::gGodRay], godRayConstantBuffer_->GetGPUVirtualAddress());
            binder.Set(cloudShadowBindings_[B::gBaseShapeNoise], baseShapeNoiseSrvHandle_.gpuHandle);
            binder.Set(cloudShadowBindings_[B::gDetailNoise], detailNoiseSrvHandle_.gpuHandle);
            binder.Set(cloudShadowBindings_[B::gWeatherMap], weatherMapSrvHandle_.gpuHandle);
            binder.Set(cloudShadowBindings_[B::gCloudShadowMap], cloudShadowMapUavHandle_.gpuHandle);
            binder.ValidateBeforeDraw(cloudShadowBindings_);
        }

        cmdList->Dispatch(
            (kCloudShadowMapSize + 7) / 8,
            (kCloudShadowMapSize + 7) / 8,
            1);

        // マーチ CS が SRV として読めるよう遷移
        Barrier::UAV(cmdList, cloudShadowMap_);
        Barrier::Transition(cmdList, cloudShadowMap_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== ゴッドレイマーチ CS: 遮蔽差分を半解像度で積分 =====
        // 雲透過率（cloudBuffer_.a）で差分をスケールするため SRV として読む
        // （RenderClouds が末尾で UAV 状態へ戻している）
        Barrier::Transition(cmdList, cloudBuffer_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, godRayBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(godRayMarchPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(godRayMarchPipeline_.GetComputeRootSignature());

        {
            namespace B = GodRayMarchBind;
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(godRayMarchBindings_[B::gGodRay], godRayConstantBuffer_->GetGPUVirtualAddress());
            binder.Set(godRayMarchBindings_[B::gAtmosphere], atmosphereManager->GetConstantBufferGPUAddress());
            binder.Set(godRayMarchBindings_[B::gCloudShadowMap], cloudShadowMapSrvHandle_.gpuHandle);
            binder.Set(godRayMarchBindings_[B::gTransmittanceLUT], atmosphereManager->GetTransmittanceLUTSRVHandle());
            binder.Set(godRayMarchBindings_[B::gSceneDepth], depthSrvHandle);
            binder.Set(godRayMarchBindings_[B::gCloudBuffer], cloudBufferSrvHandle_.gpuHandle);
            binder.Set(godRayMarchBindings_[B::gGodRayOutput], godRayBufferUavHandle_.gpuHandle);
            binder.ValidateBeforeDraw(godRayMarchBindings_);
        }

        cmdList->Dispatch(
            (targetsWidth_ + 7) / 8,
            (targetsHeight_ + 7) / 8,
            1);

        // 合成 CS が SRV として読めるよう遷移
        Barrier::UAV(cmdList, godRayBuffer_);
        Barrier::Transition(cmdList, godRayBuffer_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== 合成 CS: SceneColor + Δ輝度 → 中間テクスチャ =====
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, compositeResult_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(godRayCompositePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(godRayCompositePipeline_.GetComputeRootSignature());

        {
            namespace B = GodRayCompositeBind;
            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(godRayCompositeBindings_[B::gGodRay], godRayConstantBuffer_->GetGPUVirtualAddress());
            binder.Set(godRayCompositeBindings_[B::gSceneColor], sceneColorSrvHandle);
            binder.Set(godRayCompositeBindings_[B::gGodRayBuffer], godRayBufferSrvHandle_.gpuHandle);
            binder.Set(godRayCompositeBindings_[B::gOutput], compositeResultUavHandle_.gpuHandle);
            binder.ValidateBeforeDraw(godRayCompositeBindings_);
        }

        cmdList->Dispatch(
            (static_cast<UINT>(compositeResult_.Desc().Width) + 7) / 8,
            (compositeResult_.Desc().Height + 7) / 8,
            1);

        // ===== 結果を SceneColor へコピーバック =====
        Barrier::UAV(cmdList, compositeResult_);
        Barrier::Transition(cmdList, compositeResult_, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor.Get(), compositeResult_.Get());

        // 後続パス（Transparent 等）に備えて元の想定状態へ戻す
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, compositeResult_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, godRayBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, cloudShadowMap_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, cloudBuffer_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}
