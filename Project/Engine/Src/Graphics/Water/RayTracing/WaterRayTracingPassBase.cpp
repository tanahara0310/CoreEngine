#include "pch.h"
#include "WaterRayTracingPassBase.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include <dxcapi.h>

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RayTracingPipelineBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        /// @brief ヒットシェーディングの定数（Include/RayTracing/RTHitShading.hlsli の RTHitShadingConstants）
        struct RTHitShadingConstants {
            uint32_t hitInstanceTableIndex;
            uint32_t hitSubMeshTableIndex;
            uint32_t directionalLightsIndex;
            uint32_t directionalLightCount;
            uint32_t skyIrradianceSHIndex;
            uint32_t skyAmbientEnabled;
            float skyAmbientScale;
            uint32_t hitShadingEnabled;
            uint32_t skySpecularMapIndex;
            uint32_t skySpecularEnabled;
            uint32_t cloudShadowMapIndex;
            float cloudShadowStrength;
            float cloudShadowRegionCenterXZ[2];
            float cloudShadowRegionSize;
            float cloudShadowAnchorY;
            float cloudShadowEdgeFadeStart;
            float pad[3];
        };
        static constexpr Cb::Field kRTHitShadingConstantsFields[] = {
            CB_FIELD(RTHitShadingConstants, hitInstanceTableIndex),
            CB_FIELD(RTHitShadingConstants, hitSubMeshTableIndex),
            CB_FIELD(RTHitShadingConstants, directionalLightsIndex),
            CB_FIELD(RTHitShadingConstants, directionalLightCount),
            CB_FIELD(RTHitShadingConstants, skyIrradianceSHIndex),
            CB_FIELD(RTHitShadingConstants, skyAmbientEnabled),
            CB_FIELD(RTHitShadingConstants, skyAmbientScale),
            CB_FIELD(RTHitShadingConstants, hitShadingEnabled),
            CB_FIELD(RTHitShadingConstants, skySpecularMapIndex),
            CB_FIELD(RTHitShadingConstants, skySpecularEnabled),
            CB_FIELD(RTHitShadingConstants, cloudShadowMapIndex),
            CB_FIELD(RTHitShadingConstants, cloudShadowStrength),
            CB_FIELD(RTHitShadingConstants, cloudShadowRegionCenterXZ),
            CB_FIELD(RTHitShadingConstants, cloudShadowRegionSize),
            CB_FIELD(RTHitShadingConstants, cloudShadowAnchorY),
            CB_FIELD(RTHitShadingConstants, cloudShadowEdgeFadeStart),
            CB_FIELD(RTHitShadingConstants, pad),
        };
        CB_VERIFY_LAYOUT(RTHitShadingConstants, kRTHitShadingConstantsFields);
        CB_BIND_HLSL(RTHitShadingConstants, kRTHitShadingConstantsFields, "RTHitShadingConstants");
    }

    // 構成データ 1 つから DXR パイプライン・シェーダーテーブル・出力ビューを丸ごと組む。
    // 屈折・反射・コースティクスの 3 マネージャはこの関数への引数だけが違う
    bool WaterRayTracingPassBase::InitializeFromDesc(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr,
        ShaderProgramCache* shaderProgramCache,
        const RTWaterPipelineDesc& desc)
    {
        Logger& log = Logger::GetInstance();

        if (!InitializeBase(dxCommon, descriptorAllocator, asMgr, shaderProgramCache,
            desc.ownerName, desc.outputDebugName)) {
            log.Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: DXR unsupported. initialization skipped.",
                desc.ownerName);
            return false;
        }

        // lib_6_6 のコンパイルとリフレクションはキャッシュが担当する
        const ShaderProgram* program =
            shaderProgramCache_->GetOrCreateLibrary(desc.shaderPath, desc.ownerName);
        IDxcBlob* shaderBlob = program ? program->GetCS() : nullptr;
        if (!shaderBlob) {
            log.Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: shader compile failed.",
                desc.ownerName);
            return false;
        }

        // 宣言表を組み立てる。名前はすべて静的記憶域（RTWaterPipelineDesc の契約）なので
        // BindingTable がポインタを保持しても問題ない
        declStorage_.clear();
        declStorage_.push_back({ desc.outputUavName, ShaderBindingType::UAV, BindingUsage::Required });
        declStorage_.push_back({ "gScene",           ShaderBindingType::SRV, BindingUsage::Required });
        for (const char* srvName : desc.srvTableNames) {
            declStorage_.push_back({ srvName, ShaderBindingType::SRV, BindingUsage::Required });
        }
        // HLSL 側の実名は cbuffer WaterSurfaceData（RTWaterSurfaceCommon.hlsli:16）
        declStorage_.push_back({ "WaterSurfaceData", ShaderBindingType::CBV, BindingUsage::Required });
        declStorage_.push_back({ desc.constantsName,  ShaderBindingType::CBV, BindingUsage::Required });

        // 2 枚目の出力と追加の CBV は末尾に置く（それ以外の添字は 1 枚だけのパスと同じ）
        hasSecondaryOutput_ = (desc.secondaryOutputUavName != nullptr);
        if (hasSecondaryOutput_) {
            declStorage_.push_back({ desc.secondaryOutputUavName, ShaderBindingType::UAV, BindingUsage::Required });
        }
        hasExtraConstantBuffer_ = (desc.extraConstantBufferName != nullptr);
        if (hasExtraConstantBuffer_) {
            declStorage_.push_back({ desc.extraConstantBufferName, ShaderBindingType::CBV, BindingUsage::Required });
        }

        // 添字は宣言した順。ディスパッチ側はこれで引く
        slotOutputUav_ = 0;
        slotScene_ = 1;
        slotSrvFirst_ = 2;
        slotSurfaceData_ = 2 + desc.srvTableNames.size();
        slotConstants_ = slotSurfaceData_ + 1;
        slotSecondaryOutputUav_ = slotConstants_ + 1;
        slotExtraConstantBuffer_ = slotConstants_ + (hasSecondaryOutput_ ? 2 : 1);

        RootSignatureConfig config;
        // DXR に入力アセンブラは無い。ヒープを直接引くパスだけフラグを立てる
        config.SetFlags(desc.directlyIndexedHeap
            ? D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
            : D3D12_ROOT_SIGNATURE_FLAG_NONE);
        {
            ResourceBindingConfig rc(desc.constantsName, BindingStrategy::RootConstants);
            rc.rootConstantsCount = static_cast<UINT>(desc.constantsBytes / sizeof(uint32_t));
            config.ConfigureResource(rc);
        }
        // 空キューブマップの SampleLevel 用。宣言しないシェーダーには影響しない
        config.ConfigureSampler("gLinearClamp", SamplerConfig::LinearClamp());
        // 物のテクスチャを繰り返して引く。宣言しないシェーダーには影響しない
        config.ConfigureSampler("gLinearWrap", SamplerConfig::Linear());

        if (!BuildGlobalRootSignature(*program, declStorage_.data(), declStorage_.size(), config)) {
            return false;
        }

        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob)
            .AddHitGroup({ desc.hitGroupName, desc.closestHitName })
            .SetShaderConfig(desc.payloadBytes)
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        if (!pipelineBuilder.Build(dxCommon_->GetDevice(), stateObject_, stateObjectProperties_)) {
            log.Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: state object build failed.",
                desc.ownerName);
            return false;
        }

        shaderTableBuilder_
            .SetRayGenShader(desc.rayGenName)
            .AddMissShader(desc.missName)
            .AddHitGroup(desc.hitGroupName);
        if (!shaderTableBuilder_.Build(dxCommon_->GetDevice(), stateObjectProperties_.Get())) {
            log.Errorf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: shader table build failed.",
                desc.ownerName);
            return false;
        }

        outputUavName_ = desc.outputUavName;
        constantsName_ = desc.constantsName;
        constantsBytes_ = desc.constantsBytes;

        isInitialized_ = true;
        log.Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "{}: initialized successfully.",
            desc.ownerName);
        return true;
    }

    // ルートシグネチャへ SRV / UAV / CBV を順に差して DispatchRays する。
    // バインド順は RTWaterPipelineDesc の宣言順と 1 対 1（ずれるとシェーダーが別リソースを読む）
    void WaterRayTracingPassBase::BindAndDispatchRays(
        ID3D12GraphicsCommandList* cmdList,
        DispatchResources& resources,
        std::initializer_list<RTWaterSrvBinding> srvBindings,
        const void* constantsBlob,
        UINT width,
        UINT height,
        D3D12_RESOURCE_STATES finalState,
        const RTWaterSecondaryOutput& secondaryOutput,
        D3D12_GPU_VIRTUAL_ADDRESS extraConstantBuffer)
    {
        resources.cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());
        resources.cmdList4->SetPipelineState1(stateObject_.Get());

        const bool writesSecondaryOutput = hasSecondaryOutput_ && secondaryOutput.resource;
        assert((!hasSecondaryOutput_ || writesSecondaryOutput) && "2 枚目の出力が渡されていない");
        assert((!hasExtraConstantBuffer_ || extraConstantBuffer != 0) && "追加の CBV が渡されていない");

        BeginOutputWrite(cmdList, *resources.output);
        if (writesSecondaryOutput) {
            BeginOutputWrite(cmdList, *secondaryOutput.resource);
        }

        // 差し方は RootSlot の種別から ShaderBinder が決める
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Compute);
        binder.Set(bindings_[slotOutputUav_], resources.outputUavHandle);
        binder.Set(bindings_[slotScene_], asMgr_->GetTLASSRVHandle());
        if (writesSecondaryOutput) {
            binder.Set(bindings_[slotSecondaryOutputUav_], secondaryOutput.uavHandle);
        }

        // srvBindings は宣言表（desc.srvTableNames）と同じ並びで渡される契約。
        // 並びが食い違うと別のテクスチャが差さるので、名前で照合して落とす
        size_t srvIndex = slotSrvFirst_;
        for (const RTWaterSrvBinding& binding : srvBindings) {
#if CB_REFLECTION_CHECK_ENABLED
            assert(srvIndex < slotSurfaceData_ && "SRV バインドの数が宣言表より多い");
            assert(std::strcmp(declStorage_[srvIndex].name, binding.name) == 0 &&
                "SRV バインドの並びが srvTableNames と食い違っている");
#endif
            binder.Set(bindings_[srvIndex++], binding.handle);
        }
        binder.Set(bindings_[slotSurfaceData_], constantBuffer_->GetGPUVirtualAddress());
        binder.SetConstants(
            bindings_[slotConstants_], constantsBlob, constantsBytes_ / sizeof(uint32_t));
        if (hasExtraConstantBuffer_ && extraConstantBuffer != 0) {
            binder.Set(bindings_[slotExtraConstantBuffer_], extraConstantBuffer);
        }
        binder.ValidateBeforeDraw(bindings_);

        auto dispatchDesc = shaderTableBuilder_.BuildDispatchDesc(width, height);
        resources.cmdList4->DispatchRays(&dispatchDesc);
        lastDispatchInfo_.status = RayTracingDispatchStatus::Dispatched;

        EndOutputWrite(cmdList, *resources.output, finalState);
        if (writesSecondaryOutput) {
            EndOutputWrite(cmdList, *secondaryOutput.resource, finalState);
        }
    }

    // 共通基盤のガード判定に、水面固有の前提（供給元の有無）を足したもの
    bool WaterRayTracingPassBase::BeginDispatch(
        ID3D12GraphicsCommandList* cmdList,
        UINT width,
        UINT height,
        uint32_t viewIndex,
        DispatchResources& outResources,
        DXGI_FORMAT format)
    {
        return BeginDispatchBase(
            cmdList, width, height, viewIndex, outResources, format, GetSurfaceConstantBufferSize());
    }

    // 水面固有の診断情報（水面高さ・有効波数）を記録する
    void WaterRayTracingPassBase::BeginDiagnostics(
        uint32_t viewIndex,
        UINT width,
        UINT height,
        const WaterSurfaceData& surfaceData,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSRV,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSRV)
    {
        BeginDiagnosticsBase(viewIndex, width, height);

        lastWaterHeight_ = surfaceData.waterHeight;
        lastActiveWaveCount_ = surfaceData.activeWaveCount;

        // 水面固有の値は共通型の extras に載せる（デバッグ UI はパスの種類を知らずに表へ出せる）
        lastDispatchInfo_.AddExtra("waterHeight", surfaceData.waterHeight);
        lastDispatchInfo_.AddExtra("activeWaves", static_cast<float>(surfaceData.activeWaveCount));
        lastDispatchInfo_.AddExtra("sceneDepthSrv", sceneDepthSRV.ptr != 0 ? 1.0f : 0.0f);
        lastDispatchInfo_.AddExtra("sceneColorSrv", sceneColorSRV.ptr != 0 ? 1.0f : 0.0f);
    }

    // 供給元の水面データを、シェーダー側 cbuffer のレイアウトへ詰め替える
    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::BuildSurfaceConstants(
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput) const
    {
        WaterSurfaceConstants surfaceConstants{};
        surfaceConstants.waterHeight = surfaceData.waterHeight;
        surfaceConstants.activeWaveCount = (std::min)(surfaceData.activeWaveCount, kMaxWaterSurfaceWaveCount);
        surfaceConstants.time = surfaceData.time;
        surfaceConstants.simulationType = surfaceData.simulationType;
        for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
            surfaceConstants.waves[waveIndex] = surfaceData.waves[waveIndex];
        }
        surfaceConstants.fftOceanEnabled = fftOceanInput.enabled;
        surfaceConstants.fftOceanResolution = fftOceanInput.resolution;
        surfaceConstants.meshSubdivisions = surfaceData.meshSubdivisions;
        for (int c = 0; c < 3; ++c) {
            surfaceConstants.cascadeMeanSquareSlope[c] = fftOceanInput.cascadeMeanSquareSlope[c];
        }
        surfaceConstants.regionValid = surfaceData.regionValid;
        for (int c = 0; c < 2; ++c) {
            surfaceConstants.regionCenterXZ[c] = surfaceData.regionCenterXZ[c];
            surfaceConstants.regionHalfExtentXZ[c] = surfaceData.regionHalfExtentXZ[c];
        }
        return surfaceConstants;
    }

    // 水面定数を GPU へ転送する（バッファ未確保なら警告して何もしない）
    void WaterRayTracingPassBase::UploadSurfaceConstants(const WaterSurfaceConstants& surfaceConstants) const
    {
        if (!constantBufferMapped_) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Buffer,
                "{}: surface constant upload skipped. constant buffer is not mapped.",
                GetOwnerName());
            return;
        }

        std::memcpy(constantBufferMapped_, &surfaceConstants, sizeof(surfaceConstants));
    }

    // 供給元から水面状態を取り出して定数バッファへ載せ、載せた内容を返す
    WaterRayTracingPassBase::WaterSurfaceConstants WaterRayTracingPassBase::UploadSurfaceDataForDispatch(
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput) const
    {
        const WaterSurfaceConstants surfaceConstants = BuildSurfaceConstants(surfaceData, fftOceanInput);
        UploadSurfaceConstants(surfaceConstants);
        return surfaceConstants;
    }

    bool WaterRayTracingPassBase::InitializeHitShadingConstants()
    {
        static_assert(sizeof(RTHitShadingConstants) <= kHitShadingConstantsStride,
            "RTHitShadingConstants does not fit in one constant buffer slot");
        const size_t totalBytes = static_cast<size_t>(kHitShadingConstantsStride) * kMaxFramesInFlight
            * static_cast<size_t>(RTWaterViewID::Count);
        hitShadingConstants_ = ResourceFactory::CreateBufferResource(dxCommon_->GetDevice(), totalBytes);
        if (!hitShadingConstants_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "{}: hit shading constant buffer creation failed.", GetOwnerName());
            return false;
        }
        void* mapped = nullptr;
        if (FAILED(hitShadingConstants_->Map(0, nullptr, &mapped)) || !mapped) {
            hitShadingConstants_.Reset();
            return false;
        }
        hitShadingConstantsMapped_ = static_cast<uint8_t*>(mapped);
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS WaterRayTracingPassBase::UploadHitShadingConstants(
        const WaterHitShadingInput& input, uint32_t viewIndex)
    {
        RTHitShadingConstants constants{};
        constants.hitInstanceTableIndex = input.instanceTableIndex;
        constants.hitSubMeshTableIndex = input.subMeshTableIndex;
        constants.directionalLightsIndex = input.directionalLightsIndex;
        constants.directionalLightCount = input.directionalLightCount;
        constants.skyIrradianceSHIndex = input.skyIrradianceSHIndex;
        constants.skyAmbientEnabled = input.skyAmbientEnabled ? 1u : 0u;
        constants.skyAmbientScale = input.skyAmbientScale;
        constants.hitShadingEnabled = input.enabled ? 1u : 0u;
        constants.skySpecularMapIndex = input.skySpecularMapIndex;
        constants.skySpecularEnabled = input.skySpecularEnabled ? 1u : 0u;
        constants.cloudShadowMapIndex = input.cloudShadowMapIndex;
        constants.cloudShadowStrength = input.cloudShadowStrength;
        constants.cloudShadowRegionCenterXZ[0] = input.cloudShadowRegionCenterXZ[0];
        constants.cloudShadowRegionCenterXZ[1] = input.cloudShadowRegionCenterXZ[1];
        constants.cloudShadowRegionSize = input.cloudShadowRegionSize;
        constants.cloudShadowAnchorY = input.cloudShadowAnchorY;
        constants.cloudShadowEdgeFadeStart = input.cloudShadowEdgeFadeStart;

        // 実行待ちのフレームが読んでいる枠を書き換えないよう、フレームとビューごとに別の枠へ書く
        const uint32_t frameIndex = dxCommon_->Frame().FrameIndex();
        const size_t offset = static_cast<size_t>(kHitShadingConstantsStride)
            * (frameIndex * static_cast<uint32_t>(RTWaterViewID::Count) + viewIndex);
        std::memcpy(hitShadingConstantsMapped_ + offset, &constants, sizeof(constants));
        return hitShadingConstants_->GetGPUVirtualAddress() + offset;
    }

    void WaterRayTracingPassBase::SetSurfaceModelProvider(
        const std::shared_ptr<const IWaterSurfaceModelProvider>& provider)
    {
        surfaceModelProvider_ = provider;
    }

    std::shared_ptr<const IWaterSurfaceModelProvider> WaterRayTracingPassBase::GetSurfaceModelProvider() const
    {
        return surfaceModelProvider_.lock();
    }

    const WaterSurfaceData& WaterRayTracingPassBase::ResolveSurfaceDataForDispatch(
        const WaterSurfaceData& fallbackSurfaceData,
        WaterSurfaceData& outResolvedSurfaceData) const
    {
        const std::shared_ptr<const IWaterSurfaceModelProvider> surfaceModelProvider = surfaceModelProvider_.lock();
        if (!surfaceModelProvider) {
            return fallbackSurfaceData;
        }

        if (!surfaceModelProvider->TryGetSurfaceData(outResolvedSurfaceData)) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "{}: surface model provider returned no data. fallback path is used. provider='{}' type={}",
                GetOwnerName(),
                surfaceModelProvider->GetProviderName(),
                static_cast<uint32_t>(surfaceModelProvider->GetSimulationType()));
            return fallbackSurfaceData;
        }

        // 以前はここで毎フレーム Infof を出していたが、3 マネージャ分が常時流れて
        // ログを埋めるだけだったため撤去した（解決結果は GetDispatchInfo で参照できる）。
        return outResolvedSurfaceData;
    }
}
