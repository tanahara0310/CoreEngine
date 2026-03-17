#include "IBLManager.h"
#include "IBLGenerator.h"
#include "Graphics/Common/DirectXCommon.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{

bool IBLManager::Initialize(DirectXCommon* dxCommon, IBLGenerator* iblGenerator, const InitParams& params)
{
    if (!dxCommon || !iblGenerator)
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "IBLManager::Initialize: Invalid parameters (dxCommon or iblGenerator is null)");
        return false;
    }

    if (!params.environmentMap)
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "IBLManager::Initialize: Invalid environment map");
        return false;
    }

    dxCommon_ = dxCommon;

    // ===== Irradiance Map生成 =====
    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Generating Irradiance Map...");
    irradianceMap_ = iblGenerator->GenerateIrradianceMap(params.environmentMap, params.irradianceSize);
    
    if (!irradianceMap_)
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to generate Irradiance Map");
        return false;
    }

    if (!CreateIrradianceSRV())
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to create Irradiance Map SRV");
        return false;
    }

    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Irradiance Map generated successfully");

    // ===== Prefiltered Environment Map生成 =====
    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Generating Prefiltered Environment Map...");
    prefilteredMap_ = iblGenerator->GeneratePrefilteredEnvironmentMap(params.environmentMap, params.prefilteredSize);

    if (!prefilteredMap_)
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to generate Prefiltered Map");
        return false;
    }

    if (!CreatePrefilteredSRV())
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to create Prefiltered Map SRV");
        return false;
    }

    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Prefiltered Map generated successfully");

    // ===== BRDF LUT生成 =====
    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "Generating BRDF LUT...");
    brdfLUT_ = iblGenerator->GenerateBRDFLUT(params.brdfLUTSize);

    if (!brdfLUT_)
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to generate BRDF LUT");
        return false;
    }

    if (!CreateBRDFLUTSRV())
    {
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "Failed to create BRDF LUT SRV");
        return false;
    }

    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "BRDF LUT generated successfully");

    isInitialized_ = true;
    Logger::GetInstance().Logf(LogLevel::INFO, LogCategory::Graphics, "{}", "IBL system initialized successfully");
    return true;
}

bool IBLManager::CreateIrradianceSRV()
{
    auto* descriptorManager = dxCommon_->GetDescriptorManager();
    if (!descriptorManager)
        return false;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.MostDetailedMip = 0;

    descriptorManager->CreateSRV(
        irradianceMap_.Get(),
        srvDesc,
        cpuHandle,
        irradianceSRV_,
        "IBL_IrradianceMap");

    return irradianceSRV_.ptr != 0;
}

bool IBLManager::CreatePrefilteredSRV()
{
    auto* descriptorManager = dxCommon_->GetDescriptorManager();
    if (!descriptorManager)
        return false;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = PREFILTERED_MIP_LEVELS;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    descriptorManager->CreateSRV(
        prefilteredMap_.Get(),
        srvDesc,
        cpuHandle,
        prefilteredSRV_,
        "IBL_PrefilteredMap");

    return prefilteredSRV_.ptr != 0;
}

bool IBLManager::CreateBRDFLUTSRV()
{
    auto* descriptorManager = dxCommon_->GetDescriptorManager();
    if (!descriptorManager)
        return false;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    descriptorManager->CreateSRV(
        brdfLUT_.Get(),
        srvDesc,
        cpuHandle,
        brdfLUTSRV_,
        "IBL_BRDF_LUT");

    return brdfLUTSRV_.ptr != 0;
}

} // namespace CoreEngine


