#include "pch.h"
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
        if (FAILED(buffer_->Map(0, nullptr, reinterpret_cast<void**>(&mapped))) || !mapped) {
            Logger::GetInstance().Log(
                "ShaderTableBuilder: Failed to map buffer",
                LogLevel::Error, LogCategory::Graphics);
            buffer_.Reset();
            return false;
        }
        std::memset(mapped, 0, static_cast<size_t>(totalSize));

        // State Object に無い名前は識別子が null で返るので、その時点で作るのをやめる
        auto writeRecord = [&](const wchar_t* name, UINT64 offset, const char* section, size_t index) {
            const void* id = props->GetShaderIdentifier(name);
            if (!id) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                    "ShaderTableBuilder: shader identifier not found. section={} index={}", section, index);
                return false;
            }
            std::memcpy(mapped + offset, id, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            return true;
        };

        bool written = writeRecord(rayGenName_, rayGenOffset_, "RayGen", 0);
        for (size_t i = 0; written && i < missNames_.size(); ++i) {
            written = writeRecord(missNames_[i], missOffset_ + i * recordSize_, "Miss", i);
        }
        for (size_t i = 0; written && i < hitGroupNames_.size(); ++i) {
            written = writeRecord(hitGroupNames_[i], hitGroupOffset_ + i * recordSize_, "HitGroup", i);
        }

        buffer_->Unmap(0, nullptr);
        if (!written) {
            buffer_.Reset();
            return false;
        }
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

        // 空の表は番地も 0 にする
        if (missTableSize_ > 0) {
            desc.MissShaderTable.StartAddress = base + missOffset_;
            desc.MissShaderTable.SizeInBytes = missTableSize_;
            desc.MissShaderTable.StrideInBytes = recordSize_;
        }
        if (hitGroupTableSize_ > 0) {
            desc.HitGroupTable.StartAddress = base + hitGroupOffset_;
            desc.HitGroupTable.SizeInBytes = hitGroupTableSize_;
            desc.HitGroupTable.StrideInBytes = recordSize_;
        }

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
