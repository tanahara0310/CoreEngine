#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <string>
#include <wrl.h>

namespace CoreEngine::ComputePipelineUtil
{
    /// @brief Compute PSO 生成の一元化ヘルパー
    /// @details 失敗時は名前・HRESULT・DeviceRemovedReason をログへ出し、成功時は PSO へ SetName する。
    /// @return 生成した PSO（失敗時は nullptr。エラーはログ済み）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> Create(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        IDxcBlob* cs,
        const std::string& debugName);

} // namespace CoreEngine::ComputePipelineUtil
