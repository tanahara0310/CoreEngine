#include "pch.h"
#include "AccelerationStructureManager.h"
#include "Graphics/RHI/Descriptor/DescriptorManager.h"
#include "Graphics/RHI/Command/CommandManager.h"
#include "Graphics/RHI/Barrier/ResourceBarrierHelper.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/VertexData.h"
#include "Utility/Logger/Logger.h"
#include <cassert>

namespace CoreEngine
{
    bool AccelerationStructureManager::Initialize(
        ID3D12Device* device, DescriptorManager* descriptorManager)
    {
        descriptorManager_ = descriptorManager;
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
        for (const BLASEntry& entry : blasList_) {
            if (entry.result) {
                total += entry.result->GetDesc().Width;
            }
        }
        return total;
    }

    UINT64 AccelerationStructureManager::GetTLASResultBytes() const
    {
        return tlasResult_ ? tlasResult_->GetDesc().Width : 0;
    }

    UINT64 AccelerationStructureManager::GetScratchTotalBytes() const
    {
        UINT64 total = 0;
        if (blasScratch_) { total += blasScratch_->GetDesc().Width; }
        if (tlasScratch_) { total += tlasScratch_->GetDesc().Width; }
        return total;
    }

    UINT AccelerationStructureManager::BuildBLAS(
        ID3D12GraphicsCommandList* cmdList, const BLASDesc& desc)
    {
        assert(isSupported_ && "DXR not supported");
        assert(desc.vertexBuffer && "Vertex buffer is required");

        // ID3D12GraphicsCommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        HRESULT hr = cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4));
        if (FAILED(hr)) {
            Logger::GetInstance().Log("BuildBLAS: CommandList4 not available",
                LogLevel::Error, LogCategory::Graphics);
            return UINT_MAX;
        }

        // ジオメトリ記述子の設定
        D3D12_RAYTRACING_GEOMETRY_DESC geomDesc{};
        geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        geomDesc.Triangles.VertexBuffer.StartAddress =
            desc.vertexBuffer->GetGPUVirtualAddress() + desc.vertexPositionOffset;
        geomDesc.Triangles.VertexBuffer.StrideInBytes = desc.vertexStride;
        geomDesc.Triangles.VertexCount = desc.vertexCount;
        geomDesc.Triangles.VertexFormat = desc.vertexFormat;

        if (desc.indexBuffer && desc.indexCount > 0) {
            geomDesc.Triangles.IndexBuffer = desc.indexBuffer->GetGPUVirtualAddress();
            geomDesc.Triangles.IndexCount = desc.indexCount;
            geomDesc.Triangles.IndexFormat = desc.indexFormat;
        } else {
            geomDesc.Triangles.IndexBuffer = 0;
            geomDesc.Triangles.IndexCount = 0;
            geomDesc.Triangles.IndexFormat = DXGI_FORMAT_UNKNOWN;
        }

        // プレビルド情報の取得
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = 1;
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.pGeometryDescs = &geomDesc;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
        device5_->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

        // スクラッチバッファの確保（既存のものが小さければ拡張）
        EnsureScratchBuffer(prebuild.ScratchDataSizeInBytes);

        // BLAS 結果バッファの作成
        BLASEntry entry;
        {
            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC resDesc{};
            resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resDesc.Width = prebuild.ResultDataMaxSizeInBytes;
            resDesc.Height = 1;
            resDesc.DepthOrArraySize = 1;
            resDesc.MipLevels = 1;
            resDesc.SampleDesc.Count = 1;
            resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            hr = device5_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr, IID_PPV_ARGS(&entry.result));
            assert(SUCCEEDED(hr));
        }

        // BLAS のビルド
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = entry.result->GetGPUVirtualAddress();
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = blasScratch_->GetGPUVirtualAddress();

        cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        // UAV バリア（BLAS 構築完了を保証）
        ResourceBarrierHelper::UAV(cmdList, entry.result.Get());

        UINT blasIndex = static_cast<UINT>(blasList_.size());
        blasList_.push_back(std::move(entry));

        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
            "BLAS[{}] built ({} vertices, {} indices)",
            blasIndex, desc.vertexCount, desc.indexCount);

        return blasIndex;
    }

    void AccelerationStructureManager::BuildTLAS(
        ID3D12GraphicsCommandList* cmdList,
        const std::vector<InstanceDesc>& instances)
    {
        if (!isSupported_ || instances.empty()) {
            tlasInstanceCount_ = 0;
            return;
        }

        // ID3D12GraphicsCommandList4 を取得
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        if (FAILED(cmdList->QueryInterface(IID_PPV_ARGS(&cmdList4)))) return;

        tlasInstanceCount_ = static_cast<UINT>(instances.size());

        const UINT64 instanceBufferSize =
            sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instances.size();

        // インスタンスバッファの確保（必要に応じて拡張）
        if (!tlasInstanceDescBuffer_ ||
            tlasInstanceDescBuffer_->GetDesc().Width < instanceBufferSize)
        {
            if (tlasInstanceDescBuffer_) {
                retiredResources_.push_back(std::move(tlasInstanceDescBuffer_));
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
                nullptr, IID_PPV_ARGS(&tlasInstanceDescBuffer_));
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
        tlasInstanceDescBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        for (UINT i = 0; i < static_cast<UINT>(instances.size()); ++i) {
            mapped[i] = {};
            memcpy(mapped[i].Transform, instances[i].transform, sizeof(mapped[i].Transform));
            mapped[i].InstanceID = i;
            mapped[i].InstanceMask = instances[i].instanceMask;
            mapped[i].InstanceContributionToHitGroupIndex = 0;
            mapped[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            mapped[i].AccelerationStructure =
                blasList_[instances[i].blasIndex].result->GetGPUVirtualAddress();
        }
        tlasInstanceDescBuffer_->Unmap(0, nullptr);

        // TLAS プレビルド情報
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
        inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        inputs.NumDescs = static_cast<UINT>(instances.size());
        inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        inputs.InstanceDescs = tlasInstanceDescBuffer_->GetGPUVirtualAddress();

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

        ensureASBuffer(tlasResult_, prebuild.ResultDataMaxSizeInBytes);
        ensureScratchBuffer(tlasScratch_, prebuild.ScratchDataSizeInBytes);

        // TLAS のビルド
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
        buildDesc.DestAccelerationStructureData = tlasResult_->GetGPUVirtualAddress();
        buildDesc.Inputs = inputs;
        buildDesc.ScratchAccelerationStructureData = tlasScratch_->GetGPUVirtualAddress();

        cmdList4->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

        // UAV バリア
        ResourceBarrierHelper::UAV(cmdList, tlasResult_.Get());

        // TLAS の SRV を作成（初回のみ確保、以降は更新）
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.RaytracingAccelerationStructure.Location =
            tlasResult_->GetGPUVirtualAddress();

        if (tlasSRVHandle_.ptr == 0) {
            // 初回: DescriptorManager から SRV スロットを確保
            descriptorManager_->CreateSRV(nullptr, srvDesc,
                tlasSRVCpuHandle_, tlasSRVHandle_, "TLAS");

            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Graphics,
                "TLAS built ({} instances, SRV allocated)", instances.size());
        } else {
            // 2回目以降: CPU ハンドルを使って SRV を上書き更新
            device5_->CreateShaderResourceView(nullptr, &srvDesc, tlasSRVCpuHandle_);
        }
    }

    bool AccelerationStructureManager::BuildBLASFromModelResource(
        ID3D12GraphicsCommandList* cmdList, ModelResource* resource)
    {
        if (!isSupported_ || !resource || !resource->IsLoaded()) return false;

        // 既に構築済みならスキップ
        if (resource->HasBLAS()) return true;

        BLASDesc desc{};
        desc.vertexBuffer = resource->GetVertexBuffer();
        desc.vertexCount = resource->GetVertexCount();
        desc.vertexStride = sizeof(VertexData);
        desc.vertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        desc.vertexPositionOffset = 0;  // VertexData::position は先頭メンバ（Vector4 だが xyz のみ使用）
        desc.indexBuffer = resource->GetIndexBuffer();
        desc.indexCount = resource->GetIndexCount();
        desc.indexFormat = DXGI_FORMAT_R32_UINT;

        UINT blasIndex = BuildBLAS(cmdList, desc);
        if (blasIndex == UINT_MAX) return false;

        resource->SetBLASIndex(blasIndex);
        return true;
    }

    void AccelerationStructureManager::EnsureScratchBuffer(UINT64 requiredSize)
    {
        if (blasScratchSize_ >= requiredSize) return;
        blasScratchSize_ = requiredSize;

        // 旧バッファはコマンドリスト実行完了まで保持する必要がある
        if (blasScratch_) {
            retiredResources_.push_back(std::move(blasScratch_));
        }

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC resDesc{};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = requiredSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        device5_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(&blasScratch_));
    }

    void AccelerationStructureManager::FlushRetiredResources(
        CommandManager* commandManager, UINT frameIndex)
    {
        if (retiredResources_.empty()) {
            return;
        }

        // 退避リソースを参照しているGPUフレームの完了を待ってから解放する
        if (commandManager) {
            commandManager->WaitForFrame(frameIndex);
        }

        retiredResources_.clear();
    }
}
