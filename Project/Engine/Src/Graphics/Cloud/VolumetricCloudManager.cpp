#include "pch.h"
#include "VolumetricCloudManager.h"

#include "Graphics/Atmosphere/AtmosphereManager.h"
#include "Graphics/Cloud/CloudBindings.h"
#include "Graphics/Cloud/CloudCVars.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cmath>

namespace CoreEngine
{
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

        // ノイズテクスチャと生成パイプライン
        const bool noiseResourcesReady = resources_.CreateNoiseTextures(device, descriptorAllocator);
        noisePipelinesReady_ = noiseResourcesReady && pipelines_.BuildNoisePasses(device);
        pipelinesReady_ = pipelines_.BuildRenderPasses(device);

        // ゴッドレイ（失敗しても雲本体は無効化しない）
        const bool godRayResourcesReady = resources_.CreateCloudShadowMap(device, descriptorAllocator);
        godRayPipelinesReady_ = godRayResourcesReady && pipelines_.BuildGodRayPasses(device);

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
        c.outputWidth = resources_.TargetsWidth();
        c.outputHeight = resources_.TargetsHeight();
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
        auto dispatchNoise = [&](CloudPass passId, CloudGpuTexture& tex,
                                 UINT gx, UINT gy, UINT gz)
        {
            Barrier::Transition(cmdList, tex,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            const CloudComputePass& pass = pipelines_[passId];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[CloudNoiseBind::gOutput], tex.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);

            cmdList->Dispatch(gx, gy, gz);

            Barrier::UAV(cmdList, tex);
            Barrier::Transition(cmdList, tex,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        };

        const UINT baseGroups = CloudResources::kBaseShapeNoiseSize / 4;   // numthreads(4,4,4)
        dispatchNoise(CloudPass::BaseShapeNoise, resources_.baseShapeNoise,
            baseGroups, baseGroups, baseGroups);

        const UINT detailGroups = CloudResources::kDetailNoiseSize / 4;    // numthreads(4,4,4)
        dispatchNoise(CloudPass::DetailNoise, resources_.detailNoise,
            detailGroups, detailGroups, detailGroups);

        const UINT weatherGroups = CloudResources::kWeatherMapSize / 8;    // numthreads(8,8,1)
        dispatchNoise(CloudPass::WeatherMap, resources_.weatherMap,
            weatherGroups, weatherGroups, 1);

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
        if (!EnsureFrameTargets(sceneColor)) {
            return;
        }

        // 出力サイズ（半解像度）を CB へ反映してから Dispatch する。
        UploadConstants();

        // ===== レイマーチ CS: BaseShapeNoise + SceneDepth → 半解像度 CloudBuffer =====
        Barrier::Transition(cmdList, resources_.cloudBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = CloudRayMarchBind;
            const CloudComputePass& pass = pipelines_[CloudPass::RayMarch];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            // 大気散乱の定数バッファと LUT（太陽色・アンビエントの単一情報源）
            binder.Set(pass.bindings[B::gAtmosphere], atmosphereManager->GetConstantBufferGPUAddress());
            binder.Set(pass.bindings[B::gBaseShapeNoise], resources_.baseShapeNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gDetailNoise], resources_.detailNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gWeatherMap], resources_.weatherMap.srv.gpuHandle);
            binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
            binder.Set(pass.bindings[B::gTransmittanceLUT], atmosphereManager->GetTransmittanceLUTSRVHandle());
            binder.Set(pass.bindings[B::gSkyViewLUT], atmosphereManager->GetSkyViewLUTSRVHandle());
            binder.Set(pass.bindings[B::gCloudOutput], resources_.cloudBuffer.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        cmdList->Dispatch(
            (resources_.TargetsWidth() + 7) / 8,
            (resources_.TargetsHeight() + 7) / 8,
            1);

        // 合成 CS が SRV として読めるよう遷移
        Barrier::UAV(cmdList, resources_.cloudBuffer);
        Barrier::Transition(cmdList, resources_.cloudBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== 合成 CS: SceneColor + CloudBuffer → 中間テクスチャ =====
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, resources_.compositeResult, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = CloudCompositeBind;
            const CloudComputePass& pass = pipelines_[CloudPass::Composite];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            binder.Set(pass.bindings[B::gSceneColor], sceneColorSrvHandle);
            binder.Set(pass.bindings[B::gCloudBuffer], resources_.cloudBuffer.srv.gpuHandle);
            binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
            binder.Set(pass.bindings[B::gOutput], resources_.compositeResult.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        DispatchComposite(cmdList);

        // ===== 結果を SceneColor へコピーバック =====
        Barrier::UAV(cmdList, resources_.compositeResult);
        Barrier::Transition(cmdList, resources_.compositeResult, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor.Get(), resources_.compositeResult.Get());

        // 後続パス（Transparent 等）に備えて元の想定状態へ戻す
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, resources_.compositeResult, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, resources_.cloudBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    bool VolumetricCloudManager::EnsureFrameTargets(GpuResource& sceneColor)
    {
        return resources_.EnsureFrameTargets(device_, descriptorAllocator_, graphicsCore_,
            sceneColor, parameters_.resolutionDivisor);
    }

    void VolumetricCloudManager::DispatchComposite(ID3D12GraphicsCommandList* cmdList) const
    {
        const D3D12_RESOURCE_DESC desc = resources_.compositeResult.Desc();
        cmdList->Dispatch(
            (static_cast<UINT>(desc.Width) + 7) / 8,
            (desc.Height + 7) / 8,
            1);
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

        {
            namespace B = CloudCubemapCaptureBind;
            const CloudComputePass& pass = pipelines_[CloudPass::CubemapCapture];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            binder.Set(pass.bindings[B::gAtmosphere], atmosphereManager->GetConstantBufferGPUAddress());
            binder.Set(pass.bindings[B::gBaseShapeNoise], resources_.baseShapeNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gDetailNoise], resources_.detailNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gWeatherMap], resources_.weatherMap.srv.gpuHandle);
            binder.Set(pass.bindings[B::gTransmittanceLUT], atmosphereManager->GetTransmittanceLUTSRVHandle());
            binder.Set(pass.bindings[B::gSkyViewLUT], atmosphereManager->GetSkyViewLUTSRVHandle());
            binder.Set(pass.bindings[B::gSkyCubemap], cubemapUav);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        constexpr uint32_t kCubemapSize = AtmosphereManager::kSkyCubemapSize;
        cmdList->Dispatch((kCubemapSize + 7) / 8, (kCubemapSize + 7) / 8, 6);
        // 後段のプリフィルタ（PrefilterSkyEnvironment）が SRV 遷移で同期するため、
        // ここでの UAV バリアは不要
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
        const float texelM = parameters_.cloudShadowRegionSizeM / static_cast<float>(CloudResources::kCloudShadowMapSize);
        g.shadowRegionCenterX = std::floor(cameraWorldPos_.x / texelM) * texelM;
        g.shadowRegionCenterZ = std::floor(cameraWorldPos_.z / texelM) * texelM;
        g.shadowRegionSizeM = parameters_.cloudShadowRegionSizeM;
        g.shadowAnchorWorldY = groundY + parameters_.layerBottomAltitudeM;

        g.intensity = parameters_.godRayIntensity;
        g.mieBoost = parameters_.godRayMieBoost;
        g.groundLevelY = groundY;
        g.edgeFadeStart = 0.8f;
        g.stepCount = std::max(parameters_.godRayStepCount, 1u);
        g.outputWidth = resources_.TargetsWidth();
        g.outputHeight = resources_.TargetsHeight();

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
        if (!EnsureFrameTargets(sceneColor)) {
            return;
        }

        // 出力サイズ（半解像度）確定後に CB を更新する
        UploadGodRayConstants();

        // ===== 雲シャドウマップ生成 =====
        // 風の移流・太陽移動・カメラ追従で毎フレーム変わるため、雲アクティブ中は毎回焼き直す
        // （1024²×24 サンプルの cheap 密度で Transmittance LUT 生成より軽い）。
        Barrier::Transition(cmdList, resources_.cloudShadowMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = CloudShadowMapBind;
            const CloudComputePass& pass = pipelines_[CloudPass::CloudShadowMap];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gCloud], constantBuffer_->GetGPUVirtualAddress());
            binder.Set(pass.bindings[B::gGodRay], godRayConstantBuffer_->GetGPUVirtualAddress());
            binder.Set(pass.bindings[B::gBaseShapeNoise], resources_.baseShapeNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gDetailNoise], resources_.detailNoise.srv.gpuHandle);
            binder.Set(pass.bindings[B::gWeatherMap], resources_.weatherMap.srv.gpuHandle);
            binder.Set(pass.bindings[B::gCloudShadowMap], resources_.cloudShadowMap.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        cmdList->Dispatch(
            (CloudResources::kCloudShadowMapSize + 7) / 8,
            (CloudResources::kCloudShadowMapSize + 7) / 8,
            1);

        // マーチ CS が SRV として読めるよう遷移
        Barrier::UAV(cmdList, resources_.cloudShadowMap);
        Barrier::Transition(cmdList, resources_.cloudShadowMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== ゴッドレイマーチ CS: 遮蔽差分を半解像度で積分 =====
        // 雲透過率（cloudBuffer.a）で差分をスケールするため SRV として読む
        // （RenderClouds が末尾で UAV 状態へ戻している）
        Barrier::Transition(cmdList, resources_.cloudBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, resources_.godRayBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = GodRayMarchBind;
            const CloudComputePass& pass = pipelines_[CloudPass::GodRayMarch];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gGodRay], godRayConstantBuffer_->GetGPUVirtualAddress());
            binder.Set(pass.bindings[B::gAtmosphere], atmosphereManager->GetConstantBufferGPUAddress());
            binder.Set(pass.bindings[B::gCloudShadowMap], resources_.cloudShadowMap.srv.gpuHandle);
            binder.Set(pass.bindings[B::gTransmittanceLUT], atmosphereManager->GetTransmittanceLUTSRVHandle());
            binder.Set(pass.bindings[B::gSceneDepth], depthSrvHandle);
            binder.Set(pass.bindings[B::gCloudBuffer], resources_.cloudBuffer.srv.gpuHandle);
            binder.Set(pass.bindings[B::gGodRayOutput], resources_.godRayBuffer.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        cmdList->Dispatch(
            (resources_.TargetsWidth() + 7) / 8,
            (resources_.TargetsHeight() + 7) / 8,
            1);

        // 合成 CS が SRV として読めるよう遷移
        Barrier::UAV(cmdList, resources_.godRayBuffer);
        Barrier::Transition(cmdList, resources_.godRayBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== 合成 CS: SceneColor + Δ輝度 → 中間テクスチャ =====
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, resources_.compositeResult, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        {
            namespace B = GodRayCompositeBind;
            const CloudComputePass& pass = pipelines_[CloudPass::GodRayComposite];
            ShaderBinder binder = pass.Begin(cmdList);
            binder.Set(pass.bindings[B::gGodRay], godRayConstantBuffer_->GetGPUVirtualAddress());
            binder.Set(pass.bindings[B::gSceneColor], sceneColorSrvHandle);
            binder.Set(pass.bindings[B::gGodRayBuffer], resources_.godRayBuffer.srv.gpuHandle);
            binder.Set(pass.bindings[B::gOutput], resources_.compositeResult.uav.gpuHandle);
            binder.ValidateBeforeDraw(pass.bindings);
        }

        DispatchComposite(cmdList);

        // ===== 結果を SceneColor へコピーバック =====
        Barrier::UAV(cmdList, resources_.compositeResult);
        Barrier::Transition(cmdList, resources_.compositeResult, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_COPY_DEST);

        cmdList->CopyResource(sceneColor.Get(), resources_.compositeResult.Get());

        // 後続パス（Transparent 等）に備えて元の想定状態へ戻す
        Barrier::Transition(cmdList, sceneColor, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Barrier::Transition(cmdList, resources_.compositeResult, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, resources_.godRayBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, resources_.cloudShadowMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, resources_.cloudBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
}
