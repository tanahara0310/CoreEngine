#include "pch.h"
#include "Graphics/Shader/CBufferLayout.h"
#include "SkinClusterGenerator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Math/MathCore.h"
#include "Utility/FrameRate/Time.h"
#include <algorithm>
#include <cassert>


namespace CoreEngine
{
namespace {
    // GPUスキニング用頂点1つ分のバイトサイズ（Skinning.CS.hlsl の SkinnedVertexGPU と一致）
    // 元の頂点バッファ（VertexData）をそのまま StructuredBuffer として読むので、VertexData と同じにする
    constexpr size_t kSkinnedVertexStride = sizeof(VertexData);
    // position(4) + texcoord(2) + normal(3) + tangent(3) + animData(4) を全てスカラーで並べた SkinnedVertexGPU
    static_assert(kSkinnedVertexStride == sizeof(float) * 16,
        "VertexData を変えたら Skinning.CS.hlsl の SkinnedVertexGPU も合わせること");
    // GPUスキニング用Influence1つ分のバイトサイズ（Skinning.CS.hlsl の InfluenceGPU と一致）
    constexpr size_t kInfluenceStride = sizeof(VertexInfluence);
    // 前のフレームの姿勢の位置 1 つ分のバイトサイズ（Skinning.CS.hlsl の gPrevPositions の float4）
    constexpr size_t kPrevPositionStride = sizeof(float) * 4;
}

CoreEngine::SkinCluster SkinClusterGenerator::CreateSkinCluster(
    GraphicsCore& graphics,
    const Skeleton& skeleton,
    const ModelData& modelData,
    ID3D12Resource* sourceVertexBuffer,
    UINT vertexCount) {

    const Microsoft::WRL::ComPtr<ID3D12Device> device = graphics.GetDevice();
    DescriptorAllocator* const descriptorAllocator = graphics.GetDescriptorAllocator();

    SkinCluster skinCluster;

    // palette（StructuredBuffer）。姿勢は毎フレーム変わるので、フレームスロットごとに置き場を持つ
    skinCluster.palette.Initialize(graphics, UINT(skeleton.joints.size()), "SkinCluster Palette");
    skinCluster.prevPalette.Initialize(graphics, UINT(skeleton.joints.size()), "SkinCluster PrevPalette");

    // InverseBindPoseMatrixの格納領域を作成して、単位行列で埋める
    skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
    std::generate(skinCluster.inverseBindPoseMatrices.begin(), skinCluster.inverseBindPoseMatrices.end(), CoreEngine::MathCore::Matrix::Identity);

    // 頂点ごとのinfluence情報。0埋めしてweightを0にしておく
    std::vector<VertexInfluence> influences(modelData.vertices.size());

    // ModelDataのSkinCluster情報を解析してInfluenceの中身を埋める
    for (const auto& jointWeight : modelData.skinClusterData) { // ModelのSkinClusterの情報を解析
        auto it = skeleton.jointMap.find(jointWeight.first); // jointWeight.firstはjoint名なので、Skeltonに対象となるjointが含まれているか判断
        if (it == skeleton.jointMap.end()) {
            continue; //そんな名前のjointは存在しない。なので次に回す
        }

        //(*it).secondにはjointのIndexが入っているので、該当のIndexのinverseBindPoseMatrixを代入
        skinCluster.inverseBindPoseMatrices[(*it).second] = jointWeight.second.inverseBindPoseMatrix;
        for (const auto& vertexWeight : jointWeight.second.vertexWeights) {
            auto& currentInfluence = influences[vertexWeight.vertexIndex]; // 該当のvertexIndexのinfluence情報を参照

            for (uint32_t index = 0; index < kNumMaxInfluence; ++index) { //空いてる所に入れる
                if (currentInfluence.weights[index] == 0.0f) { //weiht == 0が空いてる状態なので、その場所にweightとjointのindexを代入
                    currentInfluence.weights[index] = vertexWeight.weight;
                    currentInfluence.jointIndices[index] = (*it).second; // jointのIndexを代入
                    break; // 入れたら抜ける
                }
            }
        }
    }

    // influence用のResourceを確保し、組んだinfluence情報を写す
    const std::span<const VertexInfluence> influenceSpan = influences;
    skinCluster.influenceResource = ResourceFactory::CreateStaticBuffer<VertexInfluence>(device, influenceSpan);

    // Influence用のVBVを作成
    skinCluster.influenceBufferView.BufferLocation = skinCluster.influenceResource->GetGPUVirtualAddress();
    skinCluster.influenceBufferView.SizeInBytes = UINT(influenceSpan.size_bytes());
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
    skinCluster.influenceSrvHandle = UniqueDescriptor(*descriptorAllocator,
        descriptorAllocator->CreateSRV(skinCluster.influenceResource.Get(), influenceSrvDesc, "SkinCluster InfluenceSRV"));

    // 元頂点バッファのSRVを作成（GPUスキニング(CS)が読み取るため）
    D3D12_SHADER_RESOURCE_VIEW_DESC sourceVertexSrvDesc{};
    sourceVertexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    sourceVertexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sourceVertexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sourceVertexSrvDesc.Buffer.FirstElement = 0;
    sourceVertexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    sourceVertexSrvDesc.Buffer.NumElements = vertexCount;
    sourceVertexSrvDesc.Buffer.StructureByteStride = UINT(kSkinnedVertexStride);
    skinCluster.sourceVertexSrvHandle = UniqueDescriptor(*descriptorAllocator,
        descriptorAllocator->CreateSRV(sourceVertexBuffer, sourceVertexSrvDesc, "SkinCluster SourceVertexSRV"));

    // GPUスキニング出力バッファ（UAV）を作成し、そのままVBVとしても使えるようにする
    {
        const size_t outputSizeInBytes = kSkinnedVertexStride * vertexCount;

        const D3D12_RESOURCE_DESC outputDesc =
            ResourceFactory::MakeBufferDesc(outputSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

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
        skinCluster.outputUavHandle = UniqueDescriptor(*descriptorAllocator,
            descriptorAllocator->CreateUAV(skinCluster.outputVertexResource.Get(), outputUavDesc, "SkinCluster OutputVertexUAV"));

        // 水面の映り込みのヒットシェーディングが、変形後の頂点（VertexData の並び）を読むための SRV
        D3D12_SHADER_RESOURCE_VIEW_DESC outputRawSrvDesc{};
        outputRawSrvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        outputRawSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        outputRawSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        outputRawSrvDesc.Buffer.FirstElement = 0;
        outputRawSrvDesc.Buffer.NumElements = UINT(outputSizeInBytes / sizeof(uint32_t));
        outputRawSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        skinCluster.outputRawSrvHandle = UniqueDescriptor(*descriptorAllocator, descriptorAllocator->CreateSRV(
            skinCluster.outputVertexResource.Get(), outputRawSrvDesc, "SkinCluster OutputVertexRawSRV"));

        skinCluster.outputVertexBufferView.BufferLocation = skinCluster.outputVertexResource.GpuAddress();
        skinCluster.outputVertexBufferView.SizeInBytes = UINT(outputSizeInBytes);
        skinCluster.outputVertexBufferView.StrideInBytes = UINT(kSkinnedVertexStride);
    }

    // 前のフレームの姿勢の位置（CS が UAV で書き、頂点シェーダが StructuredBuffer<float4> で読む）
    {
        const D3D12_RESOURCE_DESC prevDesc = ResourceFactory::MakeBufferDesc(
            kPrevPositionStride * vertexCount, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        skinCluster.prevPositionResource.Reset(
            ResourceFactory::CreateTextureResource(device, prevDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        D3D12_UNORDERED_ACCESS_VIEW_DESC prevUavDesc{};
        prevUavDesc.Format = DXGI_FORMAT_UNKNOWN;
        prevUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        prevUavDesc.Buffer.FirstElement = 0;
        prevUavDesc.Buffer.NumElements = vertexCount;
        prevUavDesc.Buffer.StructureByteStride = UINT(kPrevPositionStride);
        prevUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        skinCluster.prevPositionUavHandle = UniqueDescriptor(*descriptorAllocator,
            descriptorAllocator->CreateUAV(skinCluster.prevPositionResource.Get(), prevUavDesc, "SkinCluster PrevPositionUAV"));

        D3D12_SHADER_RESOURCE_VIEW_DESC prevSrvDesc{};
        prevSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        prevSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        prevSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        prevSrvDesc.Buffer.FirstElement = 0;
        prevSrvDesc.Buffer.NumElements = vertexCount;
        prevSrvDesc.Buffer.StructureByteStride = UINT(kPrevPositionStride);
        prevSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        skinCluster.prevPositionSrvHandle = UniqueDescriptor(*descriptorAllocator,
            descriptorAllocator->CreateSRV(skinCluster.prevPositionResource.Get(), prevSrvDesc, "SkinCluster PrevPositionSRV"));
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
        skinCluster.skinningParamsCB = ResourceFactory::CreateStaticBuffer<SkinningParamsCB>(
            device, SkinningParamsCB{ vertexCount, { 0, 0, 0 } });
    }

    return skinCluster;
}

void SkinClusterGenerator::Update(SkinCluster& skinCluster, const Skeleton& skeleton)
{
    const std::span<WellForGPU> palette = skinCluster.palette.Write(UINT(skeleton.joints.size()));
    assert(palette.size() == skeleton.joints.size());

    for (size_t jointIndex = 0; jointIndex < palette.size(); ++jointIndex) {
        palette[jointIndex].skeletonSpaceMatrix =
            skinCluster.inverseBindPoseMatrices[jointIndex] * skeleton.joints[jointIndex].skeletonSpaceMatrix;
        palette[jointIndex].skeletonSpaceInverseTransposeMatrix =
            CoreEngine::MathCore::Matrix::Transpose(CoreEngine::MathCore::Matrix::Inverse(palette[jointIndex].skeletonSpaceMatrix));
    }

    // そのフレームで最初の更新なら、最後に更新した姿勢（前のフレームに描いた姿勢）を前の姿勢にする。
    // 最初の 1 回は前の姿勢が無いので今の姿勢を使う
    const uint64_t frame = Time::FrameCount();
    if (skinCluster.lastPoseFrame != frame) {
        if (skinCluster.lastPose.empty()) {
            skinCluster.prevPalette.Set(palette);
        } else {
            skinCluster.prevPalette.Set(skinCluster.lastPose);
        }
        skinCluster.lastPoseFrame = frame;
    }
    skinCluster.lastPose.assign(palette.begin(), palette.end());
    skinCluster.prevPoseStale = true;

    // パレットが更新されたので、次回描画前にGPUスキニング(CS)の再実行が必要
    skinCluster.needsGPUSkinning = true;
}

bool SkinClusterGenerator::SettlePrevPose(SkinCluster& skinCluster)
{
    if (!skinCluster.prevPoseStale || skinCluster.lastPose.empty() || skinCluster.lastPoseFrame == Time::FrameCount()) {
        return false;
    }
    skinCluster.prevPalette.Set(skinCluster.lastPose);
    skinCluster.prevPoseStale = false;
    skinCluster.needsGPUSkinning = true;
    return true;
}
}
