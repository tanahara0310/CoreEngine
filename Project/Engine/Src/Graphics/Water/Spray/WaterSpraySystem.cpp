#include "pch.h"
#include "WaterSpraySystem.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Camera/View/ViewInfo.h"
#include "Graphics/RHI/Barrier/BarrierBatch.h"
#include "Graphics/RHI/Descriptor/DescriptorAllocator.h"
#include "Graphics/RHI/GraphicsCore.h"
#include "Graphics/RHI/Resource/ResourceFactory.h"
#include "Graphics/RHI/Resource/UploadRing.h"
#include "Graphics/Render/GBuffer/GBufferManager.h"
#include "Graphics/RootSignature/RootSignatureConfig.h"
#include "Graphics/Shader/ShaderCompiler.h"
#include "Graphics/Shader/ShaderReflectionBuilder.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    namespace
    {
        constexpr UINT kEmitGroupSize = 8;
        constexpr UINT kUpdateGroupSize = 64;
        constexpr UINT kSmoothGroupSize = 8;
        /// @brief シーンの色（オフスクリーンの HDR）の形式
        constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kSplatDepthFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr DXGI_FORMAT kSplatDepthBufferFormat = DXGI_FORMAT_D32_FLOAT;
        constexpr DXGI_FORMAT kThicknessFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        /// @brief しぶきの深度をならす幅 [m]
        constexpr float kSmoothWorldRadius = 0.35f;
        /// @brief しぶきの深度をならすときに混ぜる深さの差の目安 [m]
        constexpr float kSmoothDepthFalloff = 0.2f;

        // 描く引数の並び（WaterSpray.hlsli の kSprayArgs* と同じ）
        constexpr uint32_t kDrawArgsDroplet = 0;      ///< しぶきの DRAW 引数（4 個）
        constexpr uint32_t kDrawArgsMist = 4;         ///< 霧の DRAW 引数（4 個）
        constexpr uint32_t kDrawArgsDropletCount = 8; ///< 生きているしぶきの数（64 ビット。流体描画を飛ばす判定）
        constexpr uint32_t kDrawArgsCount = 12;

        /// @brief 名前の資源がシェーダーにあれば、コンピュートの定数を差す
        void SetComputeCBV(ID3D12GraphicsCommandList* cmdList, const CustomShaderPipeline& pipeline,
            const char* name, D3D12_GPU_VIRTUAL_ADDRESS address)
        {
            if (const int slot = pipeline.GetComputeRootParamIndex(name); slot >= 0 && address != 0) {
                cmdList->SetComputeRootConstantBufferView(static_cast<UINT>(slot), address);
            }
        }

        /// @brief 名前の資源がシェーダーにあれば、コンピュートのディスクリプタテーブルを差す
        void SetComputeTable(ID3D12GraphicsCommandList* cmdList, const CustomShaderPipeline& pipeline,
            const char* name, D3D12_GPU_DESCRIPTOR_HANDLE handle)
        {
            if (const int slot = pipeline.GetComputeRootParamIndex(name); slot >= 0 && handle.ptr != 0) {
                cmdList->SetComputeRootDescriptorTable(static_cast<UINT>(slot), handle);
            }
        }
    }

    bool WaterSpraySystem::Initialize(GraphicsCore* dxCommon, DescriptorAllocator* descriptorAllocator)
    {
        dxCommon_ = dxCommon;
        descriptorAllocator_ = descriptorAllocator;
        if (!dxCommon_ || !dxCommon_->GetDevice() || !descriptorAllocator_) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: initialization failed. dxCommon={} descriptorAllocator={}",
                dxCommon_ != nullptr, descriptorAllocator_ != nullptr);
            return false;
        }
        if (!CreatePipelines() || !CreateParticleBuffers()) {
            return false;
        }
        isInitialized_ = true;
        return true;
    }

    void WaterSpraySystem::SetSettings(const Settings& settings)
    {
        Settings next = settings;
        next.amount = (std::max)(next.amount, 0.0f);
        next.dropletRadius = (std::max)(next.dropletRadius, 0.01f);
        next.mistRadius = (std::max)(next.mistRadius, 0.01f);
        settings_ = next;
    }

    bool WaterSpraySystem::CreatePipelines()
    {
        ShaderCompiler shaderCompiler;
        shaderCompiler.Initialize();

        ShaderReflectionBuilder reflectionBuilder;
        reflectionBuilder.Initialize(shaderCompiler.GetDxcUtils());

        struct ComputeEntry {
            CustomShaderPipeline* pipeline;
            const ICustomShaderProvider* provider;
            const char* name;
        };
        const ComputeEntry computeEntries[] = {
            { &emitPipeline_, &emitShaderProvider_, "emit" },
            { &updatePipeline_, &updateShaderProvider_, "update" },
            { &smoothPipeline_, &smoothShaderProvider_, "smooth" },
        };
        for (const ComputeEntry& entry : computeEntries) {
            const bool built = entry.pipeline->Build(
                dxCommon_->GetDevice(), shaderCompiler, reflectionBuilder, *entry.provider);
            if (!built || !entry.pipeline->HasComputePSO()) {
                Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                    "WaterSpraySystem: failed to build {} compute pipeline.", entry.name);
                return false;
            }
        }

        compositeMotionVectorFormat_ =
            GBufferManager::kRenderTargetFormats[static_cast<size_t>(GBufferManager::Target::MotionVector)];

        const DXGI_FORMAT sceneColorFormats[] = { kSceneColorFormat };
        const DXGI_FORMAT splatDepthFormats[] = { kSplatDepthFormat };
        const DXGI_FORMAT thicknessFormats[] = { kThicknessFormat };
        const DXGI_FORMAT compositeFormats[] = { kSceneColorFormat, compositeMotionVectorFormat_ };

        GraphicsTargetDesc particleTarget{};
        particleTarget.renderTargetFormats = sceneColorFormats;
        particleTarget.renderTargetCount = 1;
        particleTarget.blendMode = BlendMode::kBlendModeNormal;
        particleTarget.writeAlpha = false;

        GraphicsTargetDesc splatDepthTarget{};
        splatDepthTarget.renderTargetFormats = splatDepthFormats;
        splatDepthTarget.renderTargetCount = 1;
        splatDepthTarget.depthFormat = kSplatDepthBufferFormat;
        splatDepthTarget.depthTest = true;

        GraphicsTargetDesc thicknessTarget{};
        thicknessTarget.renderTargetFormats = thicknessFormats;
        thicknessTarget.renderTargetCount = 1;
        thicknessTarget.blendMode = BlendMode::kBlendModeAdd;

        GraphicsTargetDesc compositeTarget{};
        compositeTarget.renderTargetFormats = compositeFormats;
        compositeTarget.renderTargetCount = 2;

        return BuildGraphicsProgram(shaderCompiler, reflectionBuilder, particleProgram_,
                   L"WaterSprayParticle.VS.hlsl", L"WaterSprayParticle.PS.hlsl", particleTarget, "WaterSprayParticle")
            && BuildGraphicsProgram(shaderCompiler, reflectionBuilder, splatDepthProgram_,
                   L"WaterSpraySplat.VS.hlsl", L"WaterSpraySplatDepth.PS.hlsl", splatDepthTarget, "WaterSpraySplatDepth")
            && BuildGraphicsProgram(shaderCompiler, reflectionBuilder, splatThicknessProgram_,
                   L"WaterSpraySplat.VS.hlsl", L"WaterSpraySplatThickness.PS.hlsl", thicknessTarget, "WaterSpraySplatThickness")
            && BuildGraphicsProgram(shaderCompiler, reflectionBuilder, compositeProgram_,
                   L"FullScreen.VS.hlsl", L"WaterSprayComposite.PS.hlsl", compositeTarget, "WaterSprayComposite");
    }

    bool WaterSpraySystem::BuildGraphicsProgram(
        ShaderCompiler& shaderCompiler, ShaderReflectionBuilder& reflectionBuilder,
        GraphicsProgram& program, const wchar_t* vertexShader, const wchar_t* pixelShader,
        const GraphicsTargetDesc& target, const char* name)
    {
        Microsoft::WRL::ComPtr<IDxcBlob> vsBlob = shaderCompiler.CompileShader(vertexShader, L"vs_6_0");
        Microsoft::WRL::ComPtr<IDxcBlob> psBlob = shaderCompiler.CompileShader(pixelShader, L"ps_6_0");
        if (!vsBlob || !psBlob) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: failed to compile {} shaders.", name);
            return false;
        }

        const auto reflection = reflectionBuilder.BuildFromShaders(vsBlob.Get(), psBlob.Get(), name);
        RootSignatureConfig config;
        config.SetDefaultCBVStrategy(BindingStrategy::RootDescriptor);
        config.SetDefaultSRVStrategy(BindingStrategy::DescriptorTable);
        config.SetDefaultSamplerStrategy(BindingStrategy::StaticSampler);
        config.ConfigureSampler("gLinearClamp", SamplerConfig::LinearClamp());

        program.rootSignature = std::make_unique<RootSignatureManager>();
        const auto result = program.rootSignature->Build(dxCommon_->GetDevice(), *reflection, config);
        if (!result.success) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: failed to build {} root signature: {}", name, result.errorMessage);
            program.rootSignature.reset();
            return false;
        }

        program.blendMode = target.blendMode;
        PipelineStateBuilder builder = program.pipelineStates.CreateBuilder();
        builder.SetDebugName(name)
            .SetRasterizer(D3D12_CULL_MODE_NONE, D3D12_FILL_MODE_SOLID)
            .SetDepthStencil(target.depthTest, target.depthTest, D3D12_COMPARISON_FUNC_LESS)
            .SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE)
            .SetRenderTargetFormats(target.renderTargetFormats, target.renderTargetCount)
            .SetDepthStencilFormat(target.depthFormat)
            .SetColorWriteMask(D3D12_COLOR_WRITE_ENABLE_ALL, target.writeAlpha);
        if (!builder.Build(dxCommon_->GetDevice(), vsBlob.Get(), psBlob.Get(),
                program.rootSignature->GetRootSignature(), { target.blendMode })) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: failed to build {} pipeline state.", name);
            return false;
        }
        return true;
    }

    bool WaterSpraySystem::CreateParticleBuffers()
    {
        ID3D12Device* device = dxCommon_->GetDevice();

        const auto createBuffer = [device](GpuResource& resource, UINT64 bytes) {
            D3D12_HEAP_PROPERTIES heapProperties{};
            heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = bytes;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer)))) {
                return false;
            }
            resource.Reset(std::move(buffer), D3D12_RESOURCE_STATE_COMMON);
            return true;
        };

        // 粒のバッファは作られたときに 0（寿命 0 = 空き）で埋まっている
        if (!createBuffer(particles_, static_cast<UINT64>(kCapacity) * sizeof(Particle))
            || !createBuffer(counter_, 256)) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: failed to create particle buffers.");
            return false;
        }
        particles_.Get()->SetName(L"WaterSprayParticles");
        counter_.Get()->SetName(L"WaterSprayCounter");

        D3D12_UNORDERED_ACCESS_VIEW_DESC particleUav{};
        particleUav.Format = DXGI_FORMAT_UNKNOWN;
        particleUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        particleUav.Buffer.NumElements = kCapacity;
        particleUav.Buffer.StructureByteStride = sizeof(Particle);
        particleUAV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateUAV(particles_.Get(), particleUav, "WaterSprayParticlesUAV"));

        D3D12_SHADER_RESOURCE_VIEW_DESC particleSrv{};
        particleSrv.Format = DXGI_FORMAT_UNKNOWN;
        particleSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        particleSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        particleSrv.Buffer.NumElements = kCapacity;
        particleSrv.Buffer.StructureByteStride = sizeof(Particle);
        particleSRV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateSRV(particles_.Get(), particleSrv, "WaterSprayParticlesSRV"));

        D3D12_UNORDERED_ACCESS_VIEW_DESC counterUav{};
        counterUav.Format = DXGI_FORMAT_UNKNOWN;
        counterUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        counterUav.Buffer.NumElements = 1;
        counterUav.Buffer.StructureByteStride = sizeof(uint32_t);
        counterUAV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateUAV(counter_.Get(), counterUav, "WaterSprayCounterUAV"));

        // ---- 描く粒の一覧と、間接描画の引数 ----
        if (!createBuffer(drawArgs_, 256)
            || !createBuffer(dropletList_, static_cast<UINT64>(kCapacity) * sizeof(uint32_t))
            || !createBuffer(mistList_, static_cast<UINT64>(kCapacity) * sizeof(uint32_t))) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: failed to create draw list buffers.");
            return false;
        }
        drawArgs_.Get()->SetName(L"WaterSprayDrawArgs");
        dropletList_.Get()->SetName(L"WaterSprayDropletList");
        mistList_.Get()->SetName(L"WaterSprayMistList");

        const auto makeUintUav = [](UINT count) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Buffer.NumElements = count;
            desc.Buffer.StructureByteStride = sizeof(uint32_t);
            return desc;
        };
        const auto makeUintSrv = [](UINT count) {
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            desc.Buffer.NumElements = count;
            desc.Buffer.StructureByteStride = sizeof(uint32_t);
            return desc;
        };
        drawArgsUAV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateUAV(drawArgs_.Get(), makeUintUav(kDrawArgsCount), "WaterSprayDrawArgsUAV"));
        dropletListUAV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateUAV(dropletList_.Get(), makeUintUav(kCapacity), "WaterSprayDropletListUAV"));
        dropletListSRV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateSRV(dropletList_.Get(), makeUintSrv(kCapacity), "WaterSprayDropletListSRV"));
        mistListUAV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateUAV(mistList_.Get(), makeUintUav(kCapacity), "WaterSprayMistListUAV"));
        mistListSRV_ = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateSRV(mistList_.Get(), makeUintSrv(kCapacity), "WaterSprayMistListSRV"));

        // 毎フレームの初期値：しぶき・霧とも 0 頂点・1 インスタンス、生きているしぶきは 0
        drawArgsInitial_ = ResourceFactory::CreateBufferResource(device, kDrawArgsCount * sizeof(uint32_t));
        uint32_t* initial = nullptr;
        if (FAILED(drawArgsInitial_->Map(0, nullptr, reinterpret_cast<void**>(&initial))) || !initial) {
            return false;
        }
        for (uint32_t i = 0; i < kDrawArgsCount; ++i) {
            initial[i] = 0;
        }
        initial[kDrawArgsDroplet + 1] = 1;
        initial[kDrawArgsMist + 1] = 1;
        drawArgsInitial_->Unmap(0, nullptr);

        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        D3D12_COMMAND_SIGNATURE_DESC signature{};
        signature.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
        signature.NumArgumentDescs = 1;
        signature.pArgumentDescs = &argument;
        if (FAILED(device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&drawSignature_)))) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: failed to create draw command signature.");
            return false;
        }
        return true;
    }

    void WaterSpraySystem::Simulate(ID3D12GraphicsCommandList* cmdList, const SimulateInput& input)
    {
        if (!IsActive() || !cmdList || input.surfaceConstants == 0 || input.fftDisplacementSRV.ptr == 0
            || input.shoreWaveFieldSRV.ptr == 0 || input.windowSize <= 0.0f) {
            return;
        }
        if (input.frameNumber == lastSimulateFrame_) {
            return;
        }
        lastSimulateFrame_ = input.frameNumber;

        Barrier::Transition(cmdList, particles_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Barrier::Transition(cmdList, counter_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        UploadRing& uploadRing = dxCommon_->GetUploadRing();

        // ---- 砕けている峰から出す ----
        if (input.deltaTime > 0.0f && settings_.amount > 0.0f) {
            EmitConstants constants{};
            constants.originXZ[0] = input.windowOriginXZ[0];
            constants.originXZ[1] = input.windowOriginXZ[1];
            constants.cellSize = input.windowSize / static_cast<float>(kEmitResolution);
            constants.deltaTime = input.deltaTime;
            constants.amount = settings_.amount;
            constants.frameIndex = emitFrameIndex_++;
            constants.dropletRadius = settings_.dropletRadius;
            constants.mistRadius = settings_.mistRadius;

            cmdList->SetPipelineState(emitPipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(emitPipeline_.GetComputeRootSignature());
            SetComputeTable(cmdList, emitPipeline_, "gFFTDisplacement", input.fftDisplacementSRV);
            SetComputeTable(cmdList, emitPipeline_, "gShoreWaveField", input.shoreWaveFieldSRV);
            SetComputeTable(cmdList, emitPipeline_, "gParticles", particleUAV_.Get().gpuHandle);
            SetComputeTable(cmdList, emitPipeline_, "gSprayCounter", counterUAV_.Get().gpuHandle);
            SetComputeCBV(cmdList, emitPipeline_, "WaterSprayEmitConstants", uploadRing.AllocateConstants(constants));
            SetComputeCBV(cmdList, emitPipeline_, "WaterSurfaceData", input.surfaceConstants);
            const UINT groups = (kEmitResolution + kEmitGroupSize - 1) / kEmitGroupSize;
            cmdList->Dispatch(groups, groups, 1);
            Barrier::UAV(cmdList, particles_);
        }

        // ---- 描く引数を初期値に戻す ----
        Barrier::Transition(cmdList, drawArgs_, D3D12_RESOURCE_STATE_COPY_DEST);
        cmdList->CopyBufferRegion(drawArgs_.Get(), 0, drawArgsInitial_.Get(), 0, kDrawArgsCount * sizeof(uint32_t));
        {
            BarrierBatch batch(cmdList);
            batch.Transition(drawArgs_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            batch.Transition(dropletList_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            batch.Transition(mistList_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        // ---- 動かして、落ちた粒を消し、生きている粒を一覧に書く ----
        {
            UpdateConstants constants{};
            constants.deltaTime = (std::max)(input.deltaTime, 0.0f);
            if (input.seabedHeightSRV.ptr != 0) {
                constants.seabedOriginXZ[0] = input.windowOriginXZ[0];
                constants.seabedOriginXZ[1] = input.windowOriginXZ[1];
                constants.seabedSize = input.windowSize;
            }

            cmdList->SetPipelineState(updatePipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(updatePipeline_.GetComputeRootSignature());
            SetComputeTable(cmdList, updatePipeline_, "gFFTDisplacement", input.fftDisplacementSRV);
            SetComputeTable(cmdList, updatePipeline_, "gShoreWaveField", input.shoreWaveFieldSRV);
            SetComputeTable(cmdList, updatePipeline_, "gSeabedHeight", input.seabedHeightSRV);
            SetComputeTable(cmdList, updatePipeline_, "gParticles", particleUAV_.Get().gpuHandle);
            SetComputeTable(cmdList, updatePipeline_, "gSprayDrawArgs", drawArgsUAV_.Get().gpuHandle);
            SetComputeTable(cmdList, updatePipeline_, "gSprayDropletList", dropletListUAV_.Get().gpuHandle);
            SetComputeTable(cmdList, updatePipeline_, "gSprayMistList", mistListUAV_.Get().gpuHandle);
            SetComputeCBV(cmdList, updatePipeline_, "WaterSprayUpdateConstants", uploadRing.AllocateConstants(constants));
            SetComputeCBV(cmdList, updatePipeline_, "WaterSurfaceData", input.surfaceConstants);
            cmdList->Dispatch((kCapacity + kUpdateGroupSize - 1) / kUpdateGroupSize, 1, 1);
        }

        {
            BarrierBatch batch(cmdList);
            batch.Transition(particles_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(dropletList_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(mistList_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(drawArgs_, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }
        simulated_ = true;
    }

    bool WaterSpraySystem::CreateScreenTexture(
        ScreenTexture& texture, uint32_t width, uint32_t height, DXGI_FORMAT format,
        D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue,
        const char* name)
    {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        desc.Flags = flags;

        try {
            texture.Reset(
                ResourceFactory::CreateTextureResource(dxCommon_->GetDevice(), desc, initialState, clearValue),
                initialState);
        }
        catch (const std::exception&) {
            Logger::GetInstance().Errorf(LogCategory::Graphics, LogSubCategory::Pipeline,
                "WaterSpraySystem: failed to create {} ({}x{}).", name, width, height);
            return false;
        }
        const std::wstring wideName(name, name + std::char_traits<char>::length(name));
        texture.Get()->SetName(wideName.c_str());

        const std::string baseName(name);
        if ((flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0) {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = format;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            texture.dsv = UniqueDescriptor(*descriptorAllocator_,
                descriptorAllocator_->CreateDSV(texture.Get(), dsvDesc, baseName + "DSV"));
            return true;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        texture.srv = UniqueDescriptor(*descriptorAllocator_,
            descriptorAllocator_->CreateSRV(texture.Get(), srvDesc, baseName + "SRV"));

        if ((flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0) {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = format;
            uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            texture.uav = UniqueDescriptor(*descriptorAllocator_,
                descriptorAllocator_->CreateUAV(texture.Get(), uavDesc, baseName + "UAV"));
        }
        if ((flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0) {
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = format;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            texture.rtv = UniqueDescriptor(*descriptorAllocator_,
                descriptorAllocator_->CreateRTV(texture.Get(), rtvDesc, baseName + "RTV"));
        }
        return true;
    }

    void WaterSpraySystem::ReleaseScreenTexture(ScreenTexture& texture)
    {
        texture.srv.Reset();
        texture.uav.Reset();
        texture.rtv.Reset();
        texture.dsv.Reset();
        if (texture.IsValid()) {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource = texture.Get();
            texture.Release();
            dxCommon_->DeferRelease(std::move(resource));
        }
    }

    bool WaterSpraySystem::EnsureScreenTextures(uint32_t width, uint32_t height, DXGI_FORMAT sceneColorFormat)
    {
        if (width == screenWidth_ && height == screenHeight_ && sceneColorFormat == backgroundFormat_
            && splatDepth_.IsValid()) {
            return true;
        }
        ReleaseScreenTexture(splatDepth_);
        ReleaseScreenTexture(splatDepthBuffer_);
        ReleaseScreenTexture(smoothDepth_[0]);
        ReleaseScreenTexture(smoothDepth_[1]);
        ReleaseScreenTexture(thickness_);
        ReleaseScreenTexture(background_);
        screenWidth_ = 0;
        screenHeight_ = 0;

        D3D12_CLEAR_VALUE zeroColor{};
        zeroColor.Format = kSplatDepthFormat;
        D3D12_CLEAR_VALUE zeroThickness{};
        zeroThickness.Format = kThicknessFormat;
        D3D12_CLEAR_VALUE farDepth{};
        farDepth.Format = kSplatDepthBufferFormat;
        farDepth.DepthStencil.Depth = 1.0f;

        const bool created =
            CreateScreenTexture(splatDepth_, width, height, kSplatDepthFormat,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &zeroColor,
                "WaterSpraySplatDepth")
            && CreateScreenTexture(splatDepthBuffer_, width, height, kSplatDepthBufferFormat,
                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &farDepth, "WaterSpraySplatDepthBuffer")
            && CreateScreenTexture(smoothDepth_[0], width, height, kSplatDepthFormat,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                "WaterSpraySmoothDepth0")
            && CreateScreenTexture(smoothDepth_[1], width, height, kSplatDepthFormat,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                "WaterSpraySmoothDepth1")
            && CreateScreenTexture(thickness_, width, height, kThicknessFormat,
                D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &zeroThickness,
                "WaterSprayThickness")
            && CreateScreenTexture(background_, width, height, sceneColorFormat,
                D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                "WaterSprayBackground");
        if (!created) {
            return false;
        }
        screenWidth_ = width;
        screenHeight_ = height;
        backgroundFormat_ = sceneColorFormat;
        return true;
    }

    WaterSpraySystem::ViewConstants WaterSpraySystem::BuildViewConstants(
        const ViewInfo& view, uint32_t width, uint32_t height) const
    {
        ViewConstants constants{};
        constants.viewProjection = view.viewProjection;
        constants.prevViewProjection = hasPrevViewProjection_ ? prevViewProjection_ : view.viewProjection;
        constants.cameraPosition = view.position;
        const Matrix4x4& v = view.viewMatrix;
        constants.cameraRight = Vector3{ v.m[0][0], v.m[1][0], v.m[2][0] };
        constants.cameraUp = Vector3{ v.m[0][1], v.m[1][1], v.m[2][1] };
        constants.cameraForward = Vector3{ v.m[0][2], v.m[1][2], v.m[2][2] };
        constants.projScaleX = view.projection.m[0][0];
        constants.projScaleY = view.projection.m[1][1];
        constants.projZW = Vector2{ view.projection.m[2][2], view.projection.m[3][2] };
        constants.screenSize = Vector2{ static_cast<float>(width), static_cast<float>(height) };
        constants.waterAbsorption = Vector3{
            lighting_.waterAbsorption[0], lighting_.waterAbsorption[1], lighting_.waterAbsorption[2] };
        constants.skyAmbientScale = lighting_.skyAmbientScale;
        constants.skyAmbientEnabled =
            (lighting_.skyAmbientEnabled && lighting_.skyIrradianceSRV.ptr != 0) ? 1u : 0u;
        constants.skyEnvironmentEnabled =
            (lighting_.skyEnvironmentEnabled && lighting_.skyEnvironmentSRV.ptr != 0) ? 1u : 0u;
        return constants;
    }

    void WaterSpraySystem::BindViewResources(
        ID3D12GraphicsCommandList* cmdList, GraphicsProgram& program,
        D3D12_GPU_VIRTUAL_ADDRESS viewConstants, const RenderInput& input)
    {
        const auto setCBV = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
            const RootSlot slot = program.GetSlot(name);
            if (slot.IsValid() && address != 0) {
                cmdList->SetGraphicsRootConstantBufferView(slot.index, address);
            }
        };
        const auto setTable = [&](const char* name, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            const RootSlot slot = program.GetSlot(name);
            if (slot.IsValid() && handle.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(slot.index, handle);
            }
        };
        setCBV("WaterSprayViewConstants", viewConstants);
        setCBV("gLightCounts", input.lightCountsCBV);
        setTable("gDirectionalLights", input.directionalLightsSRV);
        setTable("gWaterSkyIrradianceSH", lighting_.skyIrradianceSRV);
        setTable("gSkyEnvironmentMap", lighting_.skyEnvironmentSRV);
        setTable("gSceneDepth", input.sceneDepthSRV);
        setTable("gParticles", particleSRV_.Get().gpuHandle);
    }

    void WaterSpraySystem::Render(ID3D12GraphicsCommandList* cmdList, const RenderInput& input)
    {
        if (!IsActive() || !simulated_ || !cmdList || !input.view || !input.sceneColor
            || !input.sceneColor->IsValid() || input.sceneColorRTV.ptr == 0 || input.sceneDepthSRV.ptr == 0) {
            return;
        }

        const D3D12_RESOURCE_DESC sceneDesc = input.sceneColor->Desc();
        const uint32_t width = static_cast<uint32_t>(sceneDesc.Width);
        const uint32_t height = sceneDesc.Height;
        if (width == 0 || height == 0 || sceneDesc.Format != kSceneColorFormat) {
            return;
        }

        const bool fluid = settings_.fluidRendering && input.motionVector && input.motionVector->IsValid()
            && input.motionVectorRTV.ptr != 0 && input.motionVectorFormat == compositeMotionVectorFormat_;
        if (fluid && !EnsureScreenTextures(width, height, sceneDesc.Format)) {
            return;
        }

        ViewConstants constants = BuildViewConstants(*input.view, width, height);
        constants.fluidRendering = fluid ? 1u : 0u;
        const D3D12_GPU_VIRTUAL_ADDRESS viewConstants = dxCommon_->GetUploadRing().AllocateConstants(constants);

        {
            BarrierBatch batch(cmdList);
            batch.Transition(particles_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(dropletList_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(mistList_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            batch.Transition(drawArgs_, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        }
        if (fluid) {
            // 生きているしぶきが 0 のフレームは、流体描画の描画・ディスパッチ・コピーを GPU が飛ばす
            cmdList->SetPredication(drawArgs_.Get(), kDrawArgsDropletCount * sizeof(uint32_t), D3D12_PREDICATION_OP_EQUAL_ZERO);
            RenderFluid(cmdList, input, viewConstants, width, height);
            cmdList->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        }
        RenderParticles(cmdList, input, viewConstants, width, height, fluid);

        prevViewProjection_ = input.view->viewProjection;
        hasPrevViewProjection_ = true;
    }

    void WaterSpraySystem::RenderFluid(
        ID3D12GraphicsCommandList* cmdList, const RenderInput& input,
        D3D12_GPU_VIRTUAL_ADDRESS viewConstants, uint32_t width, uint32_t height)
    {
        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        // ---- 一番手前の球の深度と、厚み・動きの和 ----
        {
            BarrierBatch batch(cmdList);
            batch.Transition(splatDepth_, D3D12_RESOURCE_STATE_RENDER_TARGET);
            batch.Transition(splatDepthBuffer_, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            batch.Transition(thickness_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE splatDepthRTV = splatDepth_.rtv.Get().cpuHandle;
        const D3D12_CPU_DESCRIPTOR_HANDLE splatDepthDSV = splatDepthBuffer_.dsv.Get().cpuHandle;
        const D3D12_CPU_DESCRIPTOR_HANDLE thicknessRTV = thickness_.rtv.Get().cpuHandle;
        cmdList->ClearRenderTargetView(splatDepthRTV, zero, 0, nullptr);
        cmdList->ClearDepthStencilView(splatDepthDSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        cmdList->ClearRenderTargetView(thicknessRTV, zero, 0, nullptr);

        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        cmdList->OMSetRenderTargets(1, &splatDepthRTV, FALSE, &splatDepthDSV);
        cmdList->SetPipelineState(splatDepthProgram_.GetPipelineState());
        cmdList->SetGraphicsRootSignature(splatDepthProgram_.GetRootSignature());
        BindViewResources(cmdList, splatDepthProgram_, viewConstants, input);
        DrawParticleList(cmdList, splatDepthProgram_, dropletListSRV_, kDrawArgsDroplet);

        cmdList->OMSetRenderTargets(1, &thicknessRTV, FALSE, nullptr);
        cmdList->SetPipelineState(splatThicknessProgram_.GetPipelineState());
        cmdList->SetGraphicsRootSignature(splatThicknessProgram_.GetRootSignature());
        BindViewResources(cmdList, splatThicknessProgram_, viewConstants, input);
        DrawParticleList(cmdList, splatThicknessProgram_, dropletListSRV_, kDrawArgsDroplet);

        // ---- 深度を 2 回ならす ----
        {
            SmoothConstants constants{};
            constants.screenSize[0] = width;
            constants.screenSize[1] = height;
            constants.pixelsPerMeter = 0.5f * input.view->projection.m[1][1] * static_cast<float>(height);
            constants.worldRadius = kSmoothWorldRadius;
            constants.depthFalloff = kSmoothDepthFalloff;

            cmdList->SetPipelineState(smoothPipeline_.GetComputePSO());
            cmdList->SetComputeRootSignature(smoothPipeline_.GetComputeRootSignature());
            const UINT groupsX = (width + kSmoothGroupSize - 1) / kSmoothGroupSize;
            const UINT groupsY = (height + kSmoothGroupSize - 1) / kSmoothGroupSize;

            ScreenTexture* const sources[2] = { &splatDepth_, &smoothDepth_[0] };
            for (uint32_t pass = 0; pass < 2; ++pass) {
                {
                    BarrierBatch batch(cmdList);
                    batch.Transition(*sources[pass], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    batch.Transition(smoothDepth_[pass], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                }
                SetComputeTable(cmdList, smoothPipeline_, "gDepthInput", sources[pass]->srv.Get().gpuHandle);
                SetComputeTable(cmdList, smoothPipeline_, "gDepthOutput", smoothDepth_[pass].uav.Get().gpuHandle);
                SetComputeCBV(cmdList, smoothPipeline_, "WaterSpraySmoothConstants",
                    dxCommon_->GetUploadRing().AllocateConstants(constants));
                cmdList->Dispatch(groupsX, groupsY, 1);
            }
        }

        // ---- 合成の前のシーンの色を写す ----
        {
            BarrierBatch batch(cmdList);
            batch.Transition(smoothDepth_[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Transition(thickness_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Transition(*input.sceneColor, D3D12_RESOURCE_STATE_COPY_SOURCE);
            batch.Transition(background_, D3D12_RESOURCE_STATE_COPY_DEST);
        }
        cmdList->CopyResource(background_.Get(), input.sceneColor->Get());
        {
            BarrierBatch batch(cmdList);
            batch.Transition(background_, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            batch.Transition(*input.sceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
            batch.Transition(*input.motionVector, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

        // ---- 水の面として合成する ----
        const D3D12_CPU_DESCRIPTOR_HANDLE compositeRTVs[2] = { input.sceneColorRTV, input.motionVectorRTV };
        cmdList->OMSetRenderTargets(2, compositeRTVs, FALSE, nullptr);
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->SetPipelineState(compositeProgram_.GetPipelineState());
        cmdList->SetGraphicsRootSignature(compositeProgram_.GetRootSignature());
        BindViewResources(cmdList, compositeProgram_, viewConstants, input);
        const auto setTable = [&](const char* name, D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            const RootSlot slot = compositeProgram_.GetSlot(name);
            if (slot.IsValid() && handle.ptr != 0) {
                cmdList->SetGraphicsRootDescriptorTable(slot.index, handle);
            }
        };
        setTable("gSprayDepth", smoothDepth_[1].srv.Get().gpuHandle);
        setTable("gSprayThickness", thickness_.srv.Get().gpuHandle);
        setTable("gSprayBackground", background_.srv.Get().gpuHandle);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }

    void WaterSpraySystem::RenderParticles(
        ID3D12GraphicsCommandList* cmdList, const RenderInput& input,
        D3D12_GPU_VIRTUAL_ADDRESS viewConstants, uint32_t width, uint32_t height, bool fluid)
    {
        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

        Barrier::Transition(cmdList, *input.sceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->OMSetRenderTargets(1, &input.sceneColorRTV, FALSE, nullptr);
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->SetPipelineState(particleProgram_.GetPipelineState());
        cmdList->SetGraphicsRootSignature(particleProgram_.GetRootSignature());
        BindViewResources(cmdList, particleProgram_, viewConstants, input);
        if (!fluid) {
            DrawParticleList(cmdList, particleProgram_, dropletListSRV_, kDrawArgsDroplet);
        }
        DrawParticleList(cmdList, particleProgram_, mistListSRV_, kDrawArgsMist);
    }

    void WaterSpraySystem::DrawParticleList(
        ID3D12GraphicsCommandList* cmdList, GraphicsProgram& program,
        const UniqueDescriptor& listSRV, uint32_t argsIndex)
    {
        const RootSlot slot = program.GetSlot("gSprayIndices");
        if (slot.IsValid()) {
            cmdList->SetGraphicsRootDescriptorTable(slot.index, listSRV.Get().gpuHandle);
        }
        cmdList->ExecuteIndirect(
            drawSignature_.Get(), 1, drawArgs_.Get(), argsIndex * sizeof(uint32_t), nullptr, 0);
    }
}
