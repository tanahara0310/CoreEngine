#include "pch.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "SkinClusterGenerator.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Math/MathCore.h"
#include <algorithm>
#include <cassert>


namespace CoreEngine
{
namespace {
    // GPUスキニング用頂点1つ分のバイトサイズ（Skinning.CS.hlsl の SkinnedVertexGPU と一致）
    constexpr size_t kSkinnedVertexStride = sizeof(float) * 12; // position(4) + texcoord(2) + normal(3) + tangent(3)
    // GPUスキニング用Influence1つ分のバイトサイズ（Skinning.CS.hlsl の InfluenceGPU と一致）
    constexpr size_t kInfluenceStride = sizeof(VertexInfluence);
}

CoreEngine::SkinCluster SkinClusterGenerator::CreateSkinCluster(
    const Microsoft::WRL::ComPtr<ID3D12Device>& device,
    const Skeleton& skeleton,
    const ModelData& modelData,
    DescriptorAllocator* descriptorAllocator,
    ID3D12Resource* sourceVertexBuffer,
    UINT vertexCount) {

    SkinCluster skinCluster;

    // palette用のResourceを確保
    skinCluster.paletteResource = ResourceFactory::CreateBufferResource(device, sizeof(WellForGPU) * skeleton.joints.size());
    WellForGPU* mappedPalette = nullptr;
    skinCluster.paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedPalette));
    skinCluster.mappedPalette = { mappedPalette, skeleton.joints.size() }; // spanを使ってアクセスするようにする

    // palette用のsrvを作成。StructuredBufferでアクセスできるようにする。
    D3D12_SHADER_RESOURCE_VIEW_DESC paletteSrvDesc{};
    paletteSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    paletteSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    paletteSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    paletteSrvDesc.Buffer.FirstElement = 0;
    paletteSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    paletteSrvDesc.Buffer.NumElements = UINT(skeleton.joints.size());
    paletteSrvDesc.Buffer.StructureByteStride = sizeof(WellForGPU);
    skinCluster.paletteSrvHandle = descriptorAllocator->CreateSRV(skinCluster.paletteResource.Get(), paletteSrvDesc, "SkinCluster Palette");

    // influence用のResourceを確保。頂点ごとにinfluence情報を追加できるようにする
    skinCluster.influenceResource = ResourceFactory::CreateBufferResource(device, sizeof(VertexInfluence) * modelData.vertices.size());
    VertexInfluence* mappedInfluence = nullptr;
    skinCluster.influenceResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfluence));
    std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * modelData.vertices.size()); // 0埋め。weightを0にしておく
    skinCluster.mappedInfluence = { mappedInfluence, modelData.vertices.size() };

    // Influence用のVBVを作成
    skinCluster.influenceBufferView.BufferLocation = skinCluster.influenceResource->GetGPUVirtualAddress();
    skinCluster.influenceBufferView.SizeInBytes = UINT(sizeof(VertexInfluence) * modelData.vertices.size());
    skinCluster.influenceBufferView.StrideInBytes = sizeof(VertexInfluence);

    // Influence用のSRVを作成（GPUスキニング(CS)が読み取るため）
    D3D12_SHADER_RESOURCE_VIEW_DESC influenceSrvDesc{};
    influenceSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    influenceSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    influenceSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    influenceSrvDesc.Buffer.FirstElement = 0;
    influenceSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    influenceSrvDesc.Buffer.NumElements = UINT(modelData.vertices.size());
    influenceSrvDesc.Buffer.StructureByteStride = UINT(kInfluenceStride);
    skinCluster.influenceSrvHandle = descriptorAllocator->CreateSRV(skinCluster.influenceResource.Get(), influenceSrvDesc, "SkinCluster InfluenceSRV");

    // 元頂点バッファのSRVを作成（GPUスキニング(CS)が読み取るため）
    D3D12_SHADER_RESOURCE_VIEW_DESC sourceVertexSrvDesc{};
    sourceVertexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    sourceVertexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sourceVertexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sourceVertexSrvDesc.Buffer.FirstElement = 0;
    sourceVertexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    sourceVertexSrvDesc.Buffer.NumElements = vertexCount;
    sourceVertexSrvDesc.Buffer.StructureByteStride = UINT(kSkinnedVertexStride);
    skinCluster.sourceVertexSrvHandle = descriptorAllocator->CreateSRV(sourceVertexBuffer, sourceVertexSrvDesc, "SkinCluster SourceVertexSRV");

    // GPUスキニング出力バッファ（UAV）を作成し、そのままVBVとしても使えるようにする
    {
        const size_t outputSizeInBytes = kSkinnedVertexStride * vertexCount;

        D3D12_RESOURCE_DESC outputDesc{};
        outputDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        outputDesc.Width = outputSizeInBytes;
        outputDesc.Height = 1;
        outputDesc.DepthOrArraySize = 1;
        outputDesc.MipLevels = 1;
        outputDesc.Format = DXGI_FORMAT_UNKNOWN;
        outputDesc.SampleDesc.Count = 1;
        outputDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        outputDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        skinCluster.outputVertexResource.Reset(
            ResourceFactory::CreateTextureResource(
                device, outputDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        D3D12_UNORDERED_ACCESS_VIEW_DESC outputUavDesc{};
        outputUavDesc.Format = DXGI_FORMAT_UNKNOWN;
        outputUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        outputUavDesc.Buffer.FirstElement = 0;
        outputUavDesc.Buffer.NumElements = vertexCount;
        outputUavDesc.Buffer.StructureByteStride = UINT(kSkinnedVertexStride);
        outputUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        skinCluster.outputUavHandle = descriptorAllocator->CreateUAV(skinCluster.outputVertexResource.Get(), outputUavDesc, "SkinCluster OutputVertexUAV");

        skinCluster.outputVertexBufferView.BufferLocation = skinCluster.outputVertexResource.GpuAddress();
        skinCluster.outputVertexBufferView.SizeInBytes = UINT(outputSizeInBytes);
        skinCluster.outputVertexBufferView.StrideInBytes = UINT(kSkinnedVertexStride);
    }

    // SkinningParams（頂点数）用の定数バッファを作成（頂点数はモデル固有で不変のため一度だけ書き込む）
    {
        struct SkinningParamsCB {
            uint32_t vertexCount;
            uint32_t pad[3];
        };
        static constexpr Cb::Field kSkinningParamsFields[] = {
            CB_FIELD(SkinningParamsCB, vertexCount), CB_FIELD(SkinningParamsCB, pad),
        };
        CB_VERIFY_LAYOUT(SkinningParamsCB, kSkinningParamsFields);
        skinCluster.skinningParamsCB = ResourceFactory::CreateBufferResource(device, sizeof(SkinningParamsCB));
        SkinningParamsCB* mapped = nullptr;
        skinCluster.skinningParamsCB->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        mapped->vertexCount = vertexCount;
        mapped->pad[0] = mapped->pad[1] = mapped->pad[2] = 0;
        skinCluster.skinningParamsCB->Unmap(0, nullptr);
    }

    // InverseBindPoseMatrixの格納領域を作成して、単位行列で埋める
    skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
    std::generate(skinCluster.inverseBindPoseMatrices.begin(), skinCluster.inverseBindPoseMatrices.end(), CoreEngine::MathCore::Matrix::Identity);

    // ModelDataのSkinCluster情報を解析してInfluenceの中身を埋める
    for (const auto& jointWeight : modelData.skinClusterData) { // ModelのSkinClusterの情報を解析
        auto it = skeleton.jointMap.find(jointWeight.first); // jointWeight.firstはjoint名なので、Skeltonに対象となるjointが含まれているか判断
        if (it == skeleton.jointMap.end()) {
            continue; //そんな名前のjointは存在しない。なので次に回す
        }

        //(*it).secondにはjointのIndexが入っているので、該当のIndexのinverseBindPoseMatrixを代入
        skinCluster.inverseBindPoseMatrices[(*it).second] = jointWeight.second.inverseBindPoseMatrix;
        for (const auto& vertexWeight : jointWeight.second.vertexWeights) {
            auto& currentInfluence = skinCluster.mappedInfluence[vertexWeight.vertexIndex]; // 該当のvertexIndexのinfluence情報を参照

            for (uint32_t index = 0; index < kNumMaxInfluence; ++index) { //空いてる所に入れる
                if (currentInfluence.weights[index] == 0.0f) { //weiht == 0が空いてる状態なので、その場所にweightとjointのindexを代入
                    currentInfluence.weights[index] = vertexWeight.weight;
                    currentInfluence.jointIndices[index] = (*it).second; // jointのIndexを代入
                    break; // 入れたら抜ける
                }
            }
        }
    }

    return skinCluster;
}

void SkinClusterGenerator::Update(SkinCluster& skinCluster, const Skeleton& skeleton)
{
    for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex) {
        assert(jointIndex < skinCluster.mappedPalette.size());

        skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix =
            skinCluster.inverseBindPoseMatrices[jointIndex] * skeleton.joints[jointIndex].skeletonSpaceMatrix;
        skinCluster.mappedPalette[jointIndex].skeletonSpaceInverseTransposeMatrix =
            CoreEngine::MathCore::Matrix::Transpose(CoreEngine::MathCore::Matrix::Inverse(skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix));
    }

    // パレットが更新されたので、次回描画前にGPUスキニング(CS)の再実行が必要
    skinCluster.needsGPUSkinning = true;
}
}
