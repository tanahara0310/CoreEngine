#include "IBLGenerator.h"
#include "Engine/Graphics/Common/DirectXCommon.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Utility/Logger/Logger.h"
#include "externals/DirectXTex/d3dx12.h"
#include "externals/DirectXTex/DirectXTex.h"

#include <cassert>
#include <format>

namespace CoreEngine
{

void IBLGenerator::Initialize(DirectXCommon* dxCommon, ShaderCompiler* shaderCompiler)
{
    // パラメータのnullptrチェック
    if (!dxCommon || !shaderCompiler)
    {
        Logger::GetInstance().Log("IBLGenerator::Initialize: Invalid parameters (dxCommon or shaderCompiler is null)",
                                  LogLevel::Error, LogCategory::Graphics);
        throw std::invalid_argument("dxCommon and shaderCompiler must not be null");
    }

    dxCommon_ = dxCommon;
    shaderCompiler_ = shaderCompiler;

    Logger::GetInstance().Log("Initializing IBLGenerator...", LogLevel::INFO, LogCategory::Graphics);

    try
    {
        CreateBRDFLUTRootSignature();
        CreateBRDFLUTPipeline();
        CreateIrradianceRootSignature();
        CreateIrradiancePipeline();
        CreatePrefilteredRootSignature();
        CreatePrefilteredPipeline();
        
        Logger::GetInstance().Log("IBLGenerator initialized successfully", LogLevel::INFO, LogCategory::Graphics);
    }
    catch (const std::exception& e)
    {
        Logger::GetInstance().Log(std::format("IBLGenerator initialization failed: {}", e.what()),
                                  LogLevel::Error, LogCategory::Graphics);
        throw;
    }
}

void IBLGenerator::CreateBRDFLUTRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 rootParams[1];
    rootParams[0].InitAsDescriptorTable(1, &uavRange);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(1, rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error);

    if (FAILED(hr))
    {
        if (error)
        {
            Logger::GetInstance().Log(
                std::format("Failed to serialize BRDF LUT root signature: {}",
                    static_cast<char*>(error->GetBufferPointer())),
                LogLevel::Error,
                LogCategory::Graphics);
        }
        assert(false);
    }

    hr = dxCommon_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&brdfLutRootSignature_));

    assert(SUCCEEDED(hr));
}

void IBLGenerator::CreateBRDFLUTPipeline()
{
    IDxcBlob* computeShader = shaderCompiler_->CompileShader(
        L"Assets/Shaders/IBL/BRDFLUT.CS.hlsl",
        L"cs_6_0");

    assert(computeShader != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = brdfLutRootSignature_.Get();
    psoDesc.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };

    HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
        &psoDesc,
        IID_PPV_ARGS(&brdfLutPSO_));

    assert(SUCCEEDED(hr));

    computeShader->Release();
}

void IBLGenerator::CreateIrradianceRootSignature()
{
    // ルートパラメータ配列
    // [0]: SRV (入力環境マップ)
    // [1]: UAV (出力Irradianceマップ)
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 rootParams[2];
    rootParams[0].InitAsDescriptorTable(1, &srvRange);
    rootParams[1].InitAsDescriptorTable(1, &uavRange);

    // サンプラー（LinearClamp - キューブマップ用）
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(2, rootParams, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error);

    if (FAILED(hr))
    {
        if (error)
        {
            Logger::GetInstance().Log(
                std::format("Failed to serialize Irradiance root signature: {}",
                    static_cast<char*>(error->GetBufferPointer())),
                LogLevel::Error,
                LogCategory::Graphics);
        }
        assert(false);
    }

    hr = dxCommon_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&irradianceRootSignature_));

    assert(SUCCEEDED(hr));
}

void IBLGenerator::CreateIrradiancePipeline()
{
    IDxcBlob* computeShader = shaderCompiler_->CompileShader(
        L"Assets/Shaders/IBL/IrradianceConvolution.CS.hlsl",
        L"cs_6_0");

    assert(computeShader != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = irradianceRootSignature_.Get();
    psoDesc.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };

    HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
        &psoDesc,
        IID_PPV_ARGS(&irradiancePSO_));

    assert(SUCCEEDED(hr));

    computeShader->Release();
}

Microsoft::WRL::ComPtr<ID3D12Resource> IBLGenerator::CreateUAVCubemap(
    uint32_t size,
    DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = 6; // キューブマップ = 6面
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&resource));

    assert(SUCCEEDED(hr));
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> IBLGenerator::CreateUAVTexture(
    uint32_t width,
    uint32_t height,
    DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&resource));

    assert(SUCCEEDED(hr));
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> IBLGenerator::CreateDescriptorHeap(
    D3D12_DESCRIPTOR_HEAP_TYPE type,
    uint32_t numDescriptors)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
    HRESULT hr = dxCommon_->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
    assert(SUCCEEDED(hr));
    return heap;
}

Microsoft::WRL::ComPtr<ID3D12Resource> IBLGenerator::GenerateBRDFLUT(uint32_t size)
{
    Logger::GetInstance().Log(
        std::format("Generating BRDF LUT ({}x{})...", size, size),
        LogLevel::INFO,
        LogCategory::Graphics);

    // UAVテクスチャ作成（RG16F）
    auto brdfLUT = CreateUAVTexture(size, size, DXGI_FORMAT_R16G16_FLOAT);

    // ディスクリプタヒープ作成
    auto uavHeap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    // UAVビュー作成
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    dxCommon_->GetDevice()->CreateUnorderedAccessView(
        brdfLUT.Get(),
        nullptr,
        &uavDesc,
        uavHeap->GetCPUDescriptorHandleForHeapStart());

    // コマンドリスト取得
    auto commandList = dxCommon_->GetCommandList();

    // Compute Shaderを実行
    commandList->SetPipelineState(brdfLutPSO_.Get());
    commandList->SetComputeRootSignature(brdfLutRootSignature_.Get());

    ID3D12DescriptorHeap* heaps[] = { uavHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootDescriptorTable(0, uavHeap->GetGPUDescriptorHandleForHeapStart());

    // ディスパッチ（8x8スレッドグループ）
    uint32_t dispatchX = (size + 7) / 8;
    uint32_t dispatchY = (size + 7) / 8;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    // UAV→SRVバリア（全サブリソース）
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = brdfLUT.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // コマンドリストをClose
    HRESULT hr = commandList->Close();
    assert(SUCCEEDED(hr));

    // コマンドを実行
    ID3D12CommandList* commandLists[] = { commandList };
    dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);

    // GPU完了を待機
    dxCommon_->WaitForPreviousFrame();

    // コマンドリストをリセット（他のシステムが使用できるように）
    hr = dxCommon_->GetCommandAllocator()->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList->Reset(dxCommon_->GetCommandAllocator(), nullptr);
    assert(SUCCEEDED(hr));

    Logger::GetInstance().Log("BRDF LUT generated successfully", LogLevel::INFO, LogCategory::Graphics);

    return brdfLUT;
}

bool IBLGenerator::SaveBRDFLUT(ID3D12Resource* brdfLUT, const std::string& outputPath)
{
    if (!brdfLUT)
        return false;

    Logger::GetInstance().Log(
        std::format("Saving BRDF LUT to: {}", outputPath),
        LogLevel::INFO,
        LogCategory::Graphics);

    // Readbackバッファ作成
    D3D12_RESOURCE_DESC desc = brdfLUT->GetDesc();
    
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;
    dxCommon_->GetDevice()->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, &totalBytes);

    D3D12_HEAP_PROPERTIES readbackHeapProps = {};
    readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &readbackHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer));

    if (FAILED(hr))
        return false;

    // コピーコマンド
    auto commandList = dxCommon_->GetCommandList();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = brdfLUT;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = brdfLUT;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readbackBuffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);

    // コマンドリストをClose
    hr = commandList->Close();
    assert(SUCCEEDED(hr));

    // コマンドを実行
    ID3D12CommandList* commandLists[] = { commandList };
    dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);

    // GPU完了を待機
    dxCommon_->WaitForPreviousFrame();

    // コマンドリストをリセット（他のシステムが使用できるように）
    hr = dxCommon_->GetCommandAllocator()->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList->Reset(dxCommon_->GetCommandAllocator(), nullptr);
    assert(SUCCEEDED(hr));

    // データ読み込み
    void* mappedData = nullptr;
    hr = readbackBuffer->Map(0, nullptr, &mappedData);
    if (FAILED(hr))
        return false;

    // DirectXTexでDDS保存
    DirectX::Image image;
    image.width = static_cast<size_t>(desc.Width);
    image.height = static_cast<size_t>(desc.Height);
    image.format = desc.Format;
    image.rowPitch = static_cast<size_t>(layout.Footprint.RowPitch);
    image.slicePitch = static_cast<size_t>(totalBytes);
    image.pixels = static_cast<uint8_t*>(mappedData);

    DirectX::ScratchImage scratchImage;
    hr = scratchImage.InitializeFromImage(image);
    if (SUCCEEDED(hr))
    {
        std::wstring wOutputPath = Logger::GetInstance().ConvertString(outputPath);
        hr = DirectX::SaveToDDSFile(
            scratchImage.GetImages(),
            scratchImage.GetImageCount(),
            scratchImage.GetMetadata(),
            DirectX::DDS_FLAGS_NONE,
            wOutputPath.c_str());
    }

    readbackBuffer->Unmap(0, nullptr);

    if (SUCCEEDED(hr))
    {
        Logger::GetInstance().Log("BRDF LUT saved successfully", LogLevel::INFO, LogCategory::Graphics);
        return true;
    }

    Logger::GetInstance().Log("Failed to save BRDF LUT", LogLevel::Error, LogCategory::Graphics);
    return false;
}

Microsoft::WRL::ComPtr<ID3D12Resource> IBLGenerator::LoadBRDFLUT(const std::string& filePath)
{
    Logger::GetInstance().Log(
        std::format("Loading BRDF LUT from: {}", filePath),
        LogLevel::INFO,
        LogCategory::Graphics);

    std::wstring wFilePath = Logger::GetInstance().ConvertString(filePath);

    DirectX::ScratchImage image;
    HRESULT hr = DirectX::LoadFromDDSFile(wFilePath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);

    if (FAILED(hr))
    {
        Logger::GetInstance().Log("Failed to load BRDF LUT", LogLevel::WARNING, LogCategory::Graphics);
        return nullptr;
    }

    // テクスチャリソース作成
    const DirectX::TexMetadata& metadata = image.GetMetadata();
    
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = static_cast<UINT>(metadata.width);
    desc.Height = static_cast<UINT>(metadata.height);
    desc.DepthOrArraySize = 1;
    desc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
    desc.Format = metadata.format;
    desc.SampleDesc.Count = 1;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));

    if (FAILED(hr))
        return nullptr;

    // アップロード処理（省略: TextureManagerと同様の処理）
    // 実際のプロジェクトではTextureManagerのLoad処理を使用することを推奨

    Logger::GetInstance().Log("BRDF LUT loaded successfully", LogLevel::INFO, LogCategory::Graphics);
    return texture;
}

Microsoft::WRL::ComPtr<ID3D12Resource> IBLGenerator::GenerateIrradianceMap(
    ID3D12Resource* environmentMap,
    uint32_t size)
{
    if (!environmentMap)
    {
        Logger::GetInstance().Log("Invalid environment map", LogLevel::Error, LogCategory::Graphics);
        return nullptr;
    }

    Logger::GetInstance().Log(
        std::format("Generating Irradiance Map ({}x{})...", size, size),
        LogLevel::INFO,
        LogCategory::Graphics);

    // UAVキューブマップ作成（RGBA16F）
    auto irradianceMap = CreateUAVCubemap(size, DXGI_FORMAT_R16G16B16A16_FLOAT);

    // ディスクリプタヒープ作成（SRV + UAV）
    auto heap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);

    auto device = dxCommon_->GetDevice();
    uint32_t descriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heap->GetGPUDescriptorHandleForHeapStart();

    // SRVビュー作成（入力環境マップ）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = environmentMap->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(environmentMap, &srvDesc, cpuHandle);

    // UAVビュー作成（出力Irradianceマップ）
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    uavDesc.Texture2DArray.MipSlice = 0;
    uavDesc.Texture2DArray.FirstArraySlice = 0;
    uavDesc.Texture2DArray.ArraySize = 6; // 6面

    cpuHandle.ptr += descriptorSize;
    gpuHandle.ptr += descriptorSize;
    device->CreateUnorderedAccessView(irradianceMap.Get(), nullptr, &uavDesc, cpuHandle);

    // コマンドリスト取得
    auto commandList = dxCommon_->GetCommandList();

    // Compute Shaderを実行
    commandList->SetPipelineState(irradiancePSO_.Get());
    commandList->SetComputeRootSignature(irradianceRootSignature_.Get());

    ID3D12DescriptorHeap* heaps[] = { heap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    // ルートパラメータ設定
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = heap->GetGPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle = srvGpuHandle;
    uavGpuHandle.ptr += descriptorSize;

    commandList->SetComputeRootDescriptorTable(0, srvGpuHandle); // SRV
    commandList->SetComputeRootDescriptorTable(1, uavGpuHandle); // UAV

    // ディスパッチ（8x8スレッドグループ、6面分）
    uint32_t dispatchX = (size + 7) / 8;
    uint32_t dispatchY = (size + 7) / 8;
    uint32_t dispatchZ = 6; // キューブマップの6面
    commandList->Dispatch(dispatchX, dispatchY, dispatchZ);

    // UAV→SRVバリア（全サブリソース = 6面すべて）
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = irradianceMap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // コマンドリストをClose
    HRESULT hr = commandList->Close();
    assert(SUCCEEDED(hr));

    // コマンドを実行
    ID3D12CommandList* commandLists[] = { commandList };
    dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);

    // GPU完了を待機
    dxCommon_->WaitForPreviousFrame();

    // コマンドリストをリセット（他のシステムが使用できるように）
    hr = dxCommon_->GetCommandAllocator()->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList->Reset(dxCommon_->GetCommandAllocator(), nullptr);
    assert(SUCCEEDED(hr));

    Logger::GetInstance().Log("Irradiance Map generated successfully", LogLevel::INFO, LogCategory::Graphics);

    return irradianceMap;
}

// ===================================================================
// Prefiltered Environment Map生成
// ===================================================================

void IBLGenerator::CreatePrefilteredRootSignature()
{
    // ルートパラメータ配列
    // [0]: SRV (入力環境マップ)
    // [1]: UAV (出力Prefilteredマップ)
    // [2]: CBV (roughness, mipLevel, outputSize)
    CD3DX12_DESCRIPTOR_RANGE1 srvRange;
    srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE1 uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    CD3DX12_ROOT_PARAMETER1 rootParams[3];
    rootParams[0].InitAsDescriptorTable(1, &srvRange);  // t0: EnvironmentMap
    rootParams[1].InitAsDescriptorTable(1, &uavRange);  // u0: PrefilteredMap
    rootParams[2].InitAsConstants(4, 0);                 // b0: PrefilteredParams (4 x 32bit)

    // サンプラー（LinearClamp - キューブマップ用）
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(3, rootParams, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error);

    if (FAILED(hr))
    {
        if (error)
        {
            Logger::GetInstance().Log(
                std::format("Failed to serialize Prefiltered root signature: {}",
                    static_cast<char*>(error->GetBufferPointer())),
                LogLevel::Error,
                LogCategory::Graphics);
        }
        assert(false);
    }

    hr = dxCommon_->GetDevice()->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&prefilteredRootSignature_));

    assert(SUCCEEDED(hr));
}

void IBLGenerator::CreatePrefilteredPipeline()
{
    IDxcBlob* computeShader = shaderCompiler_->CompileShader(
        L"Assets/Shaders/IBL/PrefilterEnvironment.CS.hlsl",
        L"cs_6_0");

    assert(computeShader != nullptr);

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = prefilteredRootSignature_.Get();
    psoDesc.CS = { computeShader->GetBufferPointer(), computeShader->GetBufferSize() };

    HRESULT hr = dxCommon_->GetDevice()->CreateComputePipelineState(
        &psoDesc,
        IID_PPV_ARGS(&prefilteredPSO_));

    assert(SUCCEEDED(hr));

    computeShader->Release();
}

Microsoft::WRL::ComPtr<ID3D12Resource> IBLGenerator::CreateUAVCubemapWithMips(
    uint32_t size,
    uint32_t mipLevels,
    DXGI_FORMAT format)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size;
    desc.Height = size;
    desc.DepthOrArraySize = 6; // キューブマップ = 6面
    desc.MipLevels = static_cast<UINT16>(mipLevels);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    HRESULT hr = dxCommon_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&resource));

    assert(SUCCEEDED(hr));
    return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource> IBLGenerator::GeneratePrefilteredEnvironmentMap(
    ID3D12Resource* environmentMap,
    uint32_t size)
{
    if (!environmentMap)
    {
        Logger::GetInstance().Log("Invalid environment map", LogLevel::Error, LogCategory::Graphics);
        return nullptr;
    }

    Logger::GetInstance().Log(
        std::format("Generating Prefiltered Environment Map ({}x{}, 5 mips)...", size, size),
        LogLevel::INFO,
        LogCategory::Graphics);

    // ミップマップ5レベルのキューブマップ作成
    // mip0: 128x128 (roughness=0.0)
    // mip1: 64x64   (roughness=0.25)
    // mip2: 32x32   (roughness=0.5)
    // mip3: 16x16   (roughness=0.75)
    // mip4: 8x8     (roughness=1.0)
    const uint32_t mipLevels = 5;
    auto prefilteredMap = CreateUAVCubemapWithMips(size, mipLevels, DXGI_FORMAT_R16G16B16A16_FLOAT);

    auto device = dxCommon_->GetDevice();
    auto commandList = dxCommon_->GetCommandList();

    // ディスクリプタヒープ作成（SRV + UAV x 5ミップレベル）
    auto heap = CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 6);

    uint32_t descriptorSize = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heap->GetGPUDescriptorHandleForHeapStart();

    // SRVビュー作成（入力環境マップ）
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = environmentMap->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = static_cast<UINT>(-1); // すべてのミップ
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    device->CreateShaderResourceView(environmentMap, &srvDesc, cpuHandle);

    // 各ミップレベルでプリフィルタリング
    for (uint32_t mip = 0; mip < mipLevels; ++mip)
    {
        uint32_t mipSize = size >> mip; // ミップサイズ = size / 2^mip
        float roughness = static_cast<float>(mip) / static_cast<float>(mipLevels - 1);

        // UAVビュー作成（特定のミップレベル）
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        uavDesc.Texture2DArray.MipSlice = mip;
        uavDesc.Texture2DArray.FirstArraySlice = 0;
        uavDesc.Texture2DArray.ArraySize = 6; // 6面

        D3D12_CPU_DESCRIPTOR_HANDLE uavCpuHandle = cpuHandle;
        uavCpuHandle.ptr += descriptorSize * (1 + mip);
        device->CreateUnorderedAccessView(prefilteredMap.Get(), nullptr, &uavDesc, uavCpuHandle);

        // Compute Shader実行
        commandList->SetPipelineState(prefilteredPSO_.Get());
        commandList->SetComputeRootSignature(prefilteredRootSignature_.Get());

        ID3D12DescriptorHeap* heaps[] = { heap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);

        // ルートパラメータ設定
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle = gpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE uavGpuHandle = gpuHandle;
        uavGpuHandle.ptr += descriptorSize * (1 + mip);

        commandList->SetComputeRootDescriptorTable(0, srvGpuHandle); // SRV
        commandList->SetComputeRootDescriptorTable(1, uavGpuHandle); // UAV

        // Constants: roughness, mipLevel, outputSize.x, outputSize.y
        uint32_t constants[4] = {
            *reinterpret_cast<uint32_t*>(&roughness),
            mip,
            mipSize,
            mipSize
        };
        commandList->SetComputeRoot32BitConstants(2, 4, constants, 0);

        // ディスパッチ（8x8スレッドグループ、6面分）
        uint32_t dispatchX = (mipSize + 7) / 8;
        uint32_t dispatchY = (mipSize + 7) / 8;
        uint32_t dispatchZ = 6; // キューブマップの6面
        commandList->Dispatch(dispatchX, dispatchY, dispatchZ);

        // UAVバリア（次のミップレベルの前に同期）
        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = prefilteredMap.Get();
        commandList->ResourceBarrier(1, &uavBarrier);
    }

    // UAV→SRVバリア（全サブリソース）
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = prefilteredMap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // コマンドリストをClose
    HRESULT hr = commandList->Close();
    assert(SUCCEEDED(hr));

    // コマンドを実行
    ID3D12CommandList* commandLists[] = { commandList };
    dxCommon_->GetCommandQueue()->ExecuteCommandLists(1, commandLists);

    // GPU完了を待機
    dxCommon_->WaitForPreviousFrame();

    // コマンドリストをリセット
    hr = dxCommon_->GetCommandAllocator()->Reset();
    assert(SUCCEEDED(hr));
    hr = commandList->Reset(dxCommon_->GetCommandAllocator(), nullptr);
    assert(SUCCEEDED(hr));

    Logger::GetInstance().Log("Prefiltered Environment Map generated successfully", LogLevel::INFO, LogCategory::Graphics);

    return prefilteredMap;
}

}

