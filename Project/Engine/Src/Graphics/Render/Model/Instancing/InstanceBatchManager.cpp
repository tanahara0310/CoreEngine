#include "pch.h"
#include "InstanceBatchManager.h"

#include <cassert>
#include <cstring>

#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/Model/BaseModelRenderer.h"
#include "Graphics/Render/Model/ModelDrawPacket.h"
#include "Graphics/Model/ModelResource.h"
#include "Graphics/Model/TransformationMatrix.h"
#include "Graphics/Common/EngineStats.h"
#include "Graphics/Shader/ICustomShaderProvider.h"

namespace CoreEngine
{
    void InstanceBatchManager::Initialize(DirectXCommon* dxCommon, uint32_t frameCount,
        uint32_t maxInstancesPerFrame)
    {
        assert(dxCommon);
        assert(frameCount > 0);
        assert(maxInstancesPerFrame > 0);

        dxCommon_ = dxCommon;
        maxInstancesPerFrame_ = maxInstancesPerFrame;
        frameResources_.resize(frameCount);

        const size_t bufferSize = sizeof(TransformationMatrix) * maxInstancesPerFrame;
        for (auto& fr : frameResources_) {
            fr.uploadBuffer = ResourceFactory::CreateBufferResource(
                dxCommon_->GetDevice(), bufferSize);

            // 永続マッピング（Upload Heap なので問題ない）
            fr.uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&fr.mapped));
            fr.offset = 0;
        }
    }

    void InstanceBatchManager::Submit(const InstanceBatchKey& key,
        const TransformationMatrix& matrix,
        D3D12_GPU_VIRTUAL_ADDRESS materialCBV)
    {
        // フレーム切り替えチェック（自動）
        UpdateFrameIndex();

        auto it = batches_.find(key);
        if (it == batches_.end()) {
            InstanceBatch batch;
            batch.key = key;
            batch.materialCBV = materialCBV;
            batch.instances.reserve(16);
            batch.instances.push_back(matrix);
            batches_.emplace(key, std::move(batch));
        }
        else {
            it->second.instances.push_back(matrix);
        }
    }

    void InstanceBatchManager::UpdateFrameIndex()
    {
        // SwapChain の現在のバックバッファインデックスを取得
        const uint32_t newFrameIndex = dxCommon_->GetSwapChain()->GetCurrentBackBufferIndex();
        if (newFrameIndex != currentFrameIndex_) {
            // フレームが切り替わった場合、新しいフレームのリソースをリセット
            currentFrameIndex_ = newFrameIndex;
            frameResources_[currentFrameIndex_].offset = 0;
            batches_.clear();
        }
    }

    void InstanceBatchManager::Flush(ID3D12GraphicsCommandList* cmdList,
        BaseModelRenderer* renderer, bool isGBufferPass)
    {
        if (batches_.empty()) {
            return;
        }

        // 現在のパスに該当するバッチのみ描画する
        for (auto& [key, batch] : batches_) {
            if (key.isGBufferPass != isGBufferPass) {
                continue;
            }
            if (batch.instances.empty()) {
                continue;
            }
            DrawBatch(cmdList, renderer, batch);
            // バッチ数を統計に記録（DrawBatch 内の DrawIndexedInstanced で個々のインスタンスは集計済み）
            EngineStats::GetInstance().RecordBatch();
        }

        // パス内で消費したバッチは再描画されないよう削除
        for (auto it = batches_.begin(); it != batches_.end(); ) {
            if (it->first.isGBufferPass == isGBufferPass) {
                it = batches_.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS InstanceBatchManager::UploadInstanceData(
        const std::vector<TransformationMatrix>& instances)
    {
        FrameResource& fr = frameResources_[currentFrameIndex_];
        const uint32_t count = static_cast<uint32_t>(instances.size());
        assert(fr.offset + count <= maxInstancesPerFrame_
            && "InstanceBatchManager: maxInstancesPerFrame exceeded");

        TransformationMatrix* dst = fr.mapped + fr.offset;
        std::memcpy(dst, instances.data(), sizeof(TransformationMatrix) * count);

        const D3D12_GPU_VIRTUAL_ADDRESS gpuAddress =
            fr.uploadBuffer->GetGPUVirtualAddress()
            + sizeof(TransformationMatrix) * fr.offset;

        fr.offset += count;
        return gpuAddress;
    }

    void InstanceBatchManager::DrawBatch(ID3D12GraphicsCommandList* cmdList,
        BaseModelRenderer* renderer, const InstanceBatch& batch)
    {
        // インスタンス行列を Upload Heap にコピー
        const D3D12_GPU_VIRTUAL_ADDRESS instanceSRVAddress = UploadInstanceData(batch.instances);

        // 既存の ModelDrawPacket 経路を再利用してバインド
        const ModelResource* res = batch.key.resource;
        assert(res);

        ModelDrawPacket packet;
        packet.vertexBufferViews[0] = res->GetVertexBufferView();
        packet.vertexBufferViewCount = 1;
        packet.indexBufferView = res->GetIndexBufferView();

        const auto& subMeshes = res->GetSubMeshes();
        assert(batch.key.subMeshIndex < subMeshes.size());
        const auto& subMesh = subMeshes[batch.key.subMeshIndex];
        // LOD範囲を使用（範囲外のLODレベルは GetLod が最終段へクランプする）
        const SubMeshLodRange& lodRange = subMesh.GetLod(batch.key.lodIndex);
        packet.indexCount = lodRange.indexCount;
        packet.startIndex = lodRange.startIndex;

        packet.instanceDataSRV = instanceSRVAddress;
        packet.instanceCount = static_cast<UINT>(batch.instances.size());
        packet.materialCBV = batch.materialCBV;  // バッチから取得
        packet.baseColorSRV.ptr = batch.key.baseColorSRV;
        packet.normalMapSRV.ptr = batch.key.normalMapSRV;
        packet.metallicRoughnessSRV.ptr = batch.key.metallicRoughnessSRV;
        packet.occlusionSRV.ptr = batch.key.occlusionSRV;
        packet.emissiveSRV.ptr = batch.key.emissiveSRV;
        packet.isSkinned = false;

        // カスタム PSO が指定されているバッチは RS と PSO を差し替えて描画する
        if (batch.key.customForwardPSO) {
            if (batch.key.customRootSignature) {
                cmdList->SetGraphicsRootSignature(batch.key.customRootSignature);
            }
            cmdList->SetPipelineState(batch.key.customForwardPSO);

            // SetGraphicsRootSignature で全バインドがリセットされるため、
            // カスタム RS のインデックスでシーンリソースを再バインドする
            renderer->BindSceneResourcesWithCustomPipeline(cmdList, batch.key.customPipeline);
        }

        // カスタムリソース（WaterConstants など）を DrawCall より前にバインドする
        if (batch.key.customProvider) {
            batch.key.customProvider->BindCustomResources(cmdList, batch.key.customPipeline);
        }

        // customPipeline を渡してカスタム RootSignature のインデックスで正しくバインドする
        renderer->BindModelDrawPacket(cmdList, packet,
            batch.key.customForwardPSO ? batch.key.customPipeline : nullptr);

        // カスタム PSO を使った場合は既定 RS と PSO に戻す（次バッチのため）
        if (batch.key.customForwardPSO) {
            renderer->RestoreDefaultPSO(cmdList);
        }
    }
}
