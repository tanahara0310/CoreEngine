#include "pch.h"
#include "ComputePipelineUtil.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine::ComputePipelineUtil
{
    Microsoft::WRL::ComPtr<ID3D12PipelineState> Create(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        IDxcBlob* cs,
        const std::string& debugName)
    {
        if (!device || !rootSignature || !cs) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "Compute PSO生成失敗: name={} 引数が不正です (device={}, rootSignature={}, cs={})",
                debugName,
                static_cast<const void*>(device),
                static_cast<const void*>(rootSignature),
                static_cast<const void*>(cs));
            return nullptr;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSignature;
        desc.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
        const HRESULT hr = device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pso));
        if (FAILED(hr)) {
            Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics,
                "Compute PSO生成失敗: name={} HRESULT={:#010x} DeviceRemovedReason={:#010x}",
                debugName,
                static_cast<uint32_t>(hr),
                static_cast<uint32_t>(device->GetDeviceRemovedReason()));
            return nullptr;
        }

        // PIX・デバッグレイヤーのメッセージで識別できるよう名前を付ける
        pso->SetName(Logger::GetInstance().Utf8ToWide(debugName).c_str());

        return pso;
    }

} // namespace CoreEngine::ComputePipelineUtil
