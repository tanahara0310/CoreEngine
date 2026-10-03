#include "pch.h"
#include "Utility/CVar/CVar.h"
#include "WaterCausticsTechnique.h"

#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Camera/View/ViewInfo.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include <cstring>

#ifdef CORE_EDITOR
#include "Editor/ImGui/ImguiManager.h"
#endif

namespace CoreEngine
{
    namespace
    {
        CVar<bool> cvEnabled{
            "r.WaterCaustics.Enabled", true,
            "水面コースティクスの適用を有効にする",
            CVarRange{}, CVarFlags::NoUI };
    }

    // 水面の法線から屈折方向を求め、集光の度合いをスクリーンスペースで求める。
    // 深度からワールド座標を復元するので、行列は必ず G-Buffer を描いたビューのものを使う
    void WaterCausticsTechnique::Execute(const RenderContext& context, D3D12_GPU_DESCRIPTOR_HANDLE& outputSrvHandle)
    {
        outputSrvHandle = {};
        if (!IsEnabled() || !context.gBufferManager || !context.renderTargetManager || !context.dxCommon) {
            return;
        }

        auto* renderTargetManager = context.renderTargetManager;
        auto* gBufferManager = context.gBufferManager;
        auto* cmdList = context.cmdList;
        if (!cmdList) {
            return;
        }

        RenderTarget* target = renderTargetManager->GetRenderTarget(targetName_);
        if (!target) {
            RenderTargetDescriptor desc(targetName_);
            desc.needsDepthStencil = false;
            desc.clearColor[0] = 0.0f;
            desc.clearColor[1] = 0.0f;
            desc.clearColor[2] = 0.0f;
            desc.clearColor[3] = 1.0f;
            target = renderTargetManager->CreateRenderTarget(desc);
        }
        if (!target) {
            return;
        }

        // 深度復元用 View*Projection 逆行列（実行中のビューの ViewInfo から取る）
        if (context.frameViews) {
            const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
            if (view.isValid) {
                std::memcpy(params_.invViewProjMatrix, &view.invViewProjection, sizeof(float) * 16);
            }
        }
        diagnostics_.normalHandle = gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness).ptr;

        // 定数は毎フレーム UploadRing に置く
        UploadRing& uploadRing = context.dxCommon->GetUploadRing();
        const D3D12_GPU_VIRTUAL_ADDRESS mainLightAddress =
            uploadRing.AllocateConstants(BuildMainLightConstants(context.lightManager));
        const D3D12_GPU_VIRTUAL_ADDRESS waterSurfaceAddress =
            uploadRing.AllocateConstants(BuildWaterSurfaceConstants(context.waterSurfaceState));
        const D3D12_GPU_VIRTUAL_ADDRESS paramsAddress = uploadRing.AllocateConstants(params_);

        target->Begin(cmdList);

        cmdList->SetGraphicsRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(pipelineStateManager_.GetPipelineState(BlendMode::kBlendModeNone));

        // WorldPosition ターゲット廃止に伴い、深度から復元する
        const int sceneDepthIdx = GetRootParamIndex("gSceneDepth");
        if (sceneDepthIdx >= 0 && context.frameBlackboard) {
            D3D12_GPU_DESCRIPTOR_HANDLE depthHandle{};
            if (context.frameBlackboard->TryGetSrvHandle(FrameBlackboard::SceneDepth, depthHandle)) {
                cmdList->SetGraphicsRootDescriptorTable(sceneDepthIdx, depthHandle);
            }
        }

        const int normalIdx = GetRootParamIndex("gNormalRoughness");
        if (normalIdx >= 0) {
            cmdList->SetGraphicsRootDescriptorTable(normalIdx,
                gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness));
        }

        const int mainLightIdx = GetRootParamIndex("gMainLight");
        if (mainLightIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(mainLightIdx, mainLightAddress);
        }

        const int waterSurfaceIdx = GetRootParamIndex("gWaterSurfaceData");
        if (waterSurfaceIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(waterSurfaceIdx, waterSurfaceAddress);
        }

        const int paramsIdx = GetRootParamIndex("WaterCausticsParams");
        if (paramsIdx >= 0) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx, paramsAddress);
        }

        DrawFullscreenQuad(cmdList);

        target->End(cmdList);

        outputSrvHandle = target->GetSRVHandle();
        diagnostics_.outputHandle = outputSrvHandle.ptr;

        if (params_.debugLogEnabled != 0) {
            Logger::GetInstance().Infof(
                LogCategory::Graphics,
                LogSubCategory::Pipeline,
                "WaterCausticsTechnique: output=0x{:X} normal=0x{:X} activeWaveCount={} mainLightEnabled={} debugViewMode={} debugScale={:.2f}",
                diagnostics_.outputHandle,
                diagnostics_.normalHandle,
                diagnostics_.activeWaveCount,
                diagnostics_.mainLightEnabled,
                params_.debugViewMode,
                params_.debugDisplayScale);
        }
    }

    void WaterCausticsTechnique::DrawImGui()
    {
#ifdef CORE_EDITOR
        ImGui::PushID("WaterCausticsTechnique");
        UI::SliderFloat("Intensity", params_.intensity, 0.0f, 8.0f);
        UI::SliderFloat("Depth Attenuation", params_.depthAttenuation, 0.0f, 2.0f);
        UI::SliderFloat("Curvature Scale", params_.curvatureScale, 0.0f, 30.0f);
        UI::SliderFloat("Surface Sample Radius", params_.surfaceSampleRadius, 0.05f, 2.0f);
        UI::SliderFloat("Refractive Index", params_.refractiveIndex, 1.0f, 1.6f);
        UI::SliderFloat("Receiver Normal Strength", params_.receiverNormalStrength, 0.0f, 2.0f);
        UI::SliderFloat("Alignment Power", params_.alignmentPower, 1.0f, 64.0f);
        UI::SliderFloat("Debug Display Scale", params_.debugDisplayScale, 0.1f, 16.0f);
        int debugViewMode = static_cast<int>(params_.debugViewMode);
        if (ImGui::SliderInt("Debug View Mode", &debugViewMode, 0, 2)) {
            params_.debugViewMode = static_cast<uint32_t>(debugViewMode);
        }
        bool debugLogEnabled = (params_.debugLogEnabled != 0);
        if (ImGui::Checkbox("Debug Log Enabled", &debugLogEnabled)) {
            params_.debugLogEnabled = debugLogEnabled ? 1u : 0u;
        }
        ImGui::Text("Diag: waves=%u light=%u out=0x%llX", diagnostics_.activeWaveCount, diagnostics_.mainLightEnabled, diagnostics_.outputHandle);
        ImGui::PopID();
#endif
    }

    const std::wstring& WaterCausticsTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"WaterCaustics.PS.hlsl";
        return path;
    }

    WaterCausticsTechnique::MainLightConstants WaterCausticsTechnique::BuildMainLightConstants(LightManager* lightManager)
    {
        MainLightConstants constants{};
        if (lightManager) {
            if (const Light* light = lightManager->GetDirectionalLight(0); light && light->enabled) {
                // 大気透過率適用済みの実効色（DeferredLighting へ転送される色と同じ）。
                // 太陽の見た目とコースティクスの色・明るさを日没時も一致させる
                const Vector3 effectiveColor = lightManager->GetEffectiveLightColorRGB(*light);
                constants.color[0] = effectiveColor.x;
                constants.color[1] = effectiveColor.y;
                constants.color[2] = effectiveColor.z;
                // オーサリングは照度 [lx]。シェーダーが期待する従来単位へ換算する
                constants.intensity = LightUnits::LuxToShader(light->intensity);
                constants.direction[0] = light->direction.x;
                constants.direction[1] = light->direction.y;
                constants.direction[2] = light->direction.z;
                constants.enabled = 1;
            }
        }

        diagnostics_.mainLightEnabled = constants.enabled;
        return constants;
    }

    WaterCausticsTechnique::WaterSurfaceConstants WaterCausticsTechnique::BuildWaterSurfaceConstants(
        const WaterSurfaceData* surfaceData)
    {
        WaterSurfaceConstants surfaceConstants{};
        if (surfaceData) {
            surfaceConstants.waterHeight = surfaceData->waterHeight;
            surfaceConstants.activeWaveCount = (std::min)(surfaceData->activeWaveCount, kMaxWaterSurfaceWaveCount);
            surfaceConstants.time = surfaceData->time;
            for (uint32_t waveIndex = 0; waveIndex < surfaceConstants.activeWaveCount; ++waveIndex) {
                surfaceConstants.waves[waveIndex] = surfaceData->waves[waveIndex];
            }
            surfaceConstants.regionCenterXZ[0] = surfaceData->regionCenterXZ[0];
            surfaceConstants.regionCenterXZ[1] = surfaceData->regionCenterXZ[1];
            surfaceConstants.regionHalfExtentXZ[0] = surfaceData->regionHalfExtentXZ[0];
            surfaceConstants.regionHalfExtentXZ[1] = surfaceData->regionHalfExtentXZ[1];
            surfaceConstants.regionValid = surfaceData->regionValid;
        }

        diagnostics_.activeWaveCount = surfaceConstants.activeWaveCount;
        return surfaceConstants;
    }

    CVar<bool>* WaterCausticsTechnique::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
