#include "pch.h"
#include "Utility/CVar/CVar.h"
#include "WaterCausticsTechnique.h"

#include "Graphics/Light/LightManager.h"
#include "Graphics/Render/Pass/RenderPass.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/Render/RenderTarget/OffscreenRenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTarget.h"
#include "Graphics/Render/RenderTarget/RenderTargetDescriptor.h"
#include "Graphics/Render/RenderTarget/RenderTargetManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Render/RenderManager.h"
#include "Camera/View/ViewInfo.h"
#include "Math/MathCore.h"
#include "Utility/Logger/Logger.h"
#include <cassert>
#include <cstring>

#ifdef USE_IMGUI
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

    // 定数バッファと PSO は基底が用意する。ここでは水面固有の初期値だけを入れる
    void WaterCausticsTechnique::Initialize(DirectXCommon* dxCommon)
    {
        RenderingTechniqueBase::Initialize(dxCommon);
        CreateConstantBuffers();
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

        UpdateWaterSurfaceBuffer(context.waterSurfaceState);
        UpdateMainLightBuffer(context.lightManager);

        // 深度復元用 View*Projection 逆行列（実行中のビューの ViewInfo から取る）
        if (context.frameViews) {
            const ViewInfo& view = context.frameViews->Get(context.viewSettings.viewType);
            if (view.isValid) {
                std::memcpy(params_.invViewProjMatrix, &view.invViewProjection, sizeof(float) * 16);
            }
        }
        UpdateParamsBuffer();
        diagnostics_.normalHandle = gBufferManager->GetSRVHandle(GBufferManager::Target::NormalRoughness).ptr;

        if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(target)) {
            offscreen->SetUseDepthBuffer(false);
        }
        target->SetClearEnabled(true);
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
        if (mainLightIdx >= 0 && mainLightBuffer_) {
            cmdList->SetGraphicsRootConstantBufferView(mainLightIdx,
                mainLightBuffer_->GetGPUVirtualAddress());
        }

        const int waterSurfaceIdx = GetRootParamIndex("gWaterSurfaceData");
        if (waterSurfaceIdx >= 0 && waterSurfaceBuffer_) {
            cmdList->SetGraphicsRootConstantBufferView(waterSurfaceIdx,
                waterSurfaceBuffer_->GetGPUVirtualAddress());
        }

        const int paramsIdx = GetRootParamIndex("WaterCausticsParams");
        if (paramsIdx >= 0 && paramsBuffer_) {
            cmdList->SetGraphicsRootConstantBufferView(paramsIdx,
                paramsBuffer_->GetGPUVirtualAddress());
        }

        DrawFullscreenQuad(cmdList);

        target->End(cmdList);

        if (auto* offscreen = dynamic_cast<OffscreenRenderTarget*>(target)) {
            offscreen->SetUseDepthBuffer(true);
        }

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
#ifdef USE_IMGUI
        bool changed = false;
        ImGui::PushID("WaterCausticsTechnique");
        changed |= UI::SliderFloat("Intensity", params_.intensity, 0.0f, 8.0f);
        changed |= UI::SliderFloat("Depth Attenuation", params_.depthAttenuation, 0.0f, 2.0f);
        changed |= UI::SliderFloat("Curvature Scale", params_.curvatureScale, 0.0f, 30.0f);
        changed |= UI::SliderFloat("Surface Sample Radius", params_.surfaceSampleRadius, 0.05f, 2.0f);
        changed |= UI::SliderFloat("Refractive Index", params_.refractiveIndex, 1.0f, 1.6f);
        changed |= UI::SliderFloat("Receiver Normal Strength", params_.receiverNormalStrength, 0.0f, 2.0f);
        changed |= UI::SliderFloat("Alignment Power", params_.alignmentPower, 1.0f, 64.0f);
        changed |= UI::SliderFloat("Debug Display Scale", params_.debugDisplayScale, 0.1f, 16.0f);
        int debugViewMode = static_cast<int>(params_.debugViewMode);
        if (ImGui::SliderInt("Debug View Mode", &debugViewMode, 0, 2)) {
            params_.debugViewMode = static_cast<uint32_t>(debugViewMode);
            changed = true;
        }
        bool debugLogEnabled = (params_.debugLogEnabled != 0);
        if (ImGui::Checkbox("Debug Log Enabled", &debugLogEnabled)) {
            params_.debugLogEnabled = debugLogEnabled ? 1u : 0u;
            changed = true;
        }
        ImGui::Text("Diag: waves=%u light=%u out=0x%llX", diagnostics_.activeWaveCount, diagnostics_.mainLightEnabled, diagnostics_.outputHandle);
        if (changed) {
            UpdateParamsBuffer();
        }
        ImGui::PopID();
#endif
    }

    const std::wstring& WaterCausticsTechnique::GetPixelShaderPath() const
    {
        static const std::wstring path = L"WaterCaustics.PS.hlsl";
        return path;
    }

    void WaterCausticsTechnique::SetParams(const Params& params)
    {
        params_ = params;
        UpdateParamsBuffer();
    }

    void WaterCausticsTechnique::CreateConstantBuffers()
    {
        assert(directXCommon_);

        const UINT paramsBufferSize = (sizeof(Params) + 255u) & ~255u;
        paramsBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), paramsBufferSize);
        [[maybe_unused]] HRESULT hr = paramsBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedParams_));
        assert(SUCCEEDED(hr));

        const UINT mainLightBufferSize = (sizeof(MainLightConstants) + 255u) & ~255u;
        mainLightBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), mainLightBufferSize);
        hr = mainLightBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedMainLight_));
        assert(SUCCEEDED(hr));

        const UINT waterSurfaceBufferSize = (sizeof(WaterSurfaceConstants) + 255u) & ~255u;
        waterSurfaceBuffer_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), waterSurfaceBufferSize);
        hr = waterSurfaceBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&mappedWaterSurface_));
        assert(SUCCEEDED(hr));

        UpdateParamsBuffer();
        UpdateMainLightBuffer(nullptr);
        UpdateWaterSurfaceBuffer(nullptr);
    }

    void WaterCausticsTechnique::UpdateMainLightBuffer(LightManager* lightManager)
    {
        if (!mappedMainLight_) {
            return;
        }

        MainLightConstants constants{};
        if (lightManager) {
            if (Light* light = lightManager->GetDirectionalLight(0); light && light->enabled) {
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
        *mappedMainLight_ = constants;
    }

    void WaterCausticsTechnique::UpdateWaterSurfaceBuffer(const WaterSurfaceData* surfaceData)
    {
        if (!mappedWaterSurface_) {
            return;
        }

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
        *mappedWaterSurface_ = surfaceConstants;
    }

    void WaterCausticsTechnique::UpdateParamsBuffer()
    {
        if (mappedParams_) {
            *mappedParams_ = params_;
        }
    }

    CVar<bool>* WaterCausticsTechnique::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
