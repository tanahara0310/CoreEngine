#include "pch.h"
#include "AccelerationStructureManager.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Command/DeferredReleaseQueue.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/VertexData.h"
#include "Utility/Logger/Logger.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>

namespace CoreEngine
{
    bool AccelerationStructureManager::Initialize(
        ID3D12Device* device, DescriptorAllocator* descriptorAllocator)
    {
        descriptorAllocator_ = descriptorAllocator;
        Logger& logger = Logger::GetInstance();

        // ID3D12Device5 の取得（DXR API に必須）
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device5_));
        if (FAILED(hr)) {
            isSupported_ = false;
            logger.Log("AccelerationStructureManager: ID3D12Device5 not available",
                LogLevel::Warn, LogCategory::Graphics);
            return false;
        }

        // D3D12_FEATURE_D3D12_OPTIONS5 でレイトレーシングティアを確認
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
        hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5));
        if (FAILED(hr) || opts5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
            isSupported_ = false;
            logger.Log("AccelerationStructureManager: Raytracing not supported",
                LogLevel::Warn, LogCategory::Graphics);
            return false;
        }
        // シェーダーはレイを RayQuery で撃つ
        if (opts5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
            isSupported_ = false;
            raytracingTier_ = static_cast<UINT>(opts5.RaytracingTier);
            logger.Log("AccelerationStructureManager: Raytracing tier 1.1 (RayQuery) not supported",
                LogLevel::Warn, LogCategory::Graphics);
            return false;
        }

        isSupported_ = true;
        raytracingTier_ = static_cast<UINT>(opts5.RaytracingTier);
        logger.Log("AccelerationStructureManager initialized (DXR supported)",
            LogLevel::Info, LogCategory::Graphics);
        return true;
    }

    // =========================================================================
    // デバッグ表示用の統計
    // =========================================================================
    UINT64 AccelerationStructureManager::GetBLASTotalBytes() const
    {
        UINT64 total = 0;
        for (const auto& [resource, entry] : staticBlas_) {
            if (entry.result) {
                total += entry.result->GetDesc().Width;
            }
        }
        return total;
    }

    UINT64 AccelerationStructureManager::GetTLASResultBytes() const
    {
        UINT64 total = 0;
        for (const auto& result : tlasResults_) {
            if (result) { total += result->GetDesc().Width; }
        }
        return total;
    }

    UINT64 AccelerationStructureManager::GetScratchTotalBytes() const
    {
        UINT64 total = 0;
        if (blasScratch_) { total += blasScratch_->GetDesc().Width; }
        if (tlasScratch_) { total += tlasScratch_->GetDesc().Width; }
        return total;
    }

    void AccelerationStructureManager::CollectGeometryRanges(
        const ModelResource& resource, uint32_t lod, std::vector<GeometryRange>& out)
    {
        out.clear();
        const std::vector<SubMeshData>& subMeshes = resource.GetSubMeshes();
        for (uint32_t i = 0; i < static_cast<uint32_t>(subMeshes.size()); ++i) {
            const SubMeshData& subMesh = subMeshes[i];
            const uint32_t lodCount = std::clamp(subMesh.lodCount, 1u, SubMeshData::kMaxLodCount);
            const uint32_t level = (std::min)(lod, lodCount - 1u);
            UINT startIndex = subMesh.startIndex;
            UINT indexCount = subMesh.indexCount;
            if (level > 0) {
                startIndex = subMesh.lods[level].startIndex;
                indexCount = subMesh.lods[level].indexCount;
            }
            indexCount -= indexCount % 3u;
            if (indexCount >= 3u) {
                out.push_back(GeometryRange{ startIndex, indexCount, i });
            }
        }
        if (subMeshes.empty() && resource.GetIndexCount() >= 3u) {
            out.push_back(GeometryRange{ 0, resource.GetIndexCount() - resource.GetIndexCount() % 3u, kWholeMesh });
        }
    }

    const AccelerationStructureManager::StaticBLAS* AccelerationStructureManager::AcquireStaticBLAS(
        ID3D12GraphicsCommandList* cmdList, const ModelResource& resource,
        std::span<const GeometryRange> geometries, uint64_t frame)
    {
        if (!isSupported_ || !resource.IsLoaded() || geometries.empty()
            || !resource.GetVertexBuffer() || !resource.GetIndexBuffer()) {
            return nullptr;
        }

        StaticBLASEntry& entry = staticBlas_[&resource];
        entry.lastUsedFrame = frame;
        const bool upToDate = entry.result
            && entry.vertexBuffer.Get() == resource.GetVertexBuffer()
            && entry.indexBuffer.Get() == resource.GetIndexBuffer()
            && entry.vertexCount == resource.GetVertexCount()
            && std::ranges::equal(entry.geometries, geometries);
        if (upToDate) {
            return &entry.view;
        }

        BLASDesc desc{};
        desc.vertexBuffer = resource.GetVertexBuffer();
        desc.vertexCount = resource.GetVertexCount();
        desc.vertexStride = sizeof(VertexData);
        desc.vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        desc.vertexPositionOffset = 0;  // VertexData::position は先頭メンバ（Vector4 だが xyz のみ使用）
        desc.indexBuffer = resource.GetIndexBuffer();
        desc.indexFormat = DXGI_FORMAT_R32_UINT;
        desc.geometries = geometries;
        if (!BuildStaticBLAS(cmdList, desc, entry)) {
            RetireStaticBLAS(entry);
            staticBlas_.erase(&resource);
            return nullptr;
        }
        return &entry.view;
    }

    bool AccelerationStructureManager::BuildStaticBLAS(
        ID3D12GraphicsCommandList* cmdList, const BLASDesc& desc, StaticBLASEntry& entry)
    {
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (!cmdList || FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) {
            Logger::GetInstance().Log("BuildStaticBLAS: CommandList4 not available",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        MakeGeometryDescs(desc, geometryDescScratch_);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = static_cast<UINT>(geometryDescScratch_.size());
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = geometryDescScratch_.data();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
        device5_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

        // スクラッチバッファの確保（既存のものが小さければ拡張）
        EnsureScratchBuffer(prebuild.ScratchDataSizeInBytes);
        Microsoft::WRL::ComPtr<ID3D12Resource> result = CreateASBuffer(prebuild.ResultDataMaxSizeInBytes);
        if (!result || !blasScratch_) {
            return false;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = result->GetGPUVirtualAddress();
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = blasScratch_->GetGPUVirtualAddress();
        cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        // UAV バリア（BLAS 構築完了を保証）
        Barrier::UAVRaw(cmdList, result.Get());

        // スクラッチバッファは全 BLAS で 1 枚を使い回している。リソースを指定した
        // UAV バリアは「そのリソースへのアクセス」しか順序付けないので、結果バッファ側の
        // バリアだけでは次の BuildRaytracingAccelerationStructure が同じスクラッチへ
        // 同時に書き込むのを止められない。モデルが複数同時にロードされるフレームでは
        // 実際に 10 個以上の BLAS が連続で積まれるため、ここでスクラッチも直列化する。
        Barrier::UAVRaw(cmdList, blasScratch_.Get());

        // 前の結果は GPU が使い終わってから解放し、新しい結果へ差し替える
        RetireStaticBLAS(entry);
        entry.result = std::move(result);
        entry.vertexBuffer = desc.vertexBuffer;
        entry.indexBuffer = desc.indexBuffer;
        entry.vertexCount = desc.vertexCount;
        entry.geometries.assign(desc.geometries.begin(), desc.geometries.end());
        entry.view = {};
        entry.view.address = entry.result->GetGPUVirtualAddress();

        // ヒットシェーディングが頂点と索引を読むための ByteAddressBuffer SRV
        if (descriptorAllocator_ && desc.indexFormat == DXGI_FORMAT_R32_UINT) {
            auto createRawBufferSrv = [this](ID3D12Resource* buffer, const char* debugName) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Buffer.FirstElement = 0;
                srvDesc.Buffer.NumElements = static_cast<UINT>(buffer->GetDesc().Width / sizeof(uint32_t));
                srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                return UniqueDescriptor(*descriptorAllocator_, descriptorAllocator_->CreateSRV(buffer, srvDesc, debugName));
                };
            entry.vertexBufferSrv = createRawBufferSrv(desc.vertexBuffer, "BLASVertexBufferSRV");
            entry.indexBufferSrv = createRawBufferSrv(desc.indexBuffer, "BLASIndexBufferSRV");
            if (entry.vertexBufferSrv.IsValid() && entry.indexBufferSrv.IsValid()) {
                entry.view.vertexBufferIndex = entry.vertexBufferSrv.Index();
                entry.view.indexBufferIndex = entry.indexBufferSrv.Index();
            }
        }

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "BLAS built ({} vertices, {} geometries)", desc.vertexCount, desc.geometries.size());
        return true;
    }

    void AccelerationStructureManager::RetireStaticBLAS(StaticBLASEntry& entry)
    {
        if (entry.result) {
            retiredResources_.push_back(std::move(entry.result));
        }
        if (entry.vertexBuffer) {
            retiredResources_.push_back(std::move(entry.vertexBuffer));
        }
        if (entry.indexBuffer) {
            retiredResources_.push_back(std::move(entry.indexBuffer));
        }
        entry.vertexBufferSrv.Reset();
        entry.indexBufferSrv.Reset();
        entry.view = {};
    }

    void AccelerationStructureManager::RetireUnusedStaticBLAS(uint64_t frame)
    {
        for (auto it = staticBlas_.begin(); it != staticBlas_.end();) {
            if (it->second.lastUsedFrame + kStaticKeepFrames < frame) {
                RetireStaticBLAS(it->second);
                ++releasedStaticBlasCount_;
                it = staticBlas_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void AccelerationStructureManager::BuildTLAS(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<InstanceDesc>& instances)
    {
        // 組み終えたときだけ撃てる状態に戻す
        tlasInstanceCount_ = 0;
        tlasReady_ = false;
        if (!isSupported_ || !cmdList) {
            return;
        }

        // ID3D12GraphicsCommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) return;

        // 物が 0 個のフレームも空の TLAS を組む（どのレイも外れる）
        const UINT64 instanceBufferSize =
            sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * (std::max)(instances.size(), size_t{ 1 });

        // ===== 今フレームぶんのインスタンスバッファをリングから選ぶ =====
        //   このバッファは UPLOAD ヒープにあり、GPU は「コマンドを実行する時点」で
        //   直接ここを読む。一方 CPU は「コマンドを記録する時点」で書き込むため、
        //   1 枚を使い回すと CPU が N フレーム目を書いている最中に、GPU がまだ
        //   N-1 / N-2 フレーム目の BuildRaytracingAccelerationStructure でそこを
        //   読んでいる（CPU 先行は kMaxFramesInFlight フレームまで許されている）。
        //   静止シーンは毎フレーム同じ行列を書くので上書きしても結果が変わらないが、
        //   動くオブジェクトがあると新旧の行列が混ざった TLAS が出来上がり、
        //   その影が 1 フレーム単位で飛ぶ（＝動かしたときだけ影がちらつく）。
        //   BuildTLAS はフレーム 1 回（ASBuildPass が frameNumber で抑止）なので、
        //   呼び出しごとに 1 つ進めればそのままフレーム対応のリングになる。
        tlasInstanceRingIndex_ = (tlasInstanceRingIndex_ + 1) % kMaxFramesInFlight;
        auto& instanceDescBuffer = tlasInstanceDescBuffers_[tlasInstanceRingIndex_];

        // インスタンスバッファの確保（必要に応じて拡張）
        if (!instanceDescBuffer ||
            instanceDescBuffer->GetDesc().Width < instanceBufferSize)
        {
            if (instanceDescBuffer) {
                retiredResources_.push_back(std::move(instanceDescBuffer));
            }

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

            D3D12_RESOURCE_DESC resDesc{};
            resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resDesc.Width = instanceBufferSize;
            resDesc.Height = 1;
            resDesc.DepthOrArraySize = 1;
            resDesc.MipLevels = 1;
            resDesc.SampleDesc.Count = 1;
            resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            HRESULT hr = device5_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr, IID_PPV_ARGS(&instanceDescBuffer));
            if (FAILED(hr)) {
                Logger::GetInstance().Logf(
                    LogLevel::Error,
                    LogCategory::Graphics,
                    "BuildTLAS: Failed to create TLAS instance buffer. hr=0x{:08X}",
                    static_cast<unsigned int>(hr));
                return;
            }
            assert(SUCCEEDED(hr));
        }

        // インスタンスデータの書き込み
        D3D12_RAYTRACING_INSTANCE_DESC* mapped = nullptr;
        if (FAILED(instanceDescBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || !mapped) {
            Logger::GetInstance().Logf(
                LogLevel::Error, LogCategory::Graphics, "BuildTLAS: Failed to map TLAS instance buffer.");
            return;
        }
        for (UINT i = 0; i < static_cast<UINT>(instances.size()); ++i) {
            mapped[i] = {};
            memcpy(mapped[i].Transform, instances[i].transform, sizeof(mapped[i].Transform));
            mapped[i].InstanceID = i;
            mapped[i].InstanceMask = instances[i].instanceMask;
            mapped[i].InstanceContributionToHitGroupIndex = 0;
            mapped[i].Flags = instances[i].flags;
            mapped[i].AccelerationStructure = instances[i].blasAddress;
        }
        instanceDescBuffer->Unmap(0, nullptr);

        // TLAS プレビルド情報
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = static_cast<UINT>(instances.size());
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
        device5_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

        // TLAS 結果バッファ（AS ステートで作成）
        auto ensureASBuffer = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& buf, UINT64 size) {
                if (!buf || buf->GetDesc().Width < size) {
                    if (buf) { retiredResources_.push_back(std::move(buf)); }

                    D3D12_HEAP_PROPERTIES hp{};
                    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                    D3D12_RESOURCE_DESC rd{};
                    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    rd.Width = size;
                    rd.Height = 1;
                    rd.DepthOrArraySize = 1;
                    rd.MipLevels = 1;
                    rd.SampleDesc.Count = 1;
                    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

                    device5_->CreateCommittedResource(
                        &hp, D3D12_HEAP_FLAG_NONE, &rd,
                        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                        nullptr, IID_PPV_ARGS(&buf));
                }
            };

        // スクラッチバッファ（UAV、COMMON で作成→暗黙昇格）
        auto ensureScratchBuffer = [&](Microsoft::WRL::ComPtr<ID3D12Resource>& buf, UINT64 size) {
                if (!buf || buf->GetDesc().Width < size) {
                    if (buf) { retiredResources_.push_back(std::move(buf)); }

                    D3D12_HEAP_PROPERTIES hp{};
                    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
                    D3D12_RESOURCE_DESC rd{};
                    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    rd.Width = size;
                    rd.Height = 1;
                    rd.DepthOrArraySize = 1;
                    rd.MipLevels = 1;
                    rd.SampleDesc.Count = 1;
                    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

                    device5_->CreateCommittedResource(
                        &hp, D3D12_HEAP_FLAG_NONE, &rd,
                        D3D12_RESOURCE_STATE_COMMON,
                        nullptr, IID_PPV_ARGS(&buf));
                }
            };

        // 結果バッファは今フレームのリングスロットのものを使う。
        // 張り直しても、実行待ちの前フレームが参照しているのは別スロットなので影響しない。
        auto& tlasResult = tlasResults_[tlasInstanceRingIndex_];
        const D3D12_GPU_VIRTUAL_ADDRESS previousAddress =
            tlasResult ? tlasResult->GetGPUVirtualAddress() : 0;

        constexpr UINT64 kMinASBufferBytes = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
        ensureASBuffer(tlasResult, (std::max)(prebuild.ResultDataMaxSizeInBytes, kMinASBufferBytes));
        ensureScratchBuffer(tlasScratch_, (std::max)(prebuild.ScratchDataSizeInBytes, kMinASBufferBytes));
        if (!tlasResult || !tlasScratch_) {
            Logger::GetInstance().Logf(
                LogLevel::Error, LogCategory::Graphics, "BuildTLAS: Failed to create TLAS result or scratch buffer.");
            return;
        }

        // TLAS のビルド
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = tlasResult->GetGPUVirtualAddress();
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = tlasScratch_->GetGPUVirtualAddress();

        cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        // UAV バリア
        Barrier::UAVRaw(cmdList, tlasResult.Get());

        // このスロットの SRV を用意する。番地が変わっていないフレームでは書き直さない
        // （ディスクリプタへの書き込みは、そのスロットを参照するコマンドが実行待ちの間は
        //   避けたい。同じ内容でも無駄に触らない）。
        auto& tlasSRVDescriptor = tlasSRVDescriptors_[tlasInstanceRingIndex_];
        const D3D12_GPU_VIRTUAL_ADDRESS currentAddress = tlasResult->GetGPUVirtualAddress();
        if (!tlasSRVDescriptor.IsValid() || currentAddress != previousAddress) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.RaytracingAccelerationStructure.Location = currentAddress;

            if (!tlasSRVDescriptor.IsValid()) {
                tlasSRVDescriptor = descriptorAllocator_->CreateSRV(
                    nullptr, srvDesc,
                    "TLAS_ring" + std::to_string(tlasInstanceRingIndex_));
                Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                    "TLAS built ({} instances, SRV allocated for ring slot {})",
                    instances.size(), tlasInstanceRingIndex_);
            } else {
                descriptorAllocator_->WriteSRV(tlasSRVDescriptor, nullptr, srvDesc);
            }
        }

        tlasInstanceCount_ = static_cast<UINT>(instances.size());
        tlasReady_ = tlasSRVDescriptor.IsValid();
    }

    void AccelerationStructureManager::MakeGeometryDescs(
        const BLASDesc& desc, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& out)
    {
        out.clear();
        out.reserve(desc.geometries.size());
        const D3D12_GPU_VIRTUAL_ADDRESS vertexAddress =
            desc.vertexBuffer->GetGPUVirtualAddress() + desc.vertexPositionOffset;
        const D3D12_GPU_VIRTUAL_ADDRESS indexAddress = desc.indexBuffer->GetGPUVirtualAddress();
        const UINT64 indexBytes = (desc.indexFormat == DXGI_FORMAT_R16_UINT) ? 2u : 4u;
        for (const GeometryRange& range : desc.geometries) {
            D3D12_RAYTRACING_GEOMETRY_DESC geomDesc{};
            geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            geomDesc.Triangles.VertexBuffer.StartAddress = vertexAddress;
            geomDesc.Triangles.VertexBuffer.StrideInBytes = desc.vertexStride;
            geomDesc.Triangles.VertexCount = desc.vertexCount;
            geomDesc.Triangles.VertexFormat = desc.vertexFormat;
            geomDesc.Triangles.IndexBuffer = indexAddress + static_cast<UINT64>(range.startIndex) * indexBytes;
            geomDesc.Triangles.IndexCount = range.indexCount;
            geomDesc.Triangles.IndexFormat = desc.indexFormat;
            out.push_back(geomDesc);
        }
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> AccelerationStructureManager::CreateASBuffer(UINT64 size) const
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = size;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        const HRESULT hr = device5_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            nullptr, IID_PPV_ARGS(&buffer));
        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "AccelerationStructureManager: failed to create AS buffer ({} bytes). hr=0x{:08X}",
                size, static_cast<unsigned int>(hr));
            return nullptr;
        }
        return buffer;
    }

    // =========================================================================
    // 動的 BLAS（揺れる植物・スキニングモデル）
    // =========================================================================
    bool AccelerationStructureManager::BeginDynamicBLASBatch(ID3D12GraphicsCommandList* cmdList)
    {
        dynamicCmdList_.Reset();
        dynamicBuildsPending_ = false;
        if (!isSupported_ || !cmdList || FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&dynamicCmdList_)))) {
            dynamicCmdList_.Reset();
            return false;
        }

        // 前の組で切り出した合計が収まるようにスクラッチを広げる（上限まで）
        const UINT64 wanted = (std::min)(dynamicScratchDemand_, kDynamicScratchMaxBytes);
        if (wanted > dynamicScratchSize_) {
            if (dynamicScratch_) {
                retiredResources_.push_back(std::move(dynamicScratch_));
            }
            dynamicScratch_ = CreateScratch(wanted);
            dynamicScratchSize_ = dynamicScratch_ ? wanted : 0;
        }
        dynamicScratchOffset_ = 0;
        dynamicScratchDemand_ = 0;
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS AccelerationStructureManager::AllocateDynamicScratch(UINT64 len)
    {
        constexpr UINT64 kAlign = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
        len = (std::max)(kAlign, (len + kAlign - 1) / kAlign * kAlign);
        dynamicScratchDemand_ += len;
        if (len > dynamicScratchSize_) {
            // 1 本でスクラッチを超えるときは張り直す（前の構築は退避した古いバッファを使い続ける）
            if (dynamicScratch_) {
                retiredResources_.push_back(std::move(dynamicScratch_));
            }
            dynamicScratch_ = CreateScratch(len);
            dynamicScratchSize_ = dynamicScratch_ ? len : 0;
            dynamicScratchOffset_ = 0;
            if (!dynamicScratch_) {
                return 0;
            }
        } else if (dynamicScratchOffset_ + len > dynamicScratchSize_) {
            // 使い切ったら、ここまでの構築がスクラッチを使い終わるのを待って先頭から使い直す
            Barrier::UAVRaw(dynamicCmdList_.Get(), dynamicScratch_.Get());
            dynamicScratchOffset_ = 0;
        }
        const D3D12_GPU_VIRTUAL_ADDRESS address = dynamicScratch_->GetGPUVirtualAddress() + dynamicScratchOffset_;
        dynamicScratchOffset_ += len;
        return address;
    }

    D3D12_GPU_VIRTUAL_ADDRESS AccelerationStructureManager::BuildOrUpdateDynamicBLAS(
        const void* owner, const BLASDesc& desc, uint64_t frame, bool geometryChanged)
    {
        if (!dynamicCmdList_ || !owner || !desc.vertexBuffer || desc.vertexCount == 0
            || !desc.indexBuffer || desc.geometries.empty()) {
            return 0;
        }

        const D3D12_GPU_VIRTUAL_ADDRESS vertexAddress =
            desc.vertexBuffer->GetGPUVirtualAddress() + desc.vertexPositionOffset;
        const D3D12_GPU_VIRTUAL_ADDRESS indexAddress = desc.indexBuffer->GetGPUVirtualAddress();

        const bool isNew = (dynamicBlas_.find(owner) == dynamicBlas_.end());
        DynamicBLASEntry& entry = dynamicBlas_[owner];
        if (isNew) {
            // 構築し直すフレームが持ち主どうしで重ならないよう、アドレスから始点をずらす
            entry.updatesSinceBuild = static_cast<uint32_t>(
                (reinterpret_cast<uintptr_t>(owner) >> 4) % kDynamicRebuildInterval);
        }
        entry.lastUsedFrame = frame;

        const bool shapeChanged = !entry.result
            || entry.vertexAddress != vertexAddress || entry.vertexCount != desc.vertexCount
            || entry.vertexStride != desc.vertexStride
            || entry.indexAddress != indexAddress || !std::ranges::equal(entry.geometries, desc.geometries);

        // 頂点が動いていなければ前の BLAS をそのまま使う（停止中のスキニングなど）
        if (!shapeChanged && !geometryChanged) {
            return entry.result->GetGPUVirtualAddress();
        }

        MakeGeometryDescs(desc, geometryDescScratch_);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        // 更新（refit）を許可して構築する。更新時も同じフラグ（+ PERFORM_UPDATE）でなければならない
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
            | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = static_cast<UINT>(geometryDescScratch_.size());
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = geometryDescScratch_.data();

        // 必要な大きさは形が変わったときだけ問い合わせる
        if (shapeChanged) {
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
            device5_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
            entry.buildScratchSize = prebuild.ScratchDataSizeInBytes;
            entry.updateScratchSize = prebuild.UpdateScratchDataSizeInBytes;

            // 結果が収まらなければ張り直す（前の結果は実行待ちのフレームが使っているので退避）
            if (!entry.result || entry.resultSize < prebuild.ResultDataMaxSizeInBytes) {
                if (entry.result) {
                    retiredResources_.push_back(std::move(entry.result));
                }
                entry.result = CreateASBuffer(prebuild.ResultDataMaxSizeInBytes);
                entry.resultSize = entry.result ? prebuild.ResultDataMaxSizeInBytes : 0;
                if (!entry.result) {
                    dynamicBlas_.erase(owner);
                    return 0;
                }
            }
        }

        const bool rebuild = shapeChanged || entry.updatesSinceBuild >= kDynamicRebuildInterval;
        const D3D12_GPU_VIRTUAL_ADDRESS scratch =
            AllocateDynamicScratch(rebuild ? entry.buildScratchSize : entry.updateScratchSize);
        if (!scratch) {
            return 0;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = entry.result->GetGPUVirtualAddress();
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = scratch;
        if (rebuild) {
            entry.updatesSinceBuild = 0;
        } else {
            // 前回の結果をその場で更新する（同じキュー上なので、前フレームのトレースは先に終わっている）
            buildDesc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
            buildDesc.SourceAccelerationStructureData = entry.result->GetGPUVirtualAddress();
            ++entry.updatesSinceBuild;
        }

        // 組の中では待たずに続けて積む（結果とスクラッチの範囲は持ち主ごとに別）
        dynamicCmdList_->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
        dynamicBuildsPending_ = true;

        entry.vertexAddress = vertexAddress;
        entry.vertexCount = desc.vertexCount;
        entry.vertexStride = desc.vertexStride;
        entry.indexAddress = indexAddress;
        entry.geometries.assign(desc.geometries.begin(), desc.geometries.end());
        return entry.result->GetGPUVirtualAddress();
    }

    void AccelerationStructureManager::EndDynamicBLASBatch()
    {
        // 積んだ全部の構築・更新が、TLAS の構築とトレースより前に書き終わるように
        if (dynamicCmdList_ && dynamicBuildsPending_) {
            Barrier::UAVAll(dynamicCmdList_.Get());
        }
        dynamicBuildsPending_ = false;
        dynamicCmdList_.Reset();
    }

    void AccelerationStructureManager::RetireUnusedDynamicBLAS(uint64_t frame)
    {
        for (auto it = dynamicBlas_.begin(); it != dynamicBlas_.end();) {
            if (it->second.lastUsedFrame + kDynamicKeepFrames < frame) {
                // 前のフレームの TLAS がまだ指しているかもしれないので、フェンスを待ってから解放する
                if (it->second.result) {
                    retiredResources_.push_back(std::move(it->second.result));
                }
                it = dynamicBlas_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void AccelerationStructureManager::EnsureScratchBuffer(UINT64 requiredSize)
    {
        if (blasScratchSize_ >= requiredSize) return;
        blasScratchSize_ = requiredSize;

        // 旧バッファはコマンドリスト実行完了まで保持する必要がある
        if (blasScratch_) {
            retiredResources_.push_back(std::move(blasScratch_));
        }
        blasScratch_ = CreateScratch(requiredSize);
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> AccelerationStructureManager::CreateScratch(UINT64 size) const
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = size;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        device5_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&buffer));
        return buffer;
    }

    void AccelerationStructureManager::MoveRetiredResourcesTo(
        DeferredReleaseQueue& queue, std::uint64_t fenceValue)
    {
        if (retiredResources_.empty()) {
            return;
        }

        // 「GPU 完了を待ってから解放」ではなく「フェンス値つきで解放予約」に変える。
        // 待たないのでパイプラインが空にならない。
        for (auto& resource : retiredResources_) {
            queue.Push(std::move(resource), fenceValue);
        }
        retiredResources_.clear();
    }
}
