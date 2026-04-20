#include "ShaderTableBuilder.h"
#include "Utility/Logger/Logger.h"

#include <cassert>
#include <cstring>

namespace CoreEngine
{
    // =========================================================================
    // エントリ登録（流暢インターフェース）
    // =========================================================================
    ShaderTableBuilder& ShaderTableBuilder::SetRayGenShader(const wchar_t* name)
    {
        assert(name);
        rayGenName_ = name;
        return *this;
    }

    ShaderTableBuilder& ShaderTableBuilder::AddMissShader(const wchar_t* name)
    {
        assert(name);
        missNames_.push_back(name);
        return *this;
    }

    ShaderTableBuilder& ShaderTableBuilder::AddHitGroup(const wchar_t* name)
    {
        assert(name);
        hitGroupNames_.push_back(name);
        return *this;
    }

    // =========================================================================
    // Build
    // =========================================================================
    bool ShaderTableBuilder::Build(ID3D12Device* device, ID3D12StateObjectProperties* props)
    {
        assert(device);
        assert(props);
        assert(rayGenName_);
        assert(!missNames_.empty());
        assert(!hitGroupNames_.empty());

        // 1 レコード = ShaderIdentifier（32 bytes）をレコードアライメントに切り上げ
        recordSize_ = Align(
            D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

        // 各セクションの StartAddress はテーブルアライメント（64 bytes）境界に揃える
        // バッファ内レイアウト: [RayGen セクション][Miss セクション][HitGroup セクション]
        const UINT64 kTableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

        UINT64 rayGenSectionSize = Align(recordSize_, kTableAlign);
        UINT64 missSectionSize = Align(missNames_.size() * recordSize_, kTableAlign);
        UINT64 hitGroupSectionSize = Align(hitGroupNames_.size() * recordSize_, kTableAlign);
        UINT64 totalSize = rayGenSectionSize + missSectionSize + hitGroupSectionSize;

        // オフセット（StartAddress 計算用）
        rayGenOffset_ = 0;
        missOffset_ = rayGenSectionSize;
        hitGroupOffset_ = rayGenSectionSize + missSectionSize;

        // DispatchRays の SizeInBytes は実データ部分のみ（パディング除く）
        missTableSize_ = missNames_.size() * recordSize_;
        hitGroupTableSize_ = hitGroupNames_.size() * recordSize_;

        // UPLOAD ヒープバッファを作成（CPU から書き込み、GPU から読み取り）
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = totalSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(buffer_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            Logger::GetInstance().Log(
                "ShaderTableBuilder: Failed to create buffer",
                LogLevel::Error, LogCategory::Graphics);
            return false;
        }

        // シェーダー識別子を書き込む
        uint8_t* mapped = nullptr;
        buffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memset(mapped, 0, static_cast<size_t>(totalSize));

        // RayGen
        void* rayGenId = props->GetShaderIdentifier(rayGenName_);
        std::memcpy(mapped + rayGenOffset_, rayGenId, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

        // Miss（複数対応）
        for (size_t i = 0; i < missNames_.size(); ++i) {
            void* id = props->GetShaderIdentifier(missNames_[i]);
            std::memcpy(mapped + missOffset_ + i * recordSize_,
                id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }

        // HitGroup（複数対応）
        for (size_t i = 0; i < hitGroupNames_.size(); ++i) {
            void* id = props->GetShaderIdentifier(hitGroupNames_[i]);
            std::memcpy(mapped + hitGroupOffset_ + i * recordSize_,
                id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        }

        buffer_->Unmap(0, nullptr);
        return true;
    }

    // =========================================================================
    // BuildDispatchDesc
    // =========================================================================
    D3D12_DISPATCH_RAYS_DESC ShaderTableBuilder::BuildDispatchDesc(
        UINT width, UINT height, UINT depth) const
    {
        assert(IsBuilt());

        D3D12_GPU_VIRTUAL_ADDRESS base = buffer_->GetGPUVirtualAddress();

        D3D12_DISPATCH_RAYS_DESC desc{};

        desc.RayGenerationShaderRecord.StartAddress = base + rayGenOffset_;
        desc.RayGenerationShaderRecord.SizeInBytes = recordSize_;

        desc.MissShaderTable.StartAddress = base + missOffset_;
        desc.MissShaderTable.SizeInBytes = missTableSize_;
        desc.MissShaderTable.StrideInBytes = recordSize_;

        desc.HitGroupTable.StartAddress = base + hitGroupOffset_;
        desc.HitGroupTable.SizeInBytes = hitGroupTableSize_;
        desc.HitGroupTable.StrideInBytes = recordSize_;

        desc.Width = width;
        desc.Height = height;
        desc.Depth = depth;

        return desc;
    }

    // =========================================================================
    // ヘルパー
    // =========================================================================
    UINT64 ShaderTableBuilder::Align(UINT64 size, UINT64 alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }
}
