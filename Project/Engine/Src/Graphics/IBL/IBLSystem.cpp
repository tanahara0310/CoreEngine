#include "IBLSystem.h"
#include "IBLManager.h"
#include "Graphics/Render/RenderManager.h"
#include "Utility/Logger/Logger.h"

namespace CoreEngine
{
    bool IBLSystem::Initialize(DirectXCommon* dxCommon, IBLGenerator* iblGenerator, RenderManager* renderManager)
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

    bool IBLSystem::Setup(const SetupParams& params)
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

            // SetupParamsはIBLManager::InitParamsのサブクラスなので直接渡せる
            if (!nextManager->Initialize(dxCommon_, iblGenerator_, params))
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
}
