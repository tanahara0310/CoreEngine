#include "BaseModelRenderer.h"
#include "Camera/ICamera.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/Render/Model/Instancing/InstanceBatchManager.h"
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

    void BaseModelRenderer::CacheRootParamIndices()
    {
        forwardCache_.camera = GetRootParamIndex("gCamera");
        forwardCache_.lightCounts = GetRootParamIndex("gLightCounts");
        forwardCache_.directionalLights = GetRootParamIndex("gDirectionalLights");
        forwardCache_.pointLights = GetRootParamIndex("gPointLights");
        forwardCache_.spotLights = GetRootParamIndex("gSpotLights");
        forwardCache_.areaLights = GetRootParamIndex("gAreaLights");
        forwardCache_.envTexture = GetRootParamIndex("gEnvironmentTexture");
        forwardCache_.lightVP = GetRootParamIndex("gLightViewProjection");
        forwardCache_.shadowMap = GetRootParamIndex("gShadowMap");
        forwardCache_.irradianceMap = GetRootParamIndex("gIrradianceMap");
        forwardCache_.prefilteredMap = GetRootParamIndex("gPrefilteredMap");
        forwardCache_.brdfLUT = GetRootParamIndex("gBRDFLUT");
        forwardCache_.iblParams = GetRootParamIndex("gIBLParams");
        forwardCache_.transform = GetRootParamIndex("gTransformationMatrix");
        forwardCache_.instanceData = GetRootParamIndex("gInstanceData");
        forwardCache_.material = GetRootParamIndex("gMaterial");
        forwardCache_.texture = GetRootParamIndex("gTexture");
        forwardCache_.normalMap = GetRootParamIndex("gNormalMap");
        forwardCache_.metallicMap = GetRootParamIndex("gMetallicMap");
        forwardCache_.roughnessMap = GetRootParamIndex("gRoughnessMap");
        forwardCache_.aoMap = GetRootParamIndex("gAOMap");
        forwardCache_.matrixPalette = GetRootParamIndex("gMatrixPalette");

        gBufferCache_.transform = GetGBufferRootParamIndex("gTransformationMatrix");
        gBufferCache_.instanceData = GetGBufferRootParamIndex("gInstanceData");
        gBufferCache_.material = GetGBufferRootParamIndex("gMaterial");
        gBufferCache_.texture = GetGBufferRootParamIndex("gTexture");
        gBufferCache_.normalMap = GetGBufferRootParamIndex("gNormalMap");
        gBufferCache_.metallicMap = GetGBufferRootParamIndex("gMetallicMap");
        gBufferCache_.roughnessMap = GetGBufferRootParamIndex("gRoughnessMap");
        gBufferCache_.aoMap = GetGBufferRootParamIndex("gAOMap");
        gBufferCache_.matrixPalette = GetGBufferRootParamIndex("gMatrixPalette");
    }

    void BaseModelRenderer::BindModelDrawPacket(
        ID3D12GraphicsCommandList* cmdList, const ModelDrawPacket& packet)
    {
        cmdList->IASetVertexBuffers(0, packet.vertexBufferViewCount, packet.vertexBufferViews.data());
        cmdList->IASetIndexBuffer(&packet.indexBufferView);

        const CachedIndices& c = isInGBufferPass_ ? gBufferCache_ : forwardCache_;

        // インスタンシング: 通常モデルは Root SRV、スキニングモデルは従来 CBV を使用
        if (!packet.isSkinned) {
            if (c.instanceData >= 0 && packet.instanceDataSRV != 0) {
                cmdList->SetGraphicsRootShaderResourceView(c.instanceData, packet.instanceDataSRV);
            }
        }
        else {
            if (c.transform >= 0 && packet.instanceDataSRV != 0) {
                cmdList->SetGraphicsRootConstantBufferView(c.transform, packet.instanceDataSRV);
            }
        }
        if (c.material >= 0 && packet.materialCBV != 0) {
            cmdList->SetGraphicsRootConstantBufferView(c.material, packet.materialCBV);
        }
        if (c.texture >= 0 && packet.baseColorSRV.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(c.texture, packet.baseColorSRV);
        }
        if (c.normalMap >= 0 && packet.normalMapSRV.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(c.normalMap, packet.normalMapSRV);
        }
        if (packet.metallicRoughnessSRV.ptr != 0) {
            if (c.metallicMap >= 0) {
                cmdList->SetGraphicsRootDescriptorTable(c.metallicMap, packet.metallicRoughnessSRV);
            }
            if (c.roughnessMap >= 0) {
                cmdList->SetGraphicsRootDescriptorTable(c.roughnessMap, packet.metallicRoughnessSRV);
            }
        }
        if (c.aoMap >= 0 && packet.occlusionSRV.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(c.aoMap, packet.occlusionSRV);
        }
        if (packet.isSkinned && c.matrixPalette >= 0 && packet.matrixPaletteSRV.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(c.matrixPalette, packet.matrixPaletteSRV);
        }
        cmdList->DrawIndexedInstanced(packet.indexCount, packet.instanceCount, packet.startIndex, 0, 0);
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

        if (cameraCBV_ != 0 && forwardCache_.camera >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(forwardCache_.camera, cameraCBV_);
        }
        if (lightManager_) {
            lightManager_->SetLightsToCommandList(
                cmdList,
                forwardCache_.lightCounts,
                forwardCache_.directionalLights,
                forwardCache_.pointLights,
                forwardCache_.spotLights,
                forwardCache_.areaLights
            );
        }
        if (iblParams_.environmentMap.ptr != 0 && forwardCache_.envTexture >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(forwardCache_.envTexture, iblParams_.environmentMap);
        }
        if (lightViewProjectionCBV_ != 0 && forwardCache_.lightVP >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(forwardCache_.lightVP, lightViewProjectionCBV_);
        }
        if (shadowMapHandle_.ptr != 0 && forwardCache_.shadowMap >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(forwardCache_.shadowMap, shadowMapHandle_);
        }
        if (iblParams_.irradianceMap.ptr != 0 && forwardCache_.irradianceMap >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(forwardCache_.irradianceMap, iblParams_.irradianceMap);
        }
        if (iblParams_.prefilteredMap.ptr != 0 && forwardCache_.prefilteredMap >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(forwardCache_.prefilteredMap, iblParams_.prefilteredMap);
        }
        if (iblParams_.brdfLUT.ptr != 0 && forwardCache_.brdfLUT >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(forwardCache_.brdfLUT, iblParams_.brdfLUT);
        }
        if (iblParamsBuffer_) {
            IBLSceneParamsCPU params{ iblParams_.rotation.x, iblParams_.rotation.y, iblParams_.rotation.z, iblParams_.intensity };
            void* mapped = nullptr;
            iblParamsBuffer_->Map(0, nullptr, &mapped);
            std::memcpy(mapped, &params, sizeof(params));
            iblParamsBuffer_->Unmap(0, nullptr);
            if (forwardCache_.iblParams >= 0) {
                cmdList->SetGraphicsRootConstantBufferView(forwardCache_.iblParams, iblParamsCBVAddress_);
            }
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

#ifdef _DEBUG
        // デバッグ時: gTexture が GBuffer シェーダーに存在するか検証
        int textureIdx = GetGBufferRootParamIndex("gTexture");
        if (textureIdx < 0) {
            Logger::GetInstance().Logf(
                LogLevel::Warn,
                LogCategory::Graphics,
                "BaseModelRenderer::BeginGBufferPass() could not find gTexture in GBuffer root signature.");
        }
#endif
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

    void BaseModelRenderer::SetCamera(const ICamera* camera) {
        // カメラが有効な場合は GPU 仮想アドレスを保持し、無効時は 0 にリセット
        cameraCBV_ = camera ? camera->GetGPUVirtualAddress() : 0;
    }
}
