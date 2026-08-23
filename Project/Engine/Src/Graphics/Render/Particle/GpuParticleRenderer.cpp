#include "pch.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "GpuParticleRenderer.h"
#include "Graphics/Pipeline/ComputePipelineUtil.h"

#include "Particle/Gpu/GpuParticleSystem.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"

#include <cassert>
#include <stdexcept>

namespace CoreEngine
{
    int GpuParticleRenderer::ComputePass::GetRootParamIndex(const std::string& name) const
    {
        if (!reflectionData) {
            return -1;
        }
        return reflectionData->GetRootParameterIndexByName(name);
    }

    void GpuParticleRenderer::Initialize(ID3D12Device* device)
    {
        // グラフィックス側（Particle.VS/PS・PSO・共有頂点バッファ）は基底クラスをそのまま利用
        ParticleRenderer::Initialize(device);

        // Compute パイプラインの構築
        CreateComputePass(device, emitPass_, L"GpuParticleEmit.CS.hlsl", "GpuParticleEmit");
        CreateComputePass(device, updatePass_, L"GpuParticleUpdate.CS.hlsl", "GpuParticleUpdate");

        emitParticlesIdx_ = emitPass_.GetRootParamIndex("gParticles");
        emitCounterIdx_ = emitPass_.GetRootParamIndex("gCounters");
        emitFreeListIdx_ = emitPass_.GetRootParamIndex("gFreeList");
        emitParamsIdx_ = emitPass_.GetRootParamIndex("GpuParticleParams");

        updateParticlesIdx_ = updatePass_.GetRootParamIndex("gParticles");
        updateInstancingIdx_ = updatePass_.GetRootParamIndex("gInstancing");
        updateCounterIdx_ = updatePass_.GetRootParamIndex("gCounters");
        updateFreeListIdx_ = updatePass_.GetRootParamIndex("gFreeList");
        updateParamsIdx_ = updatePass_.GetRootParamIndex("GpuParticleParams");

        // ExecuteIndirect 用コマンドシグネチャ（描画引数のみ・ルートシグネチャ不要）
        D3D12_INDIRECT_ARGUMENT_DESC argDesc{};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc{};
        sigDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        HRESULT hr = device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&commandSignature_));
        if (FAILED(hr)) {
            throw std::runtime_error("GpuParticleRenderer: Failed to create command signature");
        }
    }

    void GpuParticleRenderer::CreateComputePass(ID3D12Device* device, ComputePass& pass, const wchar_t* shaderPath, const char* debugName)
    {
        IDxcBlob* csBlob = shaderCompiler_->CompileShader(shaderPath, L"cs_6_0");
        assert(csBlob != nullptr);

        pass.reflectionData = reflectionBuilder_->BuildFromComputeShader(csBlob, debugName);

        // SkinningComputeDispatcher と同じ構成: CBVはRootDescriptor、SRV/UAVはDescriptorTable
        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);

        auto buildResult = pass.rootSignatureMg->Build(device, *pass.reflectionData, config);
        if (!buildResult.success) {
            throw std::runtime_error(std::string("Failed to create RootSignature: ") + debugName + ": " + buildResult.errorMessage);
        }

        pass.pso = ComputePipelineUtil::Create(
            device, pass.rootSignatureMg->GetRootSignature(), csBlob,
            std::string("GpuParticle_") + debugName);
        if (!pass.pso) {
            throw std::runtime_error(std::string("Failed to create Compute PSO: ") + debugName);
        }
    }

    void GpuParticleRenderer::DrawGpu(GpuParticleSystem* system)
    {
        if (!cmdList_ || !system || !system->IsActive()) {
            return;
        }

        // 1. Emit / Update CS のディスパッチ
        DispatchCompute(system);

        // 2. Compute で PSO が置き換わっているため、グラフィックス状態を再設定
        //    （BeginPass がルートシグネチャ・PSO・トポロジ・頂点バッファを再バインドする）
        BeginPass(cmdList_, system->GetBlendMode());

        // 3. 描画リソースをバインドし、GPUが書き込んだ生存数で間接描画
        int instanceIdx = GetRootParamIndex("gParticle");
        if (instanceIdx >= 0) {
            cmdList_->SetGraphicsRootDescriptorTable(instanceIdx, system->GetInstancingSrvHandleGPU());
        }
        int texIdx = GetRootParamIndex("gTexture");
        if (texIdx >= 0) {
            cmdList_->SetGraphicsRootDescriptorTable(texIdx, system->GetTextureHandle());
        }

        cmdList_->ExecuteIndirect(commandSignature_.Get(), 1, system->GetArgsResource(), 0, nullptr, 0);
    }

    void GpuParticleRenderer::DispatchCompute(GpuParticleSystem* system)
    {
        const uint32_t emitCount = system->GetEmitCount();
        const bool reset = system->IsResetPending();

        // ── 1. 前準備: インスタンシング→UAV、カウンタ/間接引数→COPY_DEST ──
        {
            BarrierBatch batch(cmdList_);
            batch.Transition(system->Instancing(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            batch.Transition(system->Counter(), D3D12_RESOURCE_STATE_COPY_DEST);
            batch.Transition(system->Args(), D3D12_RESOURCE_STATE_COPY_DEST);
        }

        // 初回のみGPUバッファをコピーで初期化する
        // （CSのresetフラグ方式はCB1面の毎フレーム上書きと競合して実行されないことがあるため不使用）
        if (reset) {
            // 間接引数 {6, 0, 0, 0}
            cmdList_->CopyBufferRegion(system->GetArgsResource(), 0,
                system->GetUploadInitResource(), GpuParticleSystem::kInitArgsOffset,
                sizeof(uint32_t) * 4);
            // カウンタ {freeTop=kMaxParticles, alive=0, draw=0, 0}
            cmdList_->CopyBufferRegion(system->GetCounterResource(), 0,
                system->GetUploadInitResource(), GpuParticleSystem::kInitCountersOffset,
                sizeof(uint32_t) * GpuParticleSystem::kCounterCount);
            // フリーリスト {0, 1, ..., kMaxParticles-1}
            Barrier::Transition(cmdList_, system->FreeList(), D3D12_RESOURCE_STATE_COPY_DEST);
            cmdList_->CopyBufferRegion(system->GetFreeListResource(), 0,
                system->GetUploadInitResource(), GpuParticleSystem::kInitFreeListOffset,
                sizeof(uint32_t) * GpuParticleSystem::kMaxParticles);
            Barrier::Transition(cmdList_, system->FreeList(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        // drawCount（counter[2]）を毎フレーム0クリア（アップロードバッファの0領域をコピー元に使う）
        cmdList_->CopyBufferRegion(system->GetCounterResource(),
            sizeof(uint32_t) * GpuParticleSystem::kCounterDrawIndex,
            system->GetUploadInitResource(), sizeof(uint32_t), sizeof(uint32_t));

        Barrier::Transition(cmdList_, system->Counter(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        // ── 2. Emit CS（リセットフレームは放出しない） ──
        if (!reset && emitCount > 0) {
            cmdList_->SetComputeRootSignature(emitPass_.rootSignatureMg->GetRootSignature());
            cmdList_->SetPipelineState(emitPass_.pso.Get());

            if (emitParticlesIdx_ >= 0) {
                cmdList_->SetComputeRootDescriptorTable(emitParticlesIdx_, system->GetParticleUavHandleGPU());
            }
            if (emitCounterIdx_ >= 0) {
                cmdList_->SetComputeRootDescriptorTable(emitCounterIdx_, system->GetCounterUavHandleGPU());
            }
            if (emitFreeListIdx_ >= 0) {
                cmdList_->SetComputeRootDescriptorTable(emitFreeListIdx_, system->GetFreeListUavHandleGPU());
            }
            if (emitParamsIdx_ >= 0) {
                cmdList_->SetComputeRootConstantBufferView(emitParamsIdx_, system->GetParamsGPUAddress());
            }

            const UINT groupCount = (emitCount + 63) / 64;
            cmdList_->Dispatch(groupCount, 1, 1);

            // Emit の書き込み（粒子・カウンタ・フリーリスト）を Update から見えるようにする
            Barrier::UAVAll(cmdList_);
        }

        // ── 3. Update CS（全スロット更新 + 死亡回収 + コンパクション書き込み） ──
        cmdList_->SetComputeRootSignature(updatePass_.rootSignatureMg->GetRootSignature());
        cmdList_->SetPipelineState(updatePass_.pso.Get());

        if (updateParticlesIdx_ >= 0) {
            cmdList_->SetComputeRootDescriptorTable(updateParticlesIdx_, system->GetParticleUavHandleGPU());
        }
        if (updateInstancingIdx_ >= 0) {
            cmdList_->SetComputeRootDescriptorTable(updateInstancingIdx_, system->GetInstancingUavHandleGPU());
        }
        if (updateCounterIdx_ >= 0) {
            cmdList_->SetComputeRootDescriptorTable(updateCounterIdx_, system->GetCounterUavHandleGPU());
        }
        if (updateFreeListIdx_ >= 0) {
            cmdList_->SetComputeRootDescriptorTable(updateFreeListIdx_, system->GetFreeListUavHandleGPU());
        }
        if (updateParamsIdx_ >= 0) {
            cmdList_->SetComputeRootConstantBufferView(updateParamsIdx_, system->GetParamsGPUAddress());
        }

        const UINT groupCount = (GpuParticleSystem::kMaxParticles + 63) / 64;
        cmdList_->Dispatch(groupCount, 1, 1);

        // Update の書き込み完了待ち
        Barrier::UAVAll(cmdList_);

        // ── 4. カウンタのコピー: 間接引数の InstanceCount 更新 + 統計リードバック ──
        Barrier::Transition(cmdList_, system->Counter(), D3D12_RESOURCE_STATE_COPY_SOURCE);

        cmdList_->CopyBufferRegion(system->GetArgsResource(), sizeof(uint32_t) * 1, // InstanceCount
            system->GetCounterResource(), sizeof(uint32_t) * GpuParticleSystem::kCounterDrawIndex,
            sizeof(uint32_t));
        cmdList_->CopyBufferRegion(system->GetReadbackResource(), 0,
            system->GetCounterResource(), 0, sizeof(uint32_t) * GpuParticleSystem::kCounterCount);

        // ── 5. インスタンシングバッファを頂点シェーダーから読める状態へ ──
        {
            BarrierBatch batch(cmdList_);
            batch.Transition(system->Counter(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            batch.Transition(system->Args(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            batch.Transition(system->Instancing(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }

        if (reset) {
            system->ClearResetPending();
        }
    }
}
