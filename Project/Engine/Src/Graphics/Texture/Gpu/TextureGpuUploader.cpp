#include "TextureGpuUploader.h"

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Utility/Logger/Logger.h"
#include "Utility/FileErrorDialog/FileErrorDialog.h"
#include "externals/DirectXTex/d3dx12.h"

#include <vector>
#include <stdexcept>
#include <string>

namespace CoreEngine
{
    TextureGpuUploader::UploadResult TextureGpuUploader::UploadAndCreateSrv(
        CoreEngine::DirectXCommon* dxCommon,
        const DirectX::ScratchImage& mipImages,
        const std::string& resolvedPath)
    {
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

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

        UploadResult result{};
        HRESULT hr = dxCommon->GetDevice()->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&result.texture));

        if (FAILED(hr)) {
            std::string errorMsg = "Failed to create texture resource: " + resolvedPath;
            Logger::GetInstance().Log(errorMsg, LogLevel::Error, LogCategory::Graphics);
            FileErrorDialog::ShowTextureError("Failed to create texture resource", resolvedPath, hr);
            throw std::runtime_error(errorMsg);
        }

        // CPUメモリ上の画像データをアップロードバッファ経由でGPUへ転送する。
        std::vector<D3D12_SUBRESOURCE_DATA> subResources;
        DirectX::PrepareUpload(dxCommon->GetDevice(), mipImages.GetImages(), mipImages.GetImageCount(), texMetadata, subResources);

        uint64_t intermediateSize = GetRequiredIntermediateSize(result.texture.Get(), 0, UINT(subResources.size()));
        result.intermediate = ResourceFactory::CreateBufferResource(dxCommon->GetDevice(), intermediateSize);

        UpdateSubresources(dxCommon->GetCommandList(), result.texture.Get(), result.intermediate.Get(), 0, 0, UINT(subResources.size()), subResources.data());

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = result.texture.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
        dxCommon->GetCommandList()->ResourceBarrier(1, &barrier);

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
