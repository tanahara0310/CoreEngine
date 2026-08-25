#include "pch.h"
#include "TextureGpuUploader.h"

#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/Command/UploadContext.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
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
        CoreEngine::GraphicsCore* dxCommon,
        const DirectX::ScratchImage& mipImages,
        const std::string& resolvedPath)
    {
        // ディスクリプタ確保・リソース生成はスレッドセーフでないため直列化する。
        // （コマンドリストへの記録は UploadContext 側のロックが守る）
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
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediate =
            ResourceFactory::CreateBufferResource(dxCommon->GetDevice(), intermediateSize);

        // フレームの描画用コマンドリストではなく専用コンテキストへ積む。
        // この関数はワーカースレッドから呼ばれるため、描画用リストへ書くと
        // メインスレッドの記録と同時アクセスになる（D3D12 のコマンドリストは非スレッドセーフ）。
        {
            UploadContext::ScopedRecording recording = dxCommon->GetUploadContext()->BeginRecording();
            if (!recording.IsValid()) {
                throw std::runtime_error("TextureGpuUploader: failed to begin an upload recording");
            }

            UpdateSubresources(recording.List(), result.texture.Get(), intermediate.Get(),
                0, 0, UINT(subResources.size()), subResources.data());

            // テクスチャキャッシュ側は生ハンドルのまま扱うので、ここだけ一時的に追跡する
            GpuResource uploaded;
            uploaded.Reset(result.texture, D3D12_RESOURCE_STATE_COPY_DEST);
            Barrier::Transition(
                recording.List(), uploaded, D3D12_RESOURCE_STATE_GENERIC_READ);

            // 中間バッファは GPU コピー完了まで生きていればよい。
            // 呼び出し側が永久に握る必要はなく、フェンス通過後に自動解放される。
            recording.KeepAlive(std::move(intermediate));
        } // ここで Close → ExecuteCommandLists → Signal

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

        result.descriptor = dxCommon->GetDescriptorAllocator()->CreateSRV(result.texture.Get(), srvDesc, resolvedPath);

        return result;
    }
}


