#include "pch.h"
#include "BaseModelRenderer.h"
#include "Camera/Camera.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/RootSignature/ShaderBinder.h"
#include "Graphics/Render/Model/Instancing/InstanceBatchManager.h"
#include "Diagnostics/EngineStats.h"
#include "Utility/Logger/Logger.h"
#include <cstring>

namespace CoreEngine
{
    void BaseModelRenderer::SetIBLParameters(const IBLParameters& params) {
        iblParams_ = params;
    }

    int BaseModelRenderer::GetRootParamIndex(const std::string& resourceName) const {
        // リフレクションデータが未構築の場合は無効値を返す
        if (!forwardReflectionData_) {
            return -1;
        }
        return forwardReflectionData_->GetRootParameterIndexByName(resourceName);
    }

    int BaseModelRenderer::GetGBufferRootParamIndex(const std::string& resourceName) const {
        if (!gBufferReflectionData_) {
            return -1;
        }
        return gBufferReflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void BaseModelRenderer::ResolveBindings(
        const ShaderBindingDecl* forwardDecls,
        const ShaderBindingDecl* gBufferDecls,
        size_t count,
        const std::string& debugName)
    {
        assert(forwardReflectionData_ && gBufferReflectionData_ && "RootSignature 構築後に呼ぶこと");

        // 宣言表とシェーダー実体を突き合わせる（改名・削除・種別違いはここで throw される）
        forwardBindings_ = BindingTable::Resolve(
            *forwardReflectionData_, forwardDecls, count, debugName);
        gBufferBindings_ = BindingTable::Resolve(
            *gBufferReflectionData_, gBufferDecls, count, debugName + "_GBuffer");
    }

    void BaseModelRenderer::BindModelDrawPacket(
        ID3D12GraphicsCommandList* cmdList, const ModelDrawPacket& packet,
        const CustomShaderPipeline* customPipeline)
    {
        cmdList->IASetVertexBuffers(0, packet.vertexBufferViewCount, packet.vertexBufferViews.data());
        cmdList->IASetIndexBuffer(&packet.indexBufferView);

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);

        // カスタム RootSignature 使用時はそのシェーダー側のスロットを使う
        const BindingTable& table = customPipeline
            ? customPipeline->GetModelBindings()
            : (isInGBufferPass_ ? gBufferBindings_ : forwardBindings_);
        auto slotOf = [&table](ModelBind::Slot slot) -> RootSlot { return table[slot]; };

        // インスタンシング: 通常モデルは Root SRV、スキニングモデルは従来 CBV を使用
        // どちらを呼ぶかは RootSlot の種別から ShaderBinder が決める
        if (packet.instanceDataSRV != 0) {
            binder.Set(slotOf(packet.isSkinned ? ModelBind::gTransformationMatrix
                                               : ModelBind::gInstanceData),
                       packet.instanceDataSRV);
        }
        if (packet.materialCBV != 0) {
            binder.Set(slotOf(ModelBind::gMaterial), packet.materialCBV);
        }
        if (packet.baseColorSRV.ptr != 0) {
            binder.Set(slotOf(ModelBind::gTexture), packet.baseColorSRV);
        }
        if (packet.normalMapSRV.ptr != 0) {
            binder.Set(slotOf(ModelBind::gNormalMap), packet.normalMapSRV);
        }
        if (packet.metallicRoughnessSRV.ptr != 0) {
            binder.Set(slotOf(ModelBind::gMetallicRoughnessMap), packet.metallicRoughnessSRV);
        }
        if (packet.emissiveSRV.ptr != 0) {
            binder.Set(slotOf(ModelBind::gEmissiveMap), packet.emissiveSRV);
        }
        if (packet.occlusionSRV.ptr != 0) {
            binder.Set(slotOf(ModelBind::gAOMap), packet.occlusionSRV);
        }
        if (packet.isSkinned && packet.matrixPaletteSRV.ptr != 0) {
            binder.Set(slotOf(ModelBind::gMatrixPalette), packet.matrixPaletteSRV);
        }
        cmdList->DrawIndexedInstanced(packet.indexCount, packet.instanceCount, packet.startIndex, 0, 0);

        // 統計情報を記録（インスタンシング判定 + 三角形数集計）
        const bool isInstanced = packet.instanceCount > 1;
        EngineStats::GetInstance().RecordDrawCall(
            isInstanced, packet.instanceCount, packet.indexCount, /*vertexCountPerInstance=*/0);
    }

    void BaseModelRenderer::RestoreDefaultPSO(ID3D12GraphicsCommandList* cmdList)
    {
        // カスタムシェーダーが独自 RootSignature を使用した場合に備えて RS も既定に戻す
        cmdList->SetGraphicsRootSignature(forwardRootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(forwardPipelineState_);

        // 標準 RS に戻したので、BeginPass で設定したシーンリソースを再バインドする
        // （SetGraphicsRootSignature を呼ぶと全バインドがリセットされるため）
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
        BindForwardSceneResources(binder, forwardBindings_);
    }

    void BaseModelRenderer::BindForwardSceneResources(
        ShaderBinder& binder, const BindingTable& table)
    {
        if (cameraCBV_ != 0) {
            binder.Set(table[ModelBind::gCamera], cameraCBV_);
        }
        if (lightManager_) {
            lightManager_->SetLightsToCommandList(
                binder,
                table[ModelBind::gLightCounts],
                table[ModelBind::gDirectionalLights],
                table[ModelBind::gPointLights],
                table[ModelBind::gSpotLights],
                table[ModelBind::gAreaLights]);
        }
        if (rtShadowMaskHandle_.ptr != 0) {
            binder.Set(table[ModelBind::gRTShadowMask], rtShadowMaskHandle_);
        }
        if (iblParams_.irradianceMap.ptr != 0) {
            binder.Set(table[ModelBind::gIrradianceMap], iblParams_.irradianceMap);
        }
        if (iblParams_.prefilteredMap.ptr != 0) {
            binder.Set(table[ModelBind::gPrefilteredMap], iblParams_.prefilteredMap);
        }
        if (iblParams_.brdfLUT.ptr != 0) {
            binder.Set(table[ModelBind::gBRDFLUT], iblParams_.brdfLUT);
        }
        if (iblParamsCBVAddress_ != 0) {
            binder.Set(table[ModelBind::gIBLParams], iblParamsCBVAddress_);
        }
    }

    void BaseModelRenderer::BindSceneResourcesWithCustomPipeline(
        ID3D12GraphicsCommandList* cmdList,
        const CustomShaderPipeline* customPipeline)
    {
        if (!customPipeline) {
            return;
        }

        // カスタムシェーダー側の解決済み表でシーンレベルのリソースを再バインドする
        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
        BindForwardSceneResources(binder, customPipeline->GetModelBindings());
    }

    void BaseModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        currentCommandList_ = cmdList;
        isInGBufferPass_ = false;

        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            forwardPipelineState_ = forwardPsoMg_->GetPipelineState(blendMode);
        }

        cmdList->SetGraphicsRootSignature(forwardRootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(forwardPipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ShaderBinder binder(cmdList, ShaderBinder::Pipeline::Graphics);
        BindForwardSceneResources(binder, forwardBindings_);

        if (iblParams_.environmentMap.ptr != 0) {
            binder.Set(forwardBindings_[ModelBind::gEnvironmentTexture], iblParams_.environmentMap);
        }
        if (iblParamsBuffer_) {
            IBLSceneParamsCPU params{};
            params.rotationX = iblParams_.rotation.x;
            params.rotationY = iblParams_.rotation.y;
            params.rotationZ = iblParams_.rotation.z;
            params.environmentIntensity = iblParams_.intensity;
            // IBL の有効/無効はシーン側（マップが揃っているか）で決まる
            params.sceneIBLEnabled = HasIBLMaps() ? 1u : 0u;
            void* mapped = nullptr;
            iblParamsBuffer_->Map(0, nullptr, &mapped);
            std::memcpy(mapped, &params, sizeof(params));
            iblParamsBuffer_->Unmap(0, nullptr);
            binder.Set(forwardBindings_[ModelBind::gIBLParams], iblParamsCBVAddress_);
        }
    }
    void BaseModelRenderer::BeginGBufferPass(ID3D12GraphicsCommandList* cmdList) {
        currentCommandList_ = cmdList;
        // GBuffer パス中であることを記録
        isInGBufferPass_ = true;

        // GBuffer 用 RootSignature・PSO・プリミティブトポロジーを設定
        cmdList->SetGraphicsRootSignature(gBufferRootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(gBufferPipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // gTexture の存在検証は起動時の契約照合（kGBuffer で Required 宣言）が行う
    }

    void BaseModelRenderer::EndPass() {
        // インスタンシングバッチをフラッシュ（溜まった描画を実行）
        if (instanceBatchManager_ && currentCommandList_) {
            instanceBatchManager_->Flush(currentCommandList_, this, isInGBufferPass_);
        }

        // パス終了時に GBuffer フラグをリセット
        isInGBufferPass_ = false;
        currentCommandList_ = nullptr;
    }

    void BaseModelRenderer::SetCamera(const Camera* camera) {
        // カメラが有効な場合は GPU 仮想アドレスを保持し、無効時は 0 にリセット
        cameraCBV_ = camera ? camera->GetGPUVirtualAddress() : 0;
    }
}
