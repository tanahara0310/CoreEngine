#include "BaseModelRenderer.h"
#include "Camera/ICamera.h"
#include "Graphics/Light/LightManager.h"
#include "Graphics/Shader/ShaderReflectionData.h"
#include "Utility/Logger/Logger.h"
#include <cstring>

namespace CoreEngine
{
    int BaseModelRenderer::GetRootParamIndex(const std::string& resourceName) const {
        // リフレクションデータが未構築の場合は無効値を返す
        if (!forwardReflectionData_) {
            return -1;
        }
        return forwardReflectionData_->GetRootParameterIndexByName(resourceName);
    }

    int BaseModelRenderer::GetGBufferRootParamIndex(const std::string& resourceName) const {
        // GBuffer 用リフレクションデータが未構築の場合は無効値を返す
        if (!gBufferReflectionData_) {
            return -1;
        }
        return gBufferReflectionData_->GetRootParameterIndexByName(resourceName);
    }

    void BaseModelRenderer::BindModelDrawPacket(
        ID3D12GraphicsCommandList* cmdList, const ModelDrawPacket& packet)
    {
        // 頂点バッファ（通常=スロット0のみ、スキニング=スロット0+1）
        cmdList->IASetVertexBuffers(0, packet.vertexBufferViewCount, packet.vertexBufferViews.data());
        cmdList->IASetIndexBuffer(&packet.indexBufferView);

        // パスに応じたルートパラメータインデックス取得ラムダ（Forward / GBuffer を自動選択）
        auto getIndex = [&](const std::string& name) -> int {
            return isInGBufferPass_
                ? GetGBufferRootParamIndex(name)
                : GetRootParamIndex(name);
        };

        // WVP 行列 CBV
        const int transformIdx = getIndex("gTransformationMatrix");
        if (transformIdx >= 0 && packet.transformCBV != 0) {
            cmdList->SetGraphicsRootConstantBufferView(transformIdx, packet.transformCBV);
        }

        // マテリアル CBV
        const int materialIdx = getIndex("gMaterial");
        if (materialIdx >= 0 && packet.materialCBV != 0) {
            cmdList->SetGraphicsRootConstantBufferView(materialIdx, packet.materialCBV);
        }

        // BaseColor テクスチャ
        const int textureIdx = getIndex("gTexture");
        if (textureIdx >= 0 && packet.baseColorSRV.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(textureIdx, packet.baseColorSRV);
        }

        // NormalMap
        const int normalIdx = getIndex("gNormalMap");
        if (normalIdx >= 0 && packet.normalMapSRV.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(normalIdx, packet.normalMapSRV);
        }

        // MetallicRoughness（Metallic と Roughness は同一テクスチャを使用）
        if (packet.metallicRoughnessSRV.ptr != 0) {
            const int metallicIdx = getIndex("gMetallicMap");
            const int roughnessIdx = getIndex("gRoughnessMap");
            if (metallicIdx >= 0) {
                cmdList->SetGraphicsRootDescriptorTable(metallicIdx, packet.metallicRoughnessSRV);
            }
            if (roughnessIdx >= 0) {
                cmdList->SetGraphicsRootDescriptorTable(roughnessIdx, packet.metallicRoughnessSRV);
            }
        }

        // Occlusion (AO)
        const int aoIdx = getIndex("gAOMap");
        if (aoIdx >= 0 && packet.occlusionSRV.ptr != 0) {
            cmdList->SetGraphicsRootDescriptorTable(aoIdx, packet.occlusionSRV);
        }

        // MatrixPalette（スキニング専用）
        if (packet.isSkinned) {
            const int matrixPaletteIdx = getIndex("gMatrixPalette");
            if (matrixPaletteIdx >= 0 && packet.matrixPaletteSRV.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(matrixPaletteIdx, packet.matrixPaletteSRV);
            }
        }

        // 描画コマンド発行
        cmdList->DrawIndexedInstanced(packet.indexCount, 1, packet.startIndex, 0, 0);
    }

    void BaseModelRenderer::BeginPass(ID3D12GraphicsCommandList* cmdList, BlendMode blendMode) {
        // GBuffer パスフラグを解除（Forward パスであることを示す）
        isInGBufferPass_ = false;

        // ブレンドモードが切り替わった場合のみ PSO を差し替える
        if (blendMode != currentBlendMode_) {
            currentBlendMode_ = blendMode;
            forwardPipelineState_ = forwardPsoMg_->GetPipelineState(blendMode);
        }

        // RootSignature・PSO・プリミティブトポロジーを設定
        cmdList->SetGraphicsRootSignature(forwardRootSignatureMg_->GetRootSignature());
        cmdList->SetPipelineState(forwardPipelineState_);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // カメラ定数バッファをバインド
        int cameraIdx = GetRootParamIndex("gCamera");
        if (cameraCBV_ != 0 && cameraIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(cameraIdx, cameraCBV_);
        }

        // ライト情報（種別ごとの配列）をバインド
        if (lightManager_) {
            lightManager_->SetLightsToCommandList(
                cmdList,
                GetRootParamIndex("gLightCounts"),
                GetRootParamIndex("gDirectionalLights"),
                GetRootParamIndex("gPointLights"),
                GetRootParamIndex("gSpotLights"),
                GetRootParamIndex("gAreaLights")
            );
        }

        // 環境マップ（Cube テクスチャ）をバインド
        int envMapIdx = GetRootParamIndex("gEnvironmentTexture");
        if (environmentMapHandle_.ptr != 0 && envMapIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(envMapIdx, environmentMapHandle_);
        }

        // ライトビュープロジェクション行列（シャドウ計算用）をバインド
        int lightVPIdx = GetRootParamIndex("gLightViewProjection");
        if (lightViewProjectionCBV_ != 0 && lightVPIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(lightVPIdx, lightViewProjectionCBV_);
        }

        // シャドウマップをバインド
        int shadowMapIdx = GetRootParamIndex("gShadowMap");
        if (shadowMapHandle_.ptr != 0 && shadowMapIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(shadowMapIdx, shadowMapHandle_);
        }

        // IBL: Irradiance Map をバインド
        int irradianceIdx = GetRootParamIndex("gIrradianceMap");
        if (irradianceMapHandle_.ptr != 0 && irradianceIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(irradianceIdx, irradianceMapHandle_);
        }

        // IBL: Prefiltered Environment Map をバインド
        int prefilteredIdx = GetRootParamIndex("gPrefilteredMap");
        if (prefilteredMapHandle_.ptr != 0 && prefilteredIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(prefilteredIdx, prefilteredMapHandle_);
        }

        // IBL: BRDF LUT をバインド
        int brdfLUTIdx = GetRootParamIndex("gBRDFLUT");
        if (brdfLUTHandle_.ptr != 0 && brdfLUTIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(brdfLUTIdx, brdfLUTHandle_);
        }

        // IBL 回転パラメータをバッファに書き込んでバインド
        if (iblParamsBuffer_) {
            IBLSceneParamsCPU params{ iblRotation_.x, iblRotation_.y, iblRotation_.z, 0.0f };
            void* mapped = nullptr;
            iblParamsBuffer_->Map(0, nullptr, &mapped);
            std::memcpy(mapped, &params, sizeof(params));
            iblParamsBuffer_->Unmap(0, nullptr);

            int iblParamsIdx = GetRootParamIndex("gIBLParams");
            if (iblParamsIdx >= 0) {
                cmdList->SetGraphicsRootConstantBufferView(iblParamsIdx, iblParamsCBVAddress_);
            }
        }
    }

    void BaseModelRenderer::BeginGBufferPass(ID3D12GraphicsCommandList* cmdList) {
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
        // パス終了時に GBuffer フラグをリセット
        isInGBufferPass_ = false;
    }

    void BaseModelRenderer::SetCamera(const ICamera* camera) {
        // カメラが有効な場合は GPU 仮想アドレスを保持し、無効時は 0 にリセット
        cameraCBV_ = camera ? camera->GetGPUVirtualAddress() : 0;
    }
}
