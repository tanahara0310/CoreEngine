#include "Shockwave.h"
#include "Utility/Debug/ImGui/ImguiManager.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Graphics/RootSignature/RootSignatureManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Common/DirectXCommon.h"
#include <cassert>
#include <stdexcept>


namespace CoreEngine
{
    void Shockwave::Initialize(DirectXCommon* dxCommon)
    {
        assert(dxCommon);
        directXCommon_ = dxCommon;

        ShaderCompiler compiler;
        compiler.Initialize();
        computeShaderBlob_ = compiler.CompileShader(L"Shockwave.CS.hlsl", L"cs_6_0");

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(compiler.GetDxcUtils());
        reflectionData_ = reflectionBuilder.BuildFromComputeShader(
            computeShaderBlob_.Get(), GetEffectName());

        RootSignatureConfig config;
        config.SetFlags(D3D12_ROOT_SIGNATURE_FLAG_NONE);

        rootSignatureManager_ = std::make_unique<RootSignatureManager>();
        auto buildResult = rootSignatureManager_->Build(dxCommon->GetDevice(), *reflectionData_, config);
        if (!buildResult.success) {
            throw std::runtime_error("Shockwave: Failed to create RootSignature: " + buildResult.errorMessage);
        }

        CreateComputePipeline();
        CreateConstantBuffers();
    }

    void Shockwave::CreateComputePipeline()
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
        desc.pRootSignature = rootSignatureManager_->GetRootSignature();
        desc.CS = { computeShaderBlob_->GetBufferPointer(), computeShaderBlob_->GetBufferSize() };

        HRESULT hr = directXCommon_->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(&computePso_));
        if (FAILED(hr)) {
            throw std::runtime_error("Shockwave: Failed to create Compute PSO");
        }
    }

    void Shockwave::CreateConstantBuffers()
    {
        UINT swSize = (sizeof(ShockwaveParams) + 255) & ~255;
        shockwaveParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), swSize);
        HRESULT hr = shockwaveParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedShockwaveParams_));
        assert(SUCCEEDED(hr));
        UpdateConstantBuffer();

        UINT screenSize = (sizeof(ScreenParams) + 255) & ~255;
        screenParamsCB_ = ResourceFactory::CreateBufferResource(directXCommon_->GetDevice(), screenSize);
        hr = screenParamsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedScreenParams_));
        assert(SUCCEEDED(hr));
    }

    void Shockwave::UpdateConstantBuffer()
    {
        if (mappedShockwaveParams_) { *mappedShockwaveParams_ = params_; }
    }

    void Shockwave::UpdateScreenConstantBuffer(uint32_t width, uint32_t height)
    {
        if (mappedScreenParams_) {
            mappedScreenParams_->screenWidth  = width;
            mappedScreenParams_->screenHeight = height;
        }
    }

    void Shockwave::SetParams(const ShockwaveParams& params)
    {
        params_ = params;
        UpdateConstantBuffer();
    }

    void Shockwave::StartShockwave(float centerX, float centerY)
    {
        params_.center[0] = centerX;
        params_.center[1] = centerY;
        params_.time      = 0.0f;
        isActive_         = true;
        UpdateConstantBuffer();
    }

    void Shockwave::Update(float deltaTime)
    {
        if (!isActive_) { return; }

        params_.time += deltaTime * params_.speed;
        if (params_.time >= maxRadius_) {
            isActive_    = false;
            params_.time = 0.0f;
        }
        UpdateConstantBuffer();
    }

    void Shockwave::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        UpdateScreenConstantBuffer(width, height);

        auto* cmdList = directXCommon_->GetCommandList();
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());

        int textureIdx = GetRootParamIndex("gTexture");
        int outputIdx  = GetRootParamIndex("gOutput");
        int swIdx      = GetRootParamIndex("ShockwaveParams");
        int screenIdx  = GetRootParamIndex("ScreenParams");

        if (textureIdx >= 0) cmdList->SetComputeRootDescriptorTable(textureIdx, inputSrvHandle);
        if (outputIdx >= 0)  cmdList->SetComputeRootDescriptorTable(outputIdx, outputUavHandle);
        if (swIdx >= 0)      cmdList->SetComputeRootConstantBufferView(swIdx, shockwaveParamsCB_->GetGPUVirtualAddress());
        if (screenIdx >= 0)  cmdList->SetComputeRootConstantBufferView(screenIdx, screenParamsCB_->GetGPUVirtualAddress());

        uint32_t groupX = (width  + 7) / 8;
        uint32_t groupY = (height + 7) / 8;
        cmdList->Dispatch(groupX, groupY, 1);
    }

    void Shockwave::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("ShockwaveParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        ImGui::Text("アクティブ: %s", isActive_ ? "true" : "false");
        UI::Separator();

        bool changed = false;
        if (ImGui::TreeNode("パラメータ")) {
            changed |= UI::SliderFloat("強度", params_.strength, 0.0f, 1.0f);
            changed |= UI::SliderFloat("波の厚さ", params_.thickness, 0.01f, 0.5f);
            changed |= UI::SliderFloat("速度", params_.speed, 0.1f, 5.0f);
            ImGui::TreePop();
        }
        if (changed) { UpdateConstantBuffer(); }

        UI::Separator();
        if (ImGui::Button("ショックウェーブ発動")) {
            StartShockwave(0.5f, 0.5f);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
