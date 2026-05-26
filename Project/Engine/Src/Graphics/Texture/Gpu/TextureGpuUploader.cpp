#include "pch.h"
#include "TextureGpuUploader.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"
#include "Utility/FileErrorDialog/FileErrorDialog.h"
#include "externals/DirectXTex/d3dx12.h"

#include <vector>
#include <stdexcept>
#include <string>

namespace CoreEngine
{
    std::mutex TextureGpuUploader::gpuUploadMutex_;

    TextureGpuUploader::UploadResult TextureGpuUploader::UploadAndCreateSrv(
        CoreEngine::DirectXCommon* dxCommon,
        const DirectX::ScratchImage& mipImages,
        const std::string& resolvedPath)
    {
        // コマンドリストとディスクリプタ確保はスレッドセーフでないため直列化する。
        std::lock_guard<std::mutex> lock(gpuUploadMutex_);

        // 生成済みミップチェーンからGPUリソース記述子を構築する。
        const DirectX::TexMetadata& texMetadata = mipImages.GetMetadata();

        D3D12_RESOURCE_DESC resourceDesc{};
        resourceDesc.Width = UINT(texMetadata.width);
        resourceDesc.Height = UINT(texMetadata.height);
        resourceDesc.MipLevels = UINT16(texMetadata.mipLevels);
        resourceDesc.DepthOrArraySize = UINT16(texMetadata.arraySize);
        resourceDesc.Format = texMetadata.format;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(texMetadata.dimension);

        UploadResult result{};
        result.texture = ResourceFactory::CreateTextureResource(
            dxCommon->GetDevice(),
            resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST);

        // CPUメモリ上の画像データをアップロードバッファ経由でGPUへ転送する。
        std::vector<D3D12_SUBRESOURCE_DATA> subResources;
        DirectX::PrepareUpload(dxCommon->GetDevice(), mipImages.GetImages(), mipImages.GetImageCount(), texMetadata, subResources);

        uint64_t intermediateSize = GetRequiredIntermediateSize(result.texture.Get(), 0, UINT(subResources.size()));
        result.intermediate = ResourceFactory::CreateBufferResource(dxCommon->GetDevice(), intermediateSize);

        UpdateSubresources(dxCommon->GetCommandList(), result.texture.Get(), result.intermediate.Get(), 0, 0, UINT(subResources.size()), subResources.data());

        D3D12_RESOURCE_STATES texState = D3D12_RESOURCE_STATE_COPY_DEST;
        ResourceBarrierHelper::Transition(
            dxCommon->GetCommandList(), result.texture.Get(),
            texState, D3D12_RESOURCE_STATE_GENERIC_READ);

        // 作成したテクスチャに対してビューを構築し、描画で利用できる状態にする。
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = texMetadata.format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (texMetadata.IsCubemap()) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = UINT_MAX;
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        } else {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = UINT(texMetadata.mipLevels);
        }

        dxCommon->GetDescriptorManager()->CreateSRV(
            result.texture.Get(),
            srvDesc,
            result.cpuHandle,
            result.gpuHandle,
            resolvedPath);

        return result;
    }
}


