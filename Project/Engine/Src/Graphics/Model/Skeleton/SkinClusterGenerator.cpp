#include "SkinClusterGenerator.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Math/MathCore.h"
#include <algorithm>
#include <cassert>


namespace CoreEngine
{
CoreEngine::SkinCluster SkinClusterGenerator::CreateSkinCluster(
    const Microsoft::WRL::ComPtr<ID3D12Device>& device,
    const Skeleton& skeleton,
    const ModelData& modelData,
    DescriptorManager* descriptorManager) {

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
    descriptorManager->CreateSRV(skinCluster.paletteResource.Get(), paletteSrvDesc,
        skinCluster.paletteSrvHandle.first, skinCluster.paletteSrvHandle.second, "SkinCluster Palette");

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
}
}
