#include "pch.h"
#include "LensFlare.h"
#include "Editor/ImGui/ImguiManager.h"
#include "Utility/Logger/Logger.h"
#include "Graphics/Resource/ResourceFactory.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Graphics/Common/Core/DescriptorManager.h"
#include "Graphics/Common/ResourceBarrierHelper.h"
#include "Utility/CVar/CVar.h"
#ifdef USE_IMGUI
#include "Editor/ImGui/CVarPanel.h"
#endif
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

        // 既定値の根拠は LensFlare.h の LensFlareConstants のコメントを参照
        CVar<float> cvIntensity{
            "r.LensFlare.Intensity", 0.6f,
            "フレア全体の強度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvThreshold{
            "r.LensFlare.Threshold", 28.0f,
            "フレア源とする輝度の閾値（HDR）。下げすぎると太陽以外にも反応する",
            CVarRange{ 0.0f, 100.0f } };

        CVar<float> cvSoftKnee{
            "r.LensFlare.SoftKnee", 0.5f,
            "閾値付近のなめらかさ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvMaxBrightness{
            "r.LensFlare.MaxBrightness", 64.0f,
            "抽出輝度のクランプ上限",
            CVarRange{ 1.0f, 256.0f } };

        CVar<Vector4> cvTint{
            "r.LensFlare.Tint", Vector4{ 1.0f, 0.95f, 0.9f, 1.0f },
            "フレア全体のティント（RGBA）" };

        CVar<float> cvSunMaskRadius{
            "r.LensFlare.SunMaskRadius", 0.12f,
            "抽出を許可する太陽周辺の半径。広げるとミー散乱オーラまで源になる",
            CVarRange{ 0.02f, 0.4f } };

        CVar<int> cvGhostCount{
            "r.LensFlare.GhostCount", 6,
            "ゴーストの個数",
            CVarRange{ 0.0f, 8.0f } };

        CVar<float> cvGhostDispersal{
            "r.LensFlare.GhostDispersal", 0.6f,
            "ゴーストの間隔",
            CVarRange{ 0.1f, 1.2f } };

        CVar<float> cvGhostIntensity{
            "r.LensFlare.GhostIntensity", 2.0f,
            "ゴーストの強度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvChromaDistortion{
            "r.LensFlare.ChromaDistortion", 1.5f,
            "ゴーストの色収差",
            CVarRange{ 0.0f, 5.0f } };

        CVar<float> cvGhostRadius{
            "r.LensFlare.GhostRadius", 0.12f,
            "ゴーストの半径。小さすぎると多角形の角が潰れる",
            CVarRange{ 0.01f, 0.2f } };

        CVar<int> cvApertureBladeCount{
            "r.LensFlare.ApertureBladeCount", 6,
            "絞り羽根の枚数（多角形ゴーストの角数）",
            CVarRange{ 3.0f, 10.0f } };

        CVar<float> cvApertureRotationRad{
            "r.LensFlare.ApertureRotation", 0.3f,
            "絞りの回転角 [rad]",
            CVarRange{ 0.0f, 1.047f } };

        CVar<float> cvGhostPolygonMix{
            "r.LensFlare.GhostPolygonMix", 0.85f,
            "多角形ゴーストの混在比率。0=全て円形、1=全て多角形",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvHaloWidth{
            "r.LensFlare.HaloWidth", 0.45f,
            "ハローの半径",
            CVarRange{ 0.05f, 0.8f } };

        CVar<float> cvHaloIntensity{
            "r.LensFlare.HaloIntensity", 1.0f,
            "ハローの強度",
            CVarRange{ 0.0f, 3.0f } };

        CVar<float> cvStarburstIntensity{
            "r.LensFlare.StarburstIntensity", 0.5f,
            "スターバースト変調の強さ",
            CVarRange{ 0.0f, 1.0f } };

        CVar<float> cvBlurSigma{
            "r.LensFlare.BlurSigma", 1.0f,
            "ガウスブラー σ。大きすぎると絞り羽根の角が潰れて円形に戻る",
            CVarRange{ 0.5f, 6.0f } };

        CVar<bool> cvEnabled{
            "r.LensFlare.Enabled", true,
            "レンズフレアを有効にする（大気の太陽で自動発生）",
            CVarRange{}, CVarFlags::NoUI };

        constexpr const char* kCVarPrefix = "r.LensFlare";
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
        *mappedParams_ = BuildConstants(0, 0, 0, 0);

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

    LensFlare::LensFlareConstants LensFlare::BuildConstants(
        uint32_t width, uint32_t height, uint32_t flareWidth, uint32_t flareHeight) const
    {
        LensFlareConstants c{};
        c.threshold          = cvThreshold.Get();
        c.softKnee           = cvSoftKnee.Get();
        c.ghostDispersal     = cvGhostDispersal.Get();
        c.ghostIntensity     = cvGhostIntensity.Get();
        c.haloWidth          = cvHaloWidth.Get();
        c.haloIntensity      = cvHaloIntensity.Get();
        c.chromaDistortion   = cvChromaDistortion.Get();
        c.starburstIntensity = cvStarburstIntensity.Get();
        c.intensity          = cvIntensity.Get();
        c.ghostCount         = static_cast<uint32_t>(cvGhostCount.Get());
        c.maxBrightness      = cvMaxBrightness.Get();
        c.blurSigma          = cvBlurSigma.Get();

        const Vector4& tint = cvTint.Get();
        c.tint[0] = tint.x;
        c.tint[1] = tint.y;
        c.tint[2] = tint.z;
        c.tint[3] = tint.w;

        c.apertureBladeCount  = static_cast<float>(cvApertureBladeCount.Get());
        c.apertureRotationRad = cvApertureRotationRad.Get();
        c.ghostRadius         = cvGhostRadius.Get();
        c.ghostPolygonMix     = cvGhostPolygonMix.Get();
        c.sunMaskRadius       = cvSunMaskRadius.Get();

        // ここから下は実行時値
        c.sunUv[0]     = sunUv_[0];
        c.sunUv[1]     = sunUv_[1];
        c.sunValid     = sunValid_;
        c.flareWidth   = flareWidth;
        c.flareHeight  = flareHeight;
        c.screenWidth  = width;
        c.screenHeight = height;
        return c;
    }

    void LensFlare::UploadConstants(uint32_t width, uint32_t height)
    {
        if (!mappedParams_) {
            return;
        }
        *mappedParams_ = BuildConstants(width, height, targetsWidth_, targetsHeight_);
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
            LensFlareConstants c = BuildConstants(
                width, height,
                std::max(width / kFlareResolutionDivisor, 1u),
                std::max(height / kFlareResolutionDivisor, 1u));
            c.intensity = 0.0f;
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

        ImGui::TextWrapped(
            "フレア源は太陽スクリーン位置の周辺のみ（太陽限定）。"
            "パーティクル等の高輝度オブジェクトはフレアを起こさない。");
        ImGui::Text("太陽UV: (%.3f, %.3f) %s", sunUv_[0], sunUv_[1],
            sunValid_ > 0.5f ? "有効" : "無効(後方/情報なし)");
        UI::Separator();

        CVarUI::DrawTree(kCVarPrefix);

        UI::Separator();
        if (ImGui::Button("デフォルトに戻す")) {
            CVarUI::ResetTree(kCVarPrefix);
        }
        ImGui::PopID();
#endif // USE_IMGUI
    }

    CVar<bool>* LensFlare::GetEnabledCVar() const
    {
        return &cvEnabled;
    }
}
