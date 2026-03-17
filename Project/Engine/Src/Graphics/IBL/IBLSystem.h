#pragma once

#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <unordered_map>

#include "Graphics/IBL/IBLManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    class DirectXCommon;
    class IBLGenerator;
    class RenderManager;

    class IBLSystem
    {
    public:
        struct SetupParams
        {
            ID3D12Resource* environmentMap = nullptr;
            D3D12_GPU_DESCRIPTOR_HANDLE environmentMapSRV = {};
            std::string environmentKey;
            uint32_t irradianceSize = 128;
            uint32_t prefilteredSize = 256;
            uint32_t brdfLUTSize = 512;
            bool forceRegenerate = false;
        };

        bool Initialize(DirectXCommon* dxCommon, IBLGenerator* iblGenerator, RenderManager* renderManager)
        {
            if (!dxCommon || !iblGenerator || !renderManager)
            {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "IBLSystem::Initialize: Invalid parameters");
                return false;
            }

            dxCommon_ = dxCommon;
            iblGenerator_ = iblGenerator;
            renderManager_ = renderManager;

            return true;
        }

        bool Setup(const SetupParams& params)
        {
            if (!dxCommon_ || !iblGenerator_ || !renderManager_)
            {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "IBLSystem::Setup: System is not initialized");
                return false;
            }

            renderManager_->SetEnvironmentMap(params.environmentMapSRV);

            if (!params.environmentMap)
            {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "IBLSystem::Setup: environmentMap is null");
                return false;
            }

            std::string environmentKey = params.environmentKey;
            if (environmentKey.empty())
            {
                environmentKey = "ptr_" + std::to_string(reinterpret_cast<uintptr_t>(params.environmentMap));
            }

            const std::string cacheKey =
                environmentKey
                + "|irr_" + std::to_string(params.irradianceSize)
                + "|pref_" + std::to_string(params.prefilteredSize)
                + "|brdf_" + std::to_string(params.brdfLUTSize);

            const bool cacheHit = (!params.forceRegenerate) && iblCache_.contains(cacheKey);

            if (!cacheHit)
            {
                auto nextManager = std::make_unique<IBLManager>();

                IBLManager::InitParams initParams;
                initParams.environmentMap = params.environmentMap;
                initParams.irradianceSize = params.irradianceSize;
                initParams.prefilteredSize = params.prefilteredSize;
                initParams.brdfLUTSize = params.brdfLUTSize;

                if (!nextManager->Initialize(dxCommon_, iblGenerator_, initParams))
                {
                    Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Graphics, "{}", "IBLSystem::Setup: Failed to initialize IBLManager");
                    return false;
                }

                iblCache_[cacheKey] = std::move(nextManager);
            }

            activeCacheKey_ = cacheKey;
            iblManager_ = iblCache_[activeCacheKey_].get();

            const auto handles = iblManager_->GetSRVHandles();
            renderManager_->SetIBLMaps(handles.irradiance, handles.prefiltered, handles.brdfLUT);

            return true;
        }

    private:
        DirectXCommon* dxCommon_ = nullptr;
        IBLGenerator* iblGenerator_ = nullptr;
        RenderManager* renderManager_ = nullptr;

        std::unordered_map<std::string, std::unique_ptr<IBLManager>> iblCache_;
        std::string activeCacheKey_;
        IBLManager* iblManager_ = nullptr;
    };
}
