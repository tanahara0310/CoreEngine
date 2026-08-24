#include "pch.h"
#include "ShaderBinder.h"
#include "Utility/Logger/Logger.h"

#include <cassert>

namespace CoreEngine
{
    void ShaderBinder::Set(RootSlot slot, D3D12_GPU_DESCRIPTOR_HANDLE table)
    {
        // 未解決スロット（シェーダーがそのリソースを持っていない）は黙って捨ててよい。
        // 「持っているのに差し忘れた」の検出は boundMask_ 側の仕事。
        if (!slot.IsValid()) {
            return;
        }

        // 種別違いは「差せてしまってから GPU が落ちる」ので、ここで確実に殺す。
        // RootSRV のスロットへテーブルハンドルを渡すと GPU 仮想アドレスとして解釈される。
        assert(slot.kind == RootSlotKind::DescriptorTable &&
            "DescriptorTable 以外のスロットへ GPU_DESCRIPTOR_HANDLE を差そうとしました");
        if (slot.kind != RootSlotKind::DescriptorTable) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader,
                "バインド種別の不一致: rootParam={} は {} なのにディスクリプタテーブルを差そうとしました",
                slot.index, ToString(slot.kind));
            return;
        }

        if (pipeline_ == Pipeline::Graphics) {
            cmdList_->SetGraphicsRootDescriptorTable(slot.index, table);
        }
        else {
            cmdList_->SetComputeRootDescriptorTable(slot.index, table);
        }
        boundMask_ |= Bit(slot.index);
    }

    void ShaderBinder::Set(RootSlot slot, D3D12_GPU_VIRTUAL_ADDRESS address)
    {
        if (!slot.IsValid()) {
            return;
        }

        const bool graphics = (pipeline_ == Pipeline::Graphics);

        switch (slot.kind) {
        case RootSlotKind::RootCBV:
            if (graphics) cmdList_->SetGraphicsRootConstantBufferView(slot.index, address);
            else          cmdList_->SetComputeRootConstantBufferView(slot.index, address);
            break;

        case RootSlotKind::RootSRV:
            if (graphics) cmdList_->SetGraphicsRootShaderResourceView(slot.index, address);
            else          cmdList_->SetComputeRootShaderResourceView(slot.index, address);
            break;

        case RootSlotKind::RootUAV:
            if (graphics) cmdList_->SetGraphicsRootUnorderedAccessView(slot.index, address);
            else          cmdList_->SetComputeRootUnorderedAccessView(slot.index, address);
            break;

        default:
            // DescriptorTable / RootConstants のスロットへ GPU 仮想アドレスを渡した
            assert(false && "ルートディスクリプタ以外のスロットへ GPU_VIRTUAL_ADDRESS を差そうとしました");
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader,
                "バインド種別の不一致: rootParam={} は {} なのに GPU 仮想アドレスを差そうとしました",
                slot.index, ToString(slot.kind));
            return;
        }

        boundMask_ |= Bit(slot.index);
    }

    void ShaderBinder::SetConstantsRaw(RootSlot slot, const void* data, uint32_t num32BitValues)
    {
        if (!slot.IsValid()) {
            return;
        }

        assert(slot.kind == RootSlotKind::RootConstants &&
            "RootConstants 以外のスロットへルート定数を差そうとしました");
        if (slot.kind != RootSlotKind::RootConstants) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Shader,
                "バインド種別の不一致: rootParam={} は {} なのにルート定数を差そうとしました",
                slot.index, ToString(slot.kind));
            return;
        }

        if (pipeline_ == Pipeline::Graphics) {
            cmdList_->SetGraphicsRoot32BitConstants(slot.index, num32BitValues, data, 0);
        }
        else {
            cmdList_->SetComputeRoot32BitConstants(slot.index, num32BitValues, data, 0);
        }
        boundMask_ |= Bit(slot.index);
    }
}
