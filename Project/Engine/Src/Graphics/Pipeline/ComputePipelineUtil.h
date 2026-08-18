#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <string>
#include <wrl.h>

namespace CoreEngine::ComputePipelineUtil
{
    /// @brief Compute PSO 生成の一元化ヘルパー
    /// @details 各所で微妙に違っていたエラー処理を統一する:
    ///          - 失敗時は名前・HRESULT・DeviceRemovedReason をエラーログに出す
    ///          - 成功時は PSO に SetName する（PIX・デバッグレイヤーでの識別用）
    /// @param device D3D12 デバイス
    /// @param rootSignature ルートシグネチャ
    /// @param cs コンパイル済みコンピュートシェーダー
    /// @param debugName デバッグ名（例: "HiZ_Build", "Skinning"）
    /// @return 生成した PSO（失敗時は nullptr。エラーはログ済み）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> Create(
        ID3D12Device* device,
        ID3D12RootSignature* rootSignature,
        IDxcBlob* cs,
        const std::string& debugName);

} // namespace CoreEngine::ComputePipelineUtil
