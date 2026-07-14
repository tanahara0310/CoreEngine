#include "pch.h"
#include "LensFlare.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include <algorithm>
#include <cassert>


namespace CoreEngine
{
    namespace {
        // フレアバッファの解像度分割数（1/4 解像度。ゴーストはブラーで滲ませるため十分）
        constexpr uint32_t kFlareResolutionDivisor = 4;
        constexpr uint32_t kGroupSize = 8;

        constexpr uint32_t DispatchCount(uint32_t size)
        {
            return (size + kGroupSize - 1) / kGroupSize;
        }
    }

    void LensFlare::OnConfigureRootSignature(RootSignatureConfig& config)
    {
        // 合成 CS のフレアサンプルは画面端をクランプする（WRAP だと反対端の輝度が巻き込まれる）
        config.ConfigureSampler("gLinearClamp", SamplerConfig::LinearClamp());
    }

    void LensFlare::OnCreateConstantBuffers()
    {
        auto* device = directXCommon_->GetDevice();

        const UINT paramsSize = (sizeof(LensFlareConstants) + 255) & ~255u;
        paramsCB_ = ResourceFactory::CreateBufferResource(device, paramsSize);
        HRESULT hr = paramsCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedParams_));
        assert(SUCCEEDED(hr));
        *mappedParams_ = params_;

        // ブラー方向 CB（水平/垂直。内容は不変なので初期化時に書いて以後更新しない）
        const UINT dirSize = (sizeof(BlurDirection) + 255) & ~255u;
        blurDirHCB_ = ResourceFactory::CreateBufferResource(device, dirSize);
        blurDirVCB_ = ResourceFactory::CreateBufferResource(device, dirSize);
        BlurDirection* mappedDir = nullptr;
        hr = blurDirHCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedDir));
        assert(SUCCEEDED(hr));
        *mappedDir = BlurDirection{ { 1.0f, 0.0f }, {} };
        blurDirHCB_->Unmap(0, nullptr);
        hr = blurDirVCB_->Map(0, nullptr, reinterpret_cast<void**>(&mappedDir));
        assert(SUCCEEDED(hr));
        *mappedDir = BlurDirection{ { 0.0f, 1.0f }, {} };
        blurDirVCB_->Unmap(0, nullptr);

        internalPipelinesReady_ = CreateInternalPipelines() && EnsureSourcePosTarget();
        if (!internalPipelinesReady_) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "LensFlare: 内部パイプラインの構築に失敗（フレアは無効・入力をそのまま出力）");
        }
    }

    bool LensFlare::EnsureSourcePosTarget()
    {
        DescriptorManager* descriptorManager = directXCommon_->GetDescriptorManager();
        if (!descriptorManager) {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = directXCommon_->GetDevice();

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 1;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        try {
            sourcePosBuffer_ = ResourceFactory::CreateTextureResource(
                deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Warnf(LogCategory::Graphics,
                "LensFlare: 光源位置テクスチャの生成に失敗");
            return false;
        }
        sourcePosBufferState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = desc.Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
        descriptorManager->CreateSRV(sourcePosBuffer_.Get(), srvDesc, cpuHandle, sourcePosSrvHandle_, "LensFlareSourcePosSRV");
        descriptorManager->CreateUAV(sourcePosBuffer_.Get(), uavDesc, cpuHandle, sourcePosUavHandle_, "LensFlareSourcePosUAV");
        return true;
    }

    bool LensFlare::CreateInternalPipelines()
    {
        auto* device = directXCommon_->GetDevice();

        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        struct Entry {
            CustomShaderPipeline& pipeline;
            const ICustomShaderProvider& provider;
            const char* name;
        };
        Entry entries[] = {
            { downsamplePipeline_, downsampleShaderProvider_, "Downsample" },
            { findSourcePipeline_, findSourceShaderProvider_, "FindSource" },
            { ghostsPipeline_,     ghostsShaderProvider_,     "Ghosts" },
            { blurPipeline_,       blurShaderProvider_,       "Blur" },
        };

        for (Entry& e : entries) {
            const bool built = e.pipeline.Build(device, shaderCompiler, reflectionBuilder, e.provider);
            if (!built || !e.pipeline.HasComputePSO()) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "LensFlare: {} コンピュートパイプラインの構築に失敗", e.name);
                return false;
            }
        }
        return true;
    }

    bool LensFlare::EnsureTargets(uint32_t width, uint32_t height)
    {
        if (!directXCommon_ || width == 0 || height == 0) {
            return false;
        }

        const uint32_t flareW = std::max(width / kFlareResolutionDivisor, 1u);
        const uint32_t flareH = std::max(height / kFlareResolutionDivisor, 1u);

        if (brightBuffer_ && targetsWidth_ == flareW && targetsHeight_ == flareH) {
            return true;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> deviceRef = directXCommon_->GetDevice();
        DescriptorManager* descriptorManager = directXCommon_->GetDescriptorManager();
        if (!descriptorManager) {
            return false;
        }

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = flareW;
        desc.Height = flareH;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        struct Target {
            Microsoft::WRL::ComPtr<ID3D12Resource>& tex;
            D3D12_RESOURCE_STATES& state;
            D3D12_GPU_DESCRIPTOR_HANDLE& srv;
            D3D12_GPU_DESCRIPTOR_HANDLE& uav;
            D3D12_CPU_DESCRIPTOR_HANDLE& srvCpu;
            D3D12_CPU_DESCRIPTOR_HANDLE& uavCpu;
            const char* name;
        };
        Target targets[] = {
            { brightBuffer_,  brightBufferState_,  brightSrvHandle_,  brightUavHandle_,  brightSrvCpuHandle_,  brightUavCpuHandle_,  "LensFlareBright" },
            { featureBuffer_, featureBufferState_, featureSrvHandle_, featureUavHandle_, featureSrvCpuHandle_, featureUavCpuHandle_, "LensFlareFeature" },
            { blurBuffer_,    blurBufferState_,    blurSrvHandle_,    blurUavHandle_,    blurSrvCpuHandle_,    blurUavCpuHandle_,    "LensFlareBlur" },
        };

        for (Target& t : targets) {
            try {
                t.tex = ResourceFactory::CreateTextureResource(
                    deviceRef, desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            catch (const std::exception&) {
                Logger::GetInstance().Warnf(LogCategory::Graphics,
                    "LensFlare: 中間テクスチャ {} の生成に失敗", t.name);
                return false;
            }
            t.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = desc.Format;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = desc.Format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            // リサイズによる再生成時は既存スロットへ書き直す（毎回確保するとスロットリーク）
            descriptorManager->CreateOrUpdateSRV(t.tex.Get(), srvDesc, t.srvCpu, t.srv, (std::string(t.name) + "SRV").c_str());
            descriptorManager->CreateOrUpdateUAV(t.tex.Get(), uavDesc, t.uavCpu, t.uav, (std::string(t.name) + "UAV").c_str());
        }

        targetsWidth_ = flareW;
        targetsHeight_ = flareH;
        return true;
    }

    void LensFlare::UploadConstants(uint32_t width, uint32_t height)
    {
        if (!mappedParams_) {
            return;
        }
        LensFlareConstants c = params_;
        c.flareWidth = targetsWidth_;
        c.flareHeight = targetsHeight_;
        c.screenWidth = width;
        c.screenHeight = height;
        *mappedParams_ = c;
    }

    void LensFlare::Dispatch(
        D3D12_GPU_DESCRIPTOR_HANDLE inputSrvHandle,
        D3D12_GPU_DESCRIPTOR_HANDLE outputUavHandle,
        uint32_t width,
        uint32_t height)
    {
        auto* cmdList = directXCommon_->GetCommandList();

        const bool flareReady = internalPipelinesReady_ && EnsureTargets(width, height);

        if (!flareReady) {
            // フォールバック: フレア強度 0 で合成 CS のみ実行し、入力をそのまま出力する
            // （出力を書かないとチェーン後段が未初期化テクスチャを読むため）
            LensFlareConstants c = params_;
            c.intensity = 0.0f;
            c.screenWidth = width;
            c.screenHeight = height;
            c.flareWidth = std::max(width / kFlareResolutionDivisor, 1u);
            c.flareHeight = std::max(height / kFlareResolutionDivisor, 1u);
            if (mappedParams_) { *mappedParams_ = c; }

            cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
            cmdList->SetPipelineState(computePso_.Get());
            const int texIdx = GetRootParamIndex("gTexture");
            const int flareIdx = GetRootParamIndex("gFlare");
            const int outIdx = GetRootParamIndex("gOutput");
            const int cbIdx = GetRootParamIndex("LensFlareParams");
            if (texIdx >= 0)   cmdList->SetComputeRootDescriptorTable(texIdx, inputSrvHandle);
            if (flareIdx >= 0) cmdList->SetComputeRootDescriptorTable(flareIdx, inputSrvHandle);
            if (outIdx >= 0)   cmdList->SetComputeRootDescriptorTable(outIdx, outputUavHandle);
            if (cbIdx >= 0)    cmdList->SetComputeRootConstantBufferView(cbIdx, paramsCB_->GetGPUVirtualAddress());
            cmdList->Dispatch(DispatchCount(width), DispatchCount(height), 1);
            return;
        }

        UploadConstants(width, height);

        const uint32_t flareGroupsX = DispatchCount(targetsWidth_);
        const uint32_t flareGroupsY = DispatchCount(targetsHeight_);

        // ===== Pass 1: 輝度抽出 + ダウンサンプル（入力 → brightBuffer_） =====
        ResourceBarrierHelper::Transition(cmdList, brightBuffer_.Get(),
            brightBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(downsamplePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(downsamplePipeline_.GetComputeRootSignature());
        {
            const int cbSlot = downsamplePipeline_.GetComputeRootParamIndex("LensFlareParams");
            if (cbSlot >= 0) cmdList->SetComputeRootConstantBufferView(cbSlot, paramsCB_->GetGPUVirtualAddress());
            const int inSlot = downsamplePipeline_.GetComputeRootParamIndex("gSceneColor");
            if (inSlot >= 0) cmdList->SetComputeRootDescriptorTable(inSlot, inputSrvHandle);
            const int outSlot = downsamplePipeline_.GetComputeRootParamIndex("gBright");
            if (outSlot >= 0) cmdList->SetComputeRootDescriptorTable(outSlot, brightUavHandle_);
        }
        cmdList->Dispatch(flareGroupsX, flareGroupsY, 1);

        ResourceBarrierHelper::UAV(cmdList, brightBuffer_.Get());
        ResourceBarrierHelper::Transition(cmdList, brightBuffer_.Get(),
            brightBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== Pass 1.5: 支配的な光源の UV 位置を検出（brightBuffer_ → sourcePosBuffer_） =====
        // 単一スレッドグループでフレアバッファ全体を走査し最大輝度テクセルを求める。
        // ゴースト/ハローの絞り羽根形状マスクを正しい位置へ合わせるために必要
        // （テクスチャ空間の反射・スケールによる位置決め自体は光源位置を知らなくても
        // 正しく動くが、形状マスクは「このゴーストの中心がどこか」を知る必要がある）。
        ResourceBarrierHelper::Transition(cmdList, sourcePosBuffer_.Get(),
            sourcePosBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(findSourcePipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(findSourcePipeline_.GetComputeRootSignature());
        {
            const int cbSlot = findSourcePipeline_.GetComputeRootParamIndex("LensFlareParams");
            if (cbSlot >= 0) cmdList->SetComputeRootConstantBufferView(cbSlot, paramsCB_->GetGPUVirtualAddress());
            const int inSlot = findSourcePipeline_.GetComputeRootParamIndex("gBright");
            if (inSlot >= 0) cmdList->SetComputeRootDescriptorTable(inSlot, brightSrvHandle_);
            const int outSlot = findSourcePipeline_.GetComputeRootParamIndex("gSourcePos");
            if (outSlot >= 0) cmdList->SetComputeRootDescriptorTable(outSlot, sourcePosUavHandle_);
        }
        cmdList->Dispatch(1, 1, 1);

        ResourceBarrierHelper::UAV(cmdList, sourcePosBuffer_.Get());
        ResourceBarrierHelper::Transition(cmdList, sourcePosBuffer_.Get(),
            sourcePosBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== Pass 2: ゴースト + ハロー生成（brightBuffer_ → featureBuffer_） =====
        ResourceBarrierHelper::Transition(cmdList, featureBuffer_.Get(),
            featureBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        cmdList->SetPipelineState(ghostsPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(ghostsPipeline_.GetComputeRootSignature());
        {
            const int cbSlot = ghostsPipeline_.GetComputeRootParamIndex("LensFlareParams");
            if (cbSlot >= 0) cmdList->SetComputeRootConstantBufferView(cbSlot, paramsCB_->GetGPUVirtualAddress());
            const int inSlot = ghostsPipeline_.GetComputeRootParamIndex("gBright");
            if (inSlot >= 0) cmdList->SetComputeRootDescriptorTable(inSlot, brightSrvHandle_);
            const int srcPosSlot = ghostsPipeline_.GetComputeRootParamIndex("gSourcePos");
            if (srcPosSlot >= 0) cmdList->SetComputeRootDescriptorTable(srcPosSlot, sourcePosSrvHandle_);
            const int outSlot = ghostsPipeline_.GetComputeRootParamIndex("gFeatures");
            if (outSlot >= 0) cmdList->SetComputeRootDescriptorTable(outSlot, featureUavHandle_);
        }
        cmdList->Dispatch(flareGroupsX, flareGroupsY, 1);

        ResourceBarrierHelper::UAV(cmdList, featureBuffer_.Get());

        // ===== Pass 3: 分離ガウスブラー（水平: feature → blur、垂直: blur → feature） =====
        cmdList->SetPipelineState(blurPipeline_.GetComputePSO());
        cmdList->SetComputeRootSignature(blurPipeline_.GetComputeRootSignature());

        const int blurCbSlot = blurPipeline_.GetComputeRootParamIndex("LensFlareParams");
        const int blurDirSlot = blurPipeline_.GetComputeRootParamIndex("BlurDirection");
        const int blurInSlot = blurPipeline_.GetComputeRootParamIndex("gFlareInput");
        const int blurOutSlot = blurPipeline_.GetComputeRootParamIndex("gFlareOutput");

        // 水平
        ResourceBarrierHelper::Transition(cmdList, featureBuffer_.Get(),
            featureBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, blurBuffer_.Get(),
            blurBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (blurCbSlot >= 0)  cmdList->SetComputeRootConstantBufferView(blurCbSlot, paramsCB_->GetGPUVirtualAddress());
        if (blurDirSlot >= 0) cmdList->SetComputeRootConstantBufferView(blurDirSlot, blurDirHCB_->GetGPUVirtualAddress());
        if (blurInSlot >= 0)  cmdList->SetComputeRootDescriptorTable(blurInSlot, featureSrvHandle_);
        if (blurOutSlot >= 0) cmdList->SetComputeRootDescriptorTable(blurOutSlot, blurUavHandle_);
        cmdList->Dispatch(flareGroupsX, flareGroupsY, 1);

        ResourceBarrierHelper::UAV(cmdList, blurBuffer_.Get());

        // 垂直
        ResourceBarrierHelper::Transition(cmdList, blurBuffer_.Get(),
            blurBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ResourceBarrierHelper::Transition(cmdList, featureBuffer_.Get(),
            featureBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (blurDirSlot >= 0) cmdList->SetComputeRootConstantBufferView(blurDirSlot, blurDirVCB_->GetGPUVirtualAddress());
        if (blurInSlot >= 0)  cmdList->SetComputeRootDescriptorTable(blurInSlot, blurSrvHandle_);
        if (blurOutSlot >= 0) cmdList->SetComputeRootDescriptorTable(blurOutSlot, featureUavHandle_);
        cmdList->Dispatch(flareGroupsX, flareGroupsY, 1);

        ResourceBarrierHelper::UAV(cmdList, featureBuffer_.Get());
        ResourceBarrierHelper::Transition(cmdList, featureBuffer_.Get(),
            featureBufferState_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        // ===== Pass 4: 合成（入力 + featureBuffer_ → 出力） =====
        cmdList->SetComputeRootSignature(rootSignatureManager_->GetRootSignature());
        cmdList->SetPipelineState(computePso_.Get());
        {
            const int texIdx = GetRootParamIndex("gTexture");
            const int flareIdx = GetRootParamIndex("gFlare");
            const int outIdx = GetRootParamIndex("gOutput");
            const int cbIdx = GetRootParamIndex("LensFlareParams");
            if (texIdx >= 0)   cmdList->SetComputeRootDescriptorTable(texIdx, inputSrvHandle);
            if (flareIdx >= 0) cmdList->SetComputeRootDescriptorTable(flareIdx, featureSrvHandle_);
            if (outIdx >= 0)   cmdList->SetComputeRootDescriptorTable(outIdx, outputUavHandle);
            if (cbIdx >= 0)    cmdList->SetComputeRootConstantBufferView(cbIdx, paramsCB_->GetGPUVirtualAddress());
        }
        cmdList->Dispatch(DispatchCount(width), DispatchCount(height), 1);

        // 次フレームの Pass 1〜3 に備えて内部バッファを UAV 状態へ戻す
        ResourceBarrierHelper::Transition(cmdList, brightBuffer_.Get(),
            brightBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, featureBuffer_.Get(),
            featureBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, blurBuffer_.Get(),
            blurBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ResourceBarrierHelper::Transition(cmdList, sourcePosBuffer_.Get(),
            sourcePosBufferState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    void LensFlare::DrawImGui()
    {
#ifdef USE_IMGUI
        ImGui::PushID("LensFlareParams");
        ImGui::Text("状態: %s", IsEnabled() ? "有効" : "無効");
        if (!internalPipelinesReady_) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "内部パイプライン構築失敗（無効）");
        }
        UI::Separator();

        if (ImGui::TreeNode("全体")) {
            UI::SliderFloat("強度", params_.intensity, 0.0f, 3.0f);
            UI::SliderFloat("輝度閾値", params_.threshold, 0.0f, 100.0f);
            UI::SliderFloat("ソフトニー", params_.softKnee, 0.0f, 1.0f);
            UI::SliderFloat("最大輝度クランプ", params_.maxBrightness, 1.0f, 256.0f);
            ImGui::ColorEdit3("ティント", params_.tint);
            UI::SliderFloat("太陽マスク半径", params_.sunMaskRadius, 0.02f, 0.4f);
            ImGui::TextWrapped(
                "フレア源は太陽スクリーン位置の周辺のみ（太陽限定）。"
                "パーティクル等の高輝度オブジェクトはフレアを起こさない。");
            ImGui::Text("太陽UV: (%.3f, %.3f) %s", params_.sunUv[0], params_.sunUv[1],
                params_.sunValid > 0.5f ? "有効" : "無効(後方/情報なし)");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("ゴースト")) {
            int ghostCount = static_cast<int>(params_.ghostCount);
            if (ImGui::SliderInt("ゴースト数", &ghostCount, 0, 8)) {
                params_.ghostCount = static_cast<uint32_t>(ghostCount);
            }
            UI::SliderFloat("間隔", params_.ghostDispersal, 0.1f, 1.2f);
            UI::SliderFloat("強度##ghost", params_.ghostIntensity, 0.0f, 3.0f);
            UI::SliderFloat("色収差", params_.chromaDistortion, 0.0f, 5.0f);
            UI::SliderFloat("半径", params_.ghostRadius, 0.01f, 0.2f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("絞り（多角形ゴースト）")) {
            ImGui::TextWrapped(
                "実カメラは絞り羽根の像がそのまま映り込んで多角形ゴーストになる。"
                "羽根枚数と回転、円形とのブレンド比率を調整できる。");
            int blades = static_cast<int>(params_.apertureBladeCount);
            if (ImGui::SliderInt("絞り羽根枚数", &blades, 3, 10)) {
                params_.apertureBladeCount = static_cast<float>(blades);
            }
            UI::SliderFloat("絞り回転", params_.apertureRotationRad, 0.0f, 1.047f);
            UI::SliderFloat("多角形混在比率", params_.ghostPolygonMix, 0.0f, 1.0f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("ハロー")) {
            UI::SliderFloat("半径", params_.haloWidth, 0.05f, 0.8f);
            UI::SliderFloat("強度##halo", params_.haloIntensity, 0.0f, 3.0f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("仕上げ")) {
            UI::SliderFloat("スターバースト", params_.starburstIntensity, 0.0f, 1.0f);
            UI::SliderFloat("ブラー σ", params_.blurSigma, 0.5f, 6.0f);
            ImGui::TreePop();
        }

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            params_ = LensFlareConstants{};
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }
}
