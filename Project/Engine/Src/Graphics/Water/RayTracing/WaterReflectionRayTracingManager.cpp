#include "pch.h"
#include "WaterReflectionRayTracingManager.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <iterator>
#include <string>

#include "Graphics/Pipeline/ComputePipelineUtil.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Shader/ShaderProgram.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        struct WaterReflectionConstants {
            Matrix4x4 viewProjection;
            Matrix4x4 invViewProjection;
            float cameraPosition[3];
            float waterHeight;
            float sunDirection[3];            // メインライトの向き（光源→シーン・正規化済み）
            float surfaceBias;
            float maxRayDistance;
            // 1 = 空キューブマップが有効。RT パス側が「トレースしたレイの向き」で空を解決するのに使う
            float skyEnvReflectionEnabled;
            float screenWidth;
            float screenHeight;
            float maxReflectionOffsetPixels;
            uint32_t sunShadowEnabled;        // 1 なら水面の点から光源へ影のレイを撃つ
            float debugDisplayScale;
            uint32_t debugViewMode;
        };

        static constexpr Cb::Field kWaterReflectionConstantsFields[] = {
            CB_FIELD(WaterReflectionConstants, viewProjection),
            CB_FIELD(WaterReflectionConstants, invViewProjection),
            CB_FIELD(WaterReflectionConstants, cameraPosition), CB_FIELD(WaterReflectionConstants, waterHeight),
            CB_FIELD(WaterReflectionConstants, sunDirection), CB_FIELD(WaterReflectionConstants, surfaceBias),
            CB_FIELD(WaterReflectionConstants, maxRayDistance),
            CB_FIELD(WaterReflectionConstants, skyEnvReflectionEnabled),
            CB_FIELD(WaterReflectionConstants, screenWidth), CB_FIELD(WaterReflectionConstants, screenHeight),
            CB_FIELD(WaterReflectionConstants, maxReflectionOffsetPixels),
            CB_FIELD(WaterReflectionConstants, sunShadowEnabled),
            CB_FIELD(WaterReflectionConstants, debugDisplayScale),
            CB_FIELD(WaterReflectionConstants, debugViewMode),
        };
        CB_VERIFY_LAYOUT(WaterReflectionConstants, kWaterReflectionConstantsFields);
        CB_BIND_HLSL(WaterReflectionConstants, kWaterReflectionConstantsFields, "WaterReflectionConstants");

        /// @brief 縮小段のコンピュート（WaterReflectionColorPyramid.CS.hlsl）の PyramidConstants
        struct ColorPyramidConstants {
            uint32_t destWidth;
            uint32_t destHeight;
            float destInvWidth;
            float destInvHeight;
            uint32_t fromSceneColor;          // 1 = 段 0（画面の写しから作り、画素を物・空・水面より下に分ける）
            float waterHeight;
            float waterRegionCenterXZ[2];
            float waterRegionHalfExtentXZ[2]; // 0 なら水域の制限なし
            float pad[2];
            Matrix4x4 invViewProjection;
        };
        static constexpr Cb::Field kColorPyramidConstantsFields[] = {
            CB_FIELD(ColorPyramidConstants, destWidth), CB_FIELD(ColorPyramidConstants, destHeight),
            CB_FIELD(ColorPyramidConstants, destInvWidth), CB_FIELD(ColorPyramidConstants, destInvHeight),
            CB_FIELD(ColorPyramidConstants, fromSceneColor), CB_FIELD(ColorPyramidConstants, waterHeight),
            CB_FIELD(ColorPyramidConstants, waterRegionCenterXZ),
            CB_FIELD(ColorPyramidConstants, waterRegionHalfExtentXZ), CB_FIELD(ColorPyramidConstants, pad),
            CB_FIELD(ColorPyramidConstants, invViewProjection),
        };
        CB_VERIFY_LAYOUT(ColorPyramidConstants, kColorPyramidConstantsFields);
        CB_BIND_HLSL(ColorPyramidConstants, kColorPyramidConstantsFields, "PyramidConstants");

        /// @brief 縮小段のコンピュートが要求するリソースの契約
        namespace ColorPyramidBind {
            enum Slot : size_t {
                gPyramidSource, gPyramidSceneDepth, gPyramidSkySource, gPyramidDest, gPyramidSkyDest,
                PyramidConstants, Count };
            inline constexpr ShaderBindingDecl kDecls[] = {
                { "gPyramidSource",     ShaderBindingType::SRV, BindingUsage::Required },
                { "gPyramidSceneDepth", ShaderBindingType::SRV, BindingUsage::Required },
                { "gPyramidSkySource",  ShaderBindingType::SRV, BindingUsage::Required },
                { "gPyramidDest",       ShaderBindingType::UAV, BindingUsage::Required },
                { "gPyramidSkyDest",    ShaderBindingType::UAV, BindingUsage::Required },
                { "PyramidConstants",   ShaderBindingType::CBV, BindingUsage::Required },
            };
            static_assert(std::size(kDecls) == Slot::Count, "kDecls と Slot の並びがずれている");
        }
    }

    static_assert(sizeof(WaterReflectionConstants) == 192,
        "WaterReflectionConstants size mismatch with HLSL cbuffer");

    bool WaterReflectionRayTracingManager::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr,
        ShaderProgramCache* shaderProgramCache)
    {
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterReflectionRayTracingManager";
        desc.outputDebugName = "RTWaterReflection";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterReflection.hlsl";
        desc.rayGenName = L"RTWaterReflectionRayGen";
        desc.missName = L"RTWaterReflectionMiss";
        desc.hitGroupName = L"RTWaterReflectionHitGroup";
        desc.closestHitName = L"RTWaterReflectionClosestHit";
        desc.outputUavName = "gReflectionOutput";
        // gSkyEnvironmentMap は t5。空をこのパス内で解決するために追加した
        // （Water.PS 側で平面法線の空と混ぜると二重像になるため）。
        static constexpr const char* kSrvTableNames[] = {
            "gSceneDepth", "gSceneColor", "gFFTOceanDisplacement", "gFFTOceanNormal", "gSkyEnvironmentMap",
            "gSceneColorPyramid", "gSkyCoveragePyramid" };
        desc.srvTableNames = kSrvTableNames;
        desc.constantsName = "WaterReflectionConstants";
        desc.constantsBytes = sizeof(WaterReflectionConstants);
        desc.secondaryOutputUavName = "gSunVisibilityOutput";
        // RTReflectionPayload {hitT, hitFlag, instanceIndex, primitiveIndex, barycentrics}
        desc.payloadBytes = sizeof(float) * 2 + sizeof(uint32_t) * 2 + sizeof(float) * 2;
        // 当たった点を照らすときに表・テクスチャ・ライトをヒープから番号で引く
        desc.extraConstantBufferName = "RTHitShadingConstants";
        desc.directlyIndexedHeap = true;
        if (!InitializeFromDesc(dxCommon, descriptorAllocator, asMgr, shaderProgramCache, desc)) {
            return false;
        }

        // 縮小段が作れなくても反射そのものは動く（当たった物をぼかさずに引く）
        colorPyramidPipelineReady_ = InitializeColorPyramid();
        return true;
    }

    bool WaterReflectionRayTracingManager::InitializeColorPyramid()
    {
        Logger& log = Logger::GetInstance();
        const ShaderProgram* program = shaderProgramCache_->GetOrCreateCompute(
            L"Engine/Assets/Shaders/Water/RayTracing/WaterReflectionColorPyramid.CS.hlsl",
            "WaterReflectionColorPyramid", L"cs_6_6");
        if (!program) {
            log.Warnf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "{}: color pyramid shader compile failed. reflections are not blurred.", GetOwnerName());
            return false;
        }

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);  // コンピュートに入力アセンブラは無い
        config.ConfigureResource(ResourceBindingConfig("PyramidConstants", BindingStrategy::RootConstants));
        config.ConfigureSampler("gLinearClamp", SamplerConfig::LinearClamp());
        const auto buildResult = colorPyramidRootSigMgr_.Build(
            dxCommon_->GetDevice(), program->GetReflection(), config);
        if (!buildResult.success) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "{}: color pyramid root signature build failed: {}", GetOwnerName(), buildResult.errorMessage);
            return false;
        }

        try {
            colorPyramidBindings_ = BindingTable::Resolve(
                program->GetReflection(), ColorPyramidBind::kDecls, ColorPyramidBind::Slot::Count,
                "WaterReflectionColorPyramid");
        }
        catch (const std::exception& e) {
            log.Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "{}: color pyramid binding contract violation: {}", GetOwnerName(), e.what());
            return false;
        }

        colorPyramidPipelineState_ = ComputePipelineUtil::Create(
            dxCommon_->GetDevice(), colorPyramidRootSigMgr_.GetRootSignature(), program->GetCS(),
            "WaterReflectionColorPyramid");
        return colorPyramidPipelineState_ != nullptr;
    }

    bool WaterReflectionRayTracingManager::EnsureColorPyramid(UINT screenWidth, UINT screenHeight)
    {
        // 段 0 は画面の半分の解像度
        const UINT width = (std::max)((screenWidth + 1) / 2, 1u);
        const UINT height = (std::max)((screenHeight + 1) / 2, 1u);
        if (colorPyramid_.IsValid() && width == colorPyramidWidth_ && height == colorPyramidHeight_) {
            return true;
        }

        uint32_t mipCount = 1;
        for (UINT size = (std::max)(width, height); size > 1 && mipCount < kColorPyramidMaxMips; size >>= 1) {
            ++mipCount;
        }

        const bool created =
            CreatePyramidTexture(
                colorPyramid_, colorPyramidMipSrv_, colorPyramidMipUav_, colorPyramidFullSrv_,
                DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, mipCount, "RTWaterReflectionPyramid") &&
            CreatePyramidTexture(
                skyCoveragePyramid_, skyCoveragePyramidMipSrv_, skyCoveragePyramidMipUav_,
                skyCoveragePyramidFullSrv_, DXGI_FORMAT_R16_FLOAT, width, height, mipCount,
                "RTWaterReflectionSkyPyramid");
        if (!created) {
            colorPyramid_.Release();
            skyCoveragePyramid_.Release();
            colorPyramidWidth_ = 0;
            colorPyramidHeight_ = 0;
            colorPyramidMipCount_ = 0;
            return false;
        }

        colorPyramidWidth_ = width;
        colorPyramidHeight_ = height;
        colorPyramidMipCount_ = mipCount;
        return true;
    }

    bool WaterReflectionRayTracingManager::CreatePyramidTexture(
        GpuResource& texture,
        std::array<DescriptorHandle, kColorPyramidMaxMips>& mipSrv,
        std::array<DescriptorHandle, kColorPyramidMaxMips>& mipUav,
        DescriptorHandle& fullSrv,
        DXGI_FORMAT format,
        UINT width,
        UINT height,
        uint32_t mipCount,
        const char* debugName)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = static_cast<UINT16>(mipCount);
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        // 段数ぶんのサブリソースを個別に追跡させる
        texture.Reset(
            ResourceFactory::CreateTextureResource(
                dxCommon_->GetDevice(), desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            mipCount);
        if (!texture) {
            return false;
        }

        const std::string name(debugName);
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MostDetailedMip = mip;
            srvDesc.Texture2D.MipLevels = 1;
            descriptorAllocator_->EnsureSRV(mipSrv[mip], texture.Get(), srvDesc, name + "MipSRV");

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = mip;
            descriptorAllocator_->EnsureUAV(mipUav[mip], texture.Get(), uavDesc, name + "MipUAV");
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = mipCount;
        descriptorAllocator_->EnsureSRV(fullSrv, texture.Get(), srvDesc, name + "SRV");
        return true;
    }

    void WaterReflectionRayTracingManager::BuildColorPyramid(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sourceSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        const Matrix4x4& invViewProjection,
        const WaterSurfaceData& surfaceData)
    {
        cmdList->SetComputeRootSignature(colorPyramidRootSigMgr_.GetRootSignature());
        cmdList->SetPipelineState(colorPyramidPipelineState_.Get());

        // 全段を書き込み状態にし、段を進めるたびに 1 段上だけを読み取り状態へ移す
        Barrier::Transition(cmdList, colorPyramid_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, skyCoveragePyramid_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        for (uint32_t mip = 0; mip < colorPyramidMipCount_; ++mip) {
            D3D12_GPU_DESCRIPTOR_HANDLE source = sourceSRV;
            // 段 0 は 1 段上の空を読まない（同じ型の深度を差しておく）
            D3D12_GPU_DESCRIPTOR_HANDLE skySource = sceneDepthSRV;
            if (mip > 0) {
                Barrier::Transition(cmdList, colorPyramid_,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, mip - 1);
                Barrier::Transition(cmdList, skyCoveragePyramid_,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, mip - 1);
                source = colorPyramidMipSrv_[mip - 1].gpuHandle;
                skySource = skyCoveragePyramidMipSrv_[mip - 1].gpuHandle;
            }

            const UINT destWidth = (std::max)(colorPyramidWidth_ >> mip, 1u);
            const UINT destHeight = (std::max)(colorPyramidHeight_ >> mip, 1u);
            ColorPyramidConstants constants{};
            constants.destWidth = destWidth;
            constants.destHeight = destHeight;
            constants.destInvWidth = 1.0f / static_cast<float>(destWidth);
            constants.destInvHeight = 1.0f / static_cast<float>(destHeight);
            constants.fromSceneColor = (mip == 0) ? 1u : 0u;
            constants.waterHeight = surfaceData.waterHeight;
            if (surfaceData.regionValid != 0) {
                constants.waterRegionCenterXZ[0] = surfaceData.regionCenterXZ[0];
                constants.waterRegionCenterXZ[1] = surfaceData.regionCenterXZ[1];
                constants.waterRegionHalfExtentXZ[0] = surfaceData.regionHalfExtentXZ[0];
                constants.waterRegionHalfExtentXZ[1] = surfaceData.regionHalfExtentXZ[1];
            }
            constants.invViewProjection = invViewProjection;

            ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
            binder.Set(colorPyramidBindings_[ColorPyramidBind::gPyramidSource], source);
            binder.Set(colorPyramidBindings_[ColorPyramidBind::gPyramidSceneDepth], sceneDepthSRV);
            binder.Set(colorPyramidBindings_[ColorPyramidBind::gPyramidSkySource], skySource);
            binder.Set(colorPyramidBindings_[ColorPyramidBind::gPyramidDest], colorPyramidMipUav_[mip].gpuHandle);
            binder.Set(colorPyramidBindings_[ColorPyramidBind::gPyramidSkyDest],
                skyCoveragePyramidMipUav_[mip].gpuHandle);
            binder.SetConstants(colorPyramidBindings_[ColorPyramidBind::PyramidConstants], constants);
            binder.ValidateBeforeDraw(colorPyramidBindings_);
            cmdList->Dispatch((destWidth + 7) / 8, (destHeight + 7) / 8, 1);

            // 次の段が読む前に書き込みの完了を待つ
            Barrier::UAV(cmdList, colorPyramid_);
            Barrier::UAV(cmdList, skyCoveragePyramid_);
        }
        Barrier::Transition(cmdList, colorPyramid_,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, colorPyramidMipCount_ - 1);
        Barrier::Transition(cmdList, skyCoveragePyramid_,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, colorPyramidMipCount_ - 1);
    }

    void WaterReflectionRayTracingManager::Resize(UINT width, UINT height, ViewID viewId)
    {
        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        ReleaseOutputIfSizeMismatchBase(width, height, viewIndex);
        outputViews_.ReleaseIfSizeMismatch(width, height, kSunVisibilitySlotBase + viewIndex);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterReflectionRayTracingManager::GetSunVisibilitySRVHandle(ViewID viewId) const
    {
        return outputViews_.GetSRVHandle(kSunVisibilitySlotBase + static_cast<uint32_t>(viewId));
    }

    GpuResource& WaterReflectionRayTracingManager::GetSunVisibilityResource(ViewID viewId)
    {
        return outputViews_.Resource(kSunVisibilitySlotBase + static_cast<uint32_t>(viewId));
    }

    D3D12_GPU_DESCRIPTOR_HANDLE WaterReflectionRayTracingManager::GetReflectionSRVHandle(ViewID viewId) const
    {
        return GetOutputSRVHandleBase(static_cast<uint32_t>(viewId));
    }

    GpuResource& WaterReflectionRayTracingManager::GetReflectionResource(ViewID viewId)
    {
        return GetOutputBase(static_cast<uint32_t>(viewId));
    }

    void WaterReflectionRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV,
        const Matrix4x4& viewProjection,
        const Vector3& cameraPosition,
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput,
        D3D12_GPU_DESCRIPTOR_HANDLE skyEnvironmentSRV,
        const WaterSunShadowInput& sunShadow,
        const WaterHitShadingInput& hitShading,
        UINT width,
        UINT height,
        ViewID viewId)
    {
        WaterSurfaceData resolvedSurfaceData{};
        const WaterSurfaceData& dispatchSurfaceData =
            ResolveSurfaceDataForDispatch(surfaceData, resolvedSurfaceData);

        const uint32_t viewIndex = static_cast<uint32_t>(viewId);
        BeginDiagnostics(viewIndex, width, height, dispatchSurfaceData, sceneDepthSRV, sceneColorSRV);

        DispatchResources resources;
        if (!BeginDispatch(cmdList, width, height, viewIndex, resources)) {
            return;
        }

        // 水面の日向率（反射と同じ解像度の 1 チャンネル）
        const uint32_t sunVisibilitySlot = kSunVisibilitySlotBase + viewIndex;
        RayTracingOutputViewSet::TextureOptions sunVisibilityOptions{};
        sunVisibilityOptions.format = DXGI_FORMAT_R8_UNORM;
        if (!outputViews_.EnsureTexture(
                dxCommon_, descriptorAllocator_, width, height, sunVisibilitySlot, GetOwnerName(),
                "RTWaterSunVisibility_v" + std::to_string(viewIndex), sunVisibilityOptions)) {
            lastDispatchInfo_.status = RayTracingDispatchStatus::OutputAllocationFailed;
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: sun visibility texture allocation failed. dispatch skipped.",
                GetOwnerName());
            return;
        }

        WaterReflectionConstants constants{};
        constants.viewProjection = viewProjection;
        constants.invViewProjection = MathCore::Matrix::Inverse(viewProjection);
        constants.cameraPosition[0] = cameraPosition.x;
        constants.cameraPosition[1] = cameraPosition.y;
        constants.cameraPosition[2] = cameraPosition.z;
        constants.waterHeight = dispatchSurfaceData.waterHeight;
        constants.sunDirection[0] = sunShadow.direction.x;
        constants.sunDirection[1] = sunShadow.direction.y;
        constants.sunDirection[2] = sunShadow.direction.z;
        constants.surfaceBias = settings_.surfaceBias;
        constants.maxRayDistance = settings_.maxRayDistance;
        // 空キューブが渡っていないフレームはシェーダー側が理由コードへ落とし、
        // Water.PS の保険フォールバック（波法線で引く空 / PBR 出力）が動く。
        const bool hasSkyEnvironment = (skyEnvironmentSRV.ptr != 0);
        constants.skyEnvReflectionEnabled = hasSkyEnvironment ? 1.0f : 0.0f;
        constants.screenWidth = static_cast<float>(width);
        constants.screenHeight = static_cast<float>(height);
        constants.maxReflectionOffsetPixels = settings_.maxReflectionOffsetPixels;
        constants.sunShadowEnabled = sunShadow.enabled ? 1u : 0u;
        constants.debugDisplayScale = settings_.debugDisplayScale;
        constants.debugViewMode = settings_.debugViewMode;

        const D3D12_GPU_DESCRIPTOR_HANDLE fftDisplacementSRV =
            (fftOceanInput.displacementSRV.ptr != 0) ? fftOceanInput.displacementSRV : sceneColorSRV;
        const D3D12_GPU_DESCRIPTOR_HANDLE fftNormalSRV =
            (fftOceanInput.normalSRV.ptr != 0) ? fftOceanInput.normalSRV : sceneColorSRV;
        // 未取得のときはダミーを差す（FFT と同じ規約）。実際に読むかは
        // skyEnvReflectionEnabled でシェーダー側が判断する。
        const D3D12_GPU_DESCRIPTOR_HANDLE skyEnvSRV =
            hasSkyEnvironment ? skyEnvironmentSRV : sceneColorSRV;

        const SurfaceConstantsUpload surface = UploadSurfaceDataForDispatch(dispatchSurfaceData, fftOceanInput);

        // 反射の元画像の縮小段を作る。作れないフレームは元画像そのもの（段数 1）と深度を差し、
        // シェーダーはぼかさずに引く
        D3D12_GPU_DESCRIPTOR_HANDLE colorPyramidSRV = sceneColorSRV;
        D3D12_GPU_DESCRIPTOR_HANDLE skyCoveragePyramidSRV = sceneDepthSRV;
        if (colorPyramidPipelineReady_ && EnsureColorPyramid(width, height)) {
            BuildColorPyramid(
                cmdList, sceneColorSRV, sceneDepthSRV, constants.invViewProjection, dispatchSurfaceData);
            colorPyramidSRV = colorPyramidFullSrv_.gpuHandle;
            skyCoveragePyramidSRV = skyCoveragePyramidFullSrv_.gpuHandle;
        }

        BindAndDispatchRays(
            cmdList,
            resources,
            surface.address,
            {
                { "gSceneDepth", sceneDepthSRV },
                { "gSceneColor", sceneColorSRV },
                { "gFFTOceanDisplacement", fftDisplacementSRV },
                { "gFFTOceanNormal", fftNormalSRV },
                { "gSkyEnvironmentMap", skyEnvSRV },
                { "gSceneColorPyramid", colorPyramidSRV },
                { "gSkyCoveragePyramid", skyCoveragePyramidSRV },
            },
            &constants,
            width,
            height,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            { &outputViews_.Resource(sunVisibilitySlot), outputViews_.GetUAVHandle(sunVisibilitySlot) },
            UploadHitShadingConstants(hitShading));
    }
}
