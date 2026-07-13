#include "pch.h"
#include "BaseModelRenderer.h"
#include "Camera/ICamera.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Graphics/Pipeline/CustomShaderPipeline.h"
#include "Graphics/Render/Model/Instancing/InstanceBatchManager.h"
#include "Graphics/Common/EngineStats.h"
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
        forwardCache_.metallicRoughnessMap = GetRootParamIndex("gMetallicRoughnessMap");
        forwardCache_.emissiveMap = GetRootParamIndex("gEmissiveMap");
        forwardCache_.aoMap = GetRootParamIndex("gAOMap");
        forwardCache_.matrixPalette = GetRootParamIndex("gMatrixPalette");

        gBufferCache_.transform = GetGBufferRootParamIndex("gTransformationMatrix");
        gBufferCache_.instanceData = GetGBufferRootParamIndex("gInstanceData");
        gBufferCache_.material = GetGBufferRootParamIndex("gMaterial");
        gBufferCache_.texture = GetGBufferRootParamIndex("gTexture");
        gBufferCache_.normalMap = GetGBufferRootParamIndex("gNormalMap");
        gBufferCache_.metallicRoughnessMap = GetGBufferRootParamIndex("gMetallicRoughnessMap");
        gBufferCache_.emissiveMap = GetGBufferRootParamIndex("gEmissiveMap");
        gBufferCache_.aoMap = GetGBufferRootParamIndex("gAOMap");
        gBufferCache_.matrixPalette = GetGBufferRootParamIndex("gMatrixPalette");
    }

    void BaseModelRenderer::BindModelDrawPacket(
        ID3D12GraphicsCommandList* cmdList, const ModelDrawPacket& packet,
        const CustomShaderPipeline* customPipeline)
    {
        cmdList->IASetVertexBuffers(0, packet.vertexBufferViewCount, packet.vertexBufferViews.data());
        cmdList->IASetIndexBuffer(&packet.indexBufferView);

        // カスタム RootSignature 使用時はそのリフレクションからインデックスを解決するヘルパーラムダ
        auto resolveIdx = [&](const std::string& name, int defaultIdx) -> int {
            if (customPipeline) {
                return customPipeline->GetRootParamIndex(name);
            }
            return defaultIdx;
        };

        const CachedIndices& c = isInGBufferPass_ ? gBufferCache_ : forwardCache_;

        // インスタンシング: 通常モデルは Root SRV、スキニングモデルは従来 CBV を使用
        if (!packet.isSkinned) {
            int idx = resolveIdx("gInstanceData", c.instanceData);
            if (idx >= 0 && packet.instanceDataSRV != 0) {
                cmdList->SetGraphicsRootShaderResourceView(idx, packet.instanceDataSRV);
            }
        }
        else {
            int idx = resolveIdx("gTransformationMatrix", c.transform);
            if (idx >= 0 && packet.instanceDataSRV != 0) {
                cmdList->SetGraphicsRootConstantBufferView(idx, packet.instanceDataSRV);
            }
        }
        {
            int idx = resolveIdx("gMaterial", c.material);
            if (idx >= 0 && packet.materialCBV != 0) {
                cmdList->SetGraphicsRootConstantBufferView(idx, packet.materialCBV);
            }
        }
        {
            int idx = resolveIdx("gTexture", c.texture);
            if (idx >= 0 && packet.baseColorSRV.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(idx, packet.baseColorSRV);
            }
        }
        {
            int idx = resolveIdx("gNormalMap", c.normalMap);
            if (idx >= 0 && packet.normalMapSRV.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(idx, packet.normalMapSRV);
            }
        }
        {
            int idx = resolveIdx("gMetallicRoughnessMap", c.metallicRoughnessMap);
            if (idx >= 0 && packet.metallicRoughnessSRV.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(idx, packet.metallicRoughnessSRV);
            }
        }
        {
            int idx = resolveIdx("gEmissiveMap", c.emissiveMap);
            if (idx >= 0 && packet.emissiveSRV.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(idx, packet.emissiveSRV);
            }
        }
        {
            int idx = resolveIdx("gAOMap", c.aoMap);
            if (idx >= 0 && packet.occlusionSRV.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(idx, packet.occlusionSRV);
            }
        }
        if (packet.isSkinned) {
            int idx = resolveIdx("gMatrixPalette", c.matrixPalette);
            if (idx >= 0 && packet.matrixPaletteSRV.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(idx, packet.matrixPaletteSRV);
            }
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
        if (iblParamsCBVAddress_ != 0 && forwardCache_.iblParams >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(forwardCache_.iblParams, iblParamsCBVAddress_);
        }
    }

    void BaseModelRenderer::BindSceneResourcesWithCustomPipeline(
        ID3D12GraphicsCommandList* cmdList,
        const CustomShaderPipeline* customPipeline)
    {
        if (!customPipeline) {
            return;
        }

        // カスタム RS のインデックスでシーンレベルのリソースを再バインドする
        auto bind = [&](const std::string& name, auto bindFn) {
            int idx = customPipeline->GetRootParamIndex(name);
            if (idx >= 0) {
                bindFn(idx);
            }
        };

        bind("gCamera", [&](int i) {
            if (cameraCBV_ != 0) {
                cmdList->SetGraphicsRootConstantBufferView(i, cameraCBV_);
            }
        });
        if (lightManager_) {
            int lightCounts    = customPipeline->GetRootParamIndex("gLightCounts");
            int dirLights      = customPipeline->GetRootParamIndex("gDirectionalLights");
            int pointLights    = customPipeline->GetRootParamIndex("gPointLights");
            int spotLights     = customPipeline->GetRootParamIndex("gSpotLights");
            int areaLights     = customPipeline->GetRootParamIndex("gAreaLights");
            lightManager_->SetLightsToCommandList(
                cmdList, lightCounts, dirLights, pointLights, spotLights, areaLights);
        }
        bind("gLightViewProjection", [&](int i) {
            if (lightViewProjectionCBV_ != 0) {
                cmdList->SetGraphicsRootConstantBufferView(i, lightViewProjectionCBV_);
            }
        });
        bind("gShadowMap", [&](int i) {
            if (shadowMapHandle_.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(i, shadowMapHandle_);
            }
        });
        bind("gIrradianceMap", [&](int i) {
            if (iblParams_.irradianceMap.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(i, iblParams_.irradianceMap);
            }
        });
        bind("gPrefilteredMap", [&](int i) {
            if (iblParams_.prefilteredMap.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(i, iblParams_.prefilteredMap);
            }
        });
        bind("gBRDFLUT", [&](int i) {
            if (iblParams_.brdfLUT.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(i, iblParams_.brdfLUT);
            }
        });
        bind("gIBLParams", [&](int i) {
            if (iblParamsCBVAddress_ != 0) {
                cmdList->SetGraphicsRootConstantBufferView(i, iblParamsCBVAddress_);
            }
        });
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
