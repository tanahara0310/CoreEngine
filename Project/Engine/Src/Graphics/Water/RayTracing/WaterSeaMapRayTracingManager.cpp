#include "pch.h"
#include "WaterSeaMapRayTracingManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "Graphics/RayTracing/AccelerationStructureManager.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "Graphics/Shader/CBufferReflectionCheck.h"
#include "Graphics/Water/WaterCVars.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace {
        struct WaterSeaMapConstants {
            float mapOriginXZ[2];
            float texelSize;
            float maxValue;
            float upProbeDistance;
            uint32_t instanceMask;
            float pad[2];
        };

        static constexpr Cb::Field kWaterSeaMapConstantsFields[] = {
            CB_FIELD(WaterSeaMapConstants, mapOriginXZ), CB_FIELD(WaterSeaMapConstants, texelSize),
            CB_FIELD(WaterSeaMapConstants, maxValue), CB_FIELD(WaterSeaMapConstants, upProbeDistance),
            CB_FIELD(WaterSeaMapConstants, instanceMask), CB_FIELD(WaterSeaMapConstants, pad),
        };
        CB_VERIFY_LAYOUT(WaterSeaMapConstants, kWaterSeaMapConstantsFields);
        CB_BIND_HLSL(WaterSeaMapConstants, kWaterSeaMapConstantsFields, "WaterSeaMapConstants");

        /// @brief 測った値 1 テクセルぶん（水深, 上の物までの高さ）のバイト数
        constexpr uint32_t kMeasuredTexelBytes = sizeof(float) * 2;
        /// @brief 地図 1 テクセルぶん（半精度 4 つ）のバイト数
        constexpr uint32_t kMapTexelBytes = sizeof(uint16_t) * 4;
    }

    WaterSeaMapRayTracingManager::~WaterSeaMapRayTracingManager()
    {
        // 作っている途中の地図は待ってから捨てる（future の破棄が待つ）
        buildTask_ = {};
    }

    bool WaterSeaMapRayTracingManager::Initialize(
        GraphicsCore* dxCommon,
        DescriptorAllocator* descriptorAllocator,
        AccelerationStructureManager* asMgr,
        ShaderProgramCache* shaderProgramCache)
    {
        RTWaterPipelineDesc desc{};
        desc.ownerName = "WaterSeaMapRayTracingManager";
        desc.outputDebugName = "RTWaterSeaMap";
        desc.shaderPath = L"Engine/Assets/Shaders/Water/RayTracing/RTWaterSeaMap.hlsl";
        desc.rayGenName = L"RTWaterSeaMapRayGen";
        desc.missName = L"RTWaterSeaMapMiss";
        desc.hitGroupName = L"RTWaterSeaMapHitGroup";
        desc.closestHitName = L"RTWaterSeaMapClosestHit";
        desc.outputUavName = "gSeaMapOutput";
        desc.constantsName = "WaterSeaMapConstants";
        desc.constantsBytes = sizeof(WaterSeaMapConstants);
        // 水面の共通関数（RTWaterSurfaceCommon.hlsli）が海の範囲マップをヒープから引く
        desc.directlyIndexedHeap = true;
        return InitializeFromDesc(dxCommon, descriptorAllocator, asMgr, shaderProgramCache, desc);
    }

    WaterSeaMapRayTracingManager::BakeKey WaterSeaMapRayTracingManager::MakeKey(
        const WaterSurfaceData& surfaceData, uint64_t staticSceneSignature) const
    {
        BakeKey key{};
        const float sizeX = 2.0f * surfaceData.regionHalfExtentXZ[0];
        const float sizeZ = 2.0f * surfaceData.regionHalfExtentXZ[1];
        const float longSide = (std::max)((std::max)(sizeX, sizeZ), 1.0e-3f);
        const uint32_t maxResolution = static_cast<uint32_t>(std::clamp(WaterCVars::SeaMapResolution.Get(), 256, 4096));
        key.texelSize = longSide / static_cast<float>(maxResolution);
        key.width = std::clamp(static_cast<uint32_t>(std::ceil(sizeX / key.texelSize)), 1u, maxResolution);
        key.height = std::clamp(static_cast<uint32_t>(std::ceil(sizeZ / key.texelSize)), 1u, maxResolution);
        key.originXZ[0] = surfaceData.regionCenterXZ[0] - 0.5f * static_cast<float>(key.width) * key.texelSize;
        key.originXZ[1] = surfaceData.regionCenterXZ[1] - 0.5f * static_cast<float>(key.height) * key.texelSize;
        key.waterHeight = surfaceData.waterHeight;

        // 粗い格子の三角形 1 つが覆う範囲（頂点の間隔＋水平変位）と、バイリニアで混ざる隣のテクセルの分
        const float coarseSpacing = longSide / (std::max)(surfaceData.meshSubdivisions, 1.0f);
        key.conservativeRadius = static_cast<uint32_t>(std::ceil((coarseSpacing + kHorizontalMargin) / key.texelSize)) + 1u;
        key.sceneSignature = staticSceneSignature;
        return key;
    }

    void WaterSeaMapRayTracingManager::Update(
        ID3D12GraphicsCommandList* cmdList,
        const WaterSurfaceData* surfaceData,
        uint64_t staticSceneSignature)
    {
        if (!IsInitialized() || !cmdList) {
            return;
        }

        const bool wanted = surfaceData && surfaceData->regionValid != 0 && WaterCVars::SeaMapEnabled.Get();
        const uint64_t frame = dxCommon_->Frame().FrameNumber();

        // 別スレッドの地図づくりが終わっていれば受け取る
        if (stage_ == Stage::Building && buildTask_.valid()
            && buildTask_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            BuildResult result = buildTask_.get();
            stage_ = Stage::Idle;
            if (wanted && result.generation == generation_ && !result.texels.empty()) {
                Upload(cmdList, std::move(result));
            }
        }

        if (!wanted) {
            Discard();
            return;
        }

        // 測った値の読み戻しが終わっていれば、別スレッドで地図を作り始める
        if (stage_ == Stage::WaitingReadback
            && frame >= readbackFrame_ + dxCommon_->Frame().FramesInFlight() + 1) {
            StartBuild();
        }

        // 測る条件が kStableFrames 変わらず、今の地図と違えば測り直す
        const BakeKey key = MakeKey(*surfaceData, staticSceneSignature);
        if (!hasCandidate_ || !(key == candidateKey_)) {
            candidateKey_ = key;
            hasCandidate_ = true;
            candidateSinceFrame_ = frame;
        }
        const bool mapIsCurrent = view_.valid && mapKey_ == candidateKey_;
        if (stage_ == Stage::Idle && !mapIsCurrent && frame - candidateSinceFrame_ >= kStableFrames) {
            Dispatch(cmdList, *surfaceData, candidateKey_);
        }
    }

    void WaterSeaMapRayTracingManager::Dispatch(
        ID3D12GraphicsCommandList* cmdList, const WaterSurfaceData& surfaceData, const BakeKey& key)
    {
        WaterSurfaceData dispatchSurfaceData = surfaceData;
        dispatchSurfaceData.waterHeight = key.waterHeight;
        BeginDiagnostics(kOutputSlot, key.width, key.height, dispatchSurfaceData, {}, {});

        DispatchResources resources;
        if (!BeginDispatch(cmdList, key.width, key.height, kOutputSlot, resources, DXGI_FORMAT_R32G32_FLOAT)) {
            return;
        }

        WaterSeaMapConstants constants{};
        constants.mapOriginXZ[0] = key.originXZ[0];
        constants.mapOriginXZ[1] = key.originXZ[1];
        constants.texelSize = key.texelSize;
        constants.maxValue = WaterSeaMapBuilder::kMaxValue;
        constants.upProbeDistance = kUpProbeDistance;
        constants.instanceMask = RayTracingInstanceMask::kSolid;

        const SurfaceConstantsUpload surface = UploadSurfaceDataForDispatch(dispatchSurfaceData, FFTOceanInput{});
        BindAndDispatchRays(
            cmdList,
            resources,
            surface.address,
            {},
            &constants,
            key.width,
            key.height,
            D3D12_RESOURCE_STATE_COPY_SOURCE);

        // 測った値を読み戻し用のバッファへ写す
        ID3D12Device* device = dxCommon_->GetDevice();
        const D3D12_RESOURCE_DESC outputDesc = resources.output->Get()->GetDesc();
        UINT64 totalBytes = 0;
        device->GetCopyableFootprints(&outputDesc, 0, 1, 0, &readbackLayout_, nullptr, nullptr, &totalBytes);
        readback_ = ResourceFactory::CreateBufferResource(device, static_cast<size_t>(totalBytes), D3D12_HEAP_TYPE_READBACK);
        if (!readback_) {
            return;
        }

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = readbackLayout_;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = resources.output->Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;
        cmdList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        pendingKey_ = key;
        readbackFrame_ = dxCommon_->Frame().FrameNumber();
        stage_ = Stage::WaitingReadback;
    }

    void WaterSeaMapRayTracingManager::StartBuild()
    {
        stage_ = Stage::Idle;
        const uint32_t width = pendingKey_.width;
        const uint32_t height = pendingKey_.height;

        std::vector<float> measured(static_cast<size_t>(width) * height * 2);
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0, static_cast<SIZE_T>(readbackLayout_.Offset
            + static_cast<UINT64>(readbackLayout_.Footprint.RowPitch) * height) };
        if (!readback_ || FAILED(readback_->Map(0, &readRange, &mapped)) || !mapped) {
            readback_.Reset();
            return;
        }
        const auto* rows = static_cast<const uint8_t*>(mapped) + readbackLayout_.Offset;
        for (uint32_t z = 0; z < height; ++z) {
            std::memcpy(
                measured.data() + static_cast<size_t>(z) * width * 2,
                rows + static_cast<size_t>(z) * readbackLayout_.Footprint.RowPitch,
                static_cast<size_t>(width) * kMeasuredTexelBytes);
        }
        const D3D12_RANGE writtenRange{ 0, 0 };
        readback_->Unmap(0, &writtenRange);

        // 読み戻しが終わった（GPU が使い終わった）ので、測った値の出力と読み戻し用のバッファを手放す
        readback_.Reset();
        ReleaseOutputIfSizeMismatchBase(0, 0, kOutputSlot);

        const BakeKey key = pendingKey_;
        const uint64_t generation = generation_;
        buildTask_ = std::async(std::launch::async, [measured = std::move(measured), key, generation]() {
            const auto start = std::chrono::steady_clock::now();
            BuildResult result{};
            WaterSeaMapBuilder::Input input{};
            input.measured = measured.data();
            input.width = key.width;
            input.height = key.height;
            input.conservativeRadius = key.conservativeRadius;
            result.texels = WaterSeaMapBuilder::Build(input, &result.stats);
            result.key = key;
            result.generation = generation;
            result.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            return result;
        });
        stage_ = Stage::Building;
    }

    void WaterSeaMapRayTracingManager::Upload(ID3D12GraphicsCommandList* cmdList, BuildResult&& result)
    {
        ID3D12Device* device = dxCommon_->GetDevice();
        const BakeKey& key = result.key;

        D3D12_RESOURCE_DESC textureDesc{};
        textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        textureDesc.Width = key.width;
        textureDesc.Height = key.height;
        textureDesc.DepthOrArraySize = 1;
        textureDesc.MipLevels = 1;
        textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        textureDesc.SampleDesc.Count = 1;
        Microsoft::WRL::ComPtr<ID3D12Resource> texture =
            ResourceFactory::CreateTextureResource(device, textureDesc, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!texture) {
            return;
        }
        texture->SetName(L"WaterSeaMap");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        UINT64 totalBytes = 0;
        device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &layout, nullptr, nullptr, &totalBytes);
        Microsoft::WRL::ComPtr<ID3D12Resource> upload =
            ResourceFactory::CreateBufferResource(device, static_cast<size_t>(totalBytes), D3D12_HEAP_TYPE_UPLOAD);
        void* mapped = nullptr;
        if (!upload || FAILED(upload->Map(0, nullptr, &mapped)) || !mapped) {
            return;
        }
        auto* rows = static_cast<uint8_t*>(mapped) + layout.Offset;
        for (uint32_t z = 0; z < key.height; ++z) {
            std::memcpy(
                rows + static_cast<size_t>(z) * layout.Footprint.RowPitch,
                result.texels.data() + static_cast<size_t>(z) * key.width * 4,
                static_cast<size_t>(key.width) * kMapTexelBytes);
        }
        upload->Unmap(0, nullptr);

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = layout;
        cmdList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        GpuResource uploaded;
        uploaded.Reset(texture, D3D12_RESOURCE_STATE_COPY_DEST);
        Barrier::Transition(cmdList, uploaded,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = textureDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        DescriptorHandle srv = descriptorAllocator_->CreateSRV(texture.Get(), srvDesc, "WaterSeaMap");
        if (!srv.IsValid()) {
            return;
        }

        // 今フレームまでの描画は前の地図を読むので、前の地図はこのフレームが終わってから手放す
        DeferredReleaseQueue& deferredRelease = dxCommon_->DeferredRelease();
        deferredRelease.PushForCurrentFrame(std::move(upload));
        if (mapTexture_) {
            deferredRelease.PushForCurrentFrame(std::move(mapTexture_));
        }
        deferredRelease.PushForCurrentFrame(*descriptorAllocator_, mapSrv_);

        mapTexture_ = std::move(texture);
        mapSrv_ = srv;
        mapKey_ = key;
        view_.srvHandle = mapSrv_.gpuHandle;
        view_.srvHeapIndex = mapSrv_.index;
        view_.resolution[0] = key.width;
        view_.resolution[1] = key.height;
        view_.originXZ[0] = key.originXZ[0];
        view_.originXZ[1] = key.originXZ[1];
        view_.texelsPerMeter = 1.0f / key.texelSize;
        view_.waterHeight = key.waterHeight;
        view_.valid = true;

        Logger::GetInstance().Infof(
            LogCategory::Graphics,
            LogSubCategory::Pipeline,
            "WaterSeaMap: built {}x{} texel={:.2f}m radius={} sea={} land={} basin={} bodies={} cpu={:.1f}ms",
            key.width, key.height, key.texelSize, key.conservativeRadius,
            result.stats.seaTexels, result.stats.landTexels, result.stats.basinTexels,
            result.stats.waterBodies, result.milliseconds);
    }

    void WaterSeaMapRayTracingManager::Discard()
    {
        hasCandidate_ = false;
        if (!view_.valid && stage_ == Stage::Idle && !readback_) {
            return;
        }

        DeferredReleaseQueue& deferredRelease = dxCommon_->DeferredRelease();
        if (mapTexture_) {
            deferredRelease.PushForCurrentFrame(std::move(mapTexture_));
        }
        deferredRelease.PushForCurrentFrame(*descriptorAllocator_, mapSrv_);
        if (readback_) {
            deferredRelease.PushForCurrentFrame(std::move(readback_));
        }
        if (stage_ == Stage::WaitingReadback) {
            stage_ = Stage::Idle;
        }
        view_ = {};
        mapKey_ = {};
        ++generation_;
    }
}
