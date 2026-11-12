#include "pch.h"
#include "WaterRayTracingPassBase.h"

#include <algorithm>
#include <cassert>

#include <dxcapi.h>

#include "Camera/View/ViewInfo.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Render/FrameBlackboard.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RayTracing/RayTracingPipelineBuilder.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    // 構成データ 1 つから DXR パイプライン・シェーダーテーブル・出力ビューを丸ごと組む。
    // 屈折・反射・コースティクスの 3 マネージャはこの関数への引数だけが違う
    bool WaterRayTracingPassBase::InitializeFromDesc(const RTEffectInitContext& init, const RTWaterPipelineDesc& desc)
    {
        Logger& log = Logger::GetInstance();

        if (!InitializeBase(init.dxCommon, init.descriptorAllocator, init.asMgr, init.shaderProgramCache,
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

        // State Object とシェーダーテーブルは別スレッドで作り、最初のディスパッチの前に待つ
        // （レイは RayGen の中から RayQuery で撃つ。ミスとヒットグループは持たない）
        RayTracingPipelineBuilder pipelineBuilder;
        pipelineBuilder
            .SetDXILLibrary(shaderBlob)
            .SetGlobalRootSignature(globalRootSigMgr_.GetRootSignature())
            .SetMaxRecursionDepth(1);
        shaderTableBuilder_
            .SetRayGenShader(desc.rayGenName);
        BuildPipelineAsync(std::move(pipelineBuilder));
        // 記録版はヒープの番号で記録先を引く
        if (desc.rayRecordLabel && desc.directlyIndexedHeap) {
            EnableRayRecord(desc.shaderPath, desc.rayGenName, desc.rayRecordLabel);
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
        D3D12_GPU_VIRTUAL_ADDRESS surfaceConstants,
        std::initializer_list<RTWaterSrvBinding> srvBindings,
        const void* constantsBlob,
        UINT width,
        UINT height,
        D3D12_RESOURCE_STATES finalState,
        const RTWaterSecondaryOutput& secondaryOutput,
        D3D12_GPU_VIRTUAL_ADDRESS extraConstantBuffer)
    {
        resources.cmdList4->SetComputeRootSignature(globalRootSigMgr_.GetRootSignature());

        assert(surfaceConstants != 0 && "水面サーフェス定数が置かれていない");
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
        binder.Set(bindings_[slotSurfaceData_], surfaceConstants);
        binder.SetConstants(
            bindings_[slotConstants_], constantsBlob, constantsBytes_ / sizeof(uint32_t));
        if (hasExtraConstantBuffer_ && extraConstantBuffer != 0) {
            binder.Set(bindings_[slotExtraConstantBuffer_], extraConstantBuffer);
        }
        binder.ValidateBeforeDraw(bindings_);

        const auto dispatchDesc = SetDispatchPipeline(resources.cmdList4.Get(), width, height);
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
        return BeginDispatchBase(cmdList, width, height, viewIndex, outResources, format);
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
        const FFTOceanInput& fftOceanInput)
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
            surfaceConstants.waveGroupPhase[c] = fftOceanInput.waveGroupPhase[c];
        }
        surfaceConstants.fineWaveMeanSquareSlope = fftOceanInput.fineWaveMeanSquareSlope;
        surfaceConstants.maxCrestHeight = surfaceData.maxCrestHeight;
        surfaceConstants.regionValid = surfaceData.regionValid;
        for (int c = 0; c < 2; ++c) {
            surfaceConstants.regionCenterXZ[c] = surfaceData.regionCenterXZ[c];
            surfaceConstants.regionHalfExtentXZ[c] = surfaceData.regionHalfExtentXZ[c];
            surfaceConstants.shoreMeshOriginXZ[c] = surfaceData.shoreMeshOriginXZ[c];
        }
        surfaceConstants.shoreMeshEnabled = surfaceData.shoreMeshEnabled;
        surfaceConstants.shoreMeshSize = surfaceData.shoreMeshSize;
        surfaceConstants.shoreMeshCellSize = surfaceData.shoreMeshCellSize;
        surfaceConstants.shoreMeshBlendWidth = surfaceData.shoreMeshBlendWidth;
        surfaceConstants.shoreMeshDisplacementSrvIndex = surfaceData.shoreMeshDisplacementSrvIndex;
        const WaterSeaMapBinding& seaMap = surfaceData.seaMap;
        for (int c = 0; c < 2; ++c) {
            surfaceConstants.seaMapOriginXZ[c] = seaMap.originXZ[c];
            surfaceConstants.seaMapResolution[c] = seaMap.resolution[c];
        }
        surfaceConstants.seaMapTexelsPerMeter = seaMap.texelsPerMeter;
        surfaceConstants.seaMapSrvIndex = seaMap.srvHeapIndex;
        surfaceConstants.seaMapDepthOffset = seaMap.depthOffset;
        surfaceConstants.seaMapInvWaveHeight = seaMap.invWaveHeight;
        return surfaceConstants;
    }

    // 供給元から水面状態を取り出して記録中のフレームの UploadRing に置き、内容と場所を返す
    WaterRayTracingPassBase::SurfaceConstantsUpload WaterRayTracingPassBase::UploadSurfaceDataForDispatch(
        const WaterSurfaceData& surfaceData,
        const FFTOceanInput& fftOceanInput) const
    {
        SurfaceConstantsUpload upload{};
        upload.constants = BuildSurfaceConstants(surfaceData, fftOceanInput);
        upload.address = dxCommon_->GetUploadRing().AllocateConstants(upload.constants);
        return upload;
    }

    bool WaterRayTracingPassBase::BuildFrameInput(
        const RenderContext& context,
        const WaterSurfaceData& surfaceData,
        const char* debugLabel,
        bool requireSceneColor,
        FrameInput& out)
    {
        if (!context.gBufferManager || !context.sceneManager || !context.dxCommon || !context.cmdList) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRayTracingPassBase: {} dispatch skipped. gBuffer={} sceneManager={} dx={} cmdList={}",
                debugLabel,
                context.gBufferManager != nullptr,
                context.sceneManager != nullptr,
                context.dxCommon != nullptr,
                context.cmdList != nullptr);
            return false;
        }

        if (!context.frameViews || !context.frameViews->Get(context.viewSettings.viewType).isValid) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterRayTracingPassBase: {} dispatch skipped. view is invalid.",
                debugLabel);
            return false;
        }
        const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);

        if (context.frameBlackboard) {
            context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, out.sceneDepthSRV);

            // カラーソースは水面合成前の SceneColor スナップショット
            // （空・雲・ゴッドレイ・島すべてを含み、水面のみ未合成）。
            if (!context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneColorSnapshot, out.sceneColorSRV)) {
                context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneColor, out.sceneColorSRV);
            }
        }

        if (requireSceneColor && out.sceneColorSRV.ptr == 0) {
            Logger::GetInstance().Warnf(
                LogCategory::Graphics,
                LogSubCategory::RenderTarget,
                "WaterRayTracingPassBase: {} dispatch skipped. SceneColorSnapshot/SceneColor SRV is invalid.",
                debugLabel);
            return false;
        }

        out.viewProjection = view.viewProjection;
        out.cameraPosition = view.position;
        out.width = static_cast<UINT>(context.dxCommon->GetClientWidth());
        out.height = static_cast<UINT>(context.dxCommon->GetClientHeight());

        // FFT の海で描いているときだけ波面テクスチャを接続する（それ以外は enabled = 0）
        out.fftOceanInput = surfaceData.fftOcean;
        return true;
    }
}
