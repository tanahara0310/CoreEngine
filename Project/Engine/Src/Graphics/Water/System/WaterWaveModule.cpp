#include "pch.h"
#include "WaterWaveModule.h"

#include <algorithm>

#include "Graphics/Water/Surface/WaterSurfaceComponent.h"
#include "Graphics/Water/System/WaterFrameContext.h"
#include "Graphics/Water/System/WaterSettings.h"
#include "Graphics/Water/Wave/FFTOceanWaveModel.h"
#include "Graphics/Water/Wave/GerstnerWaveModel.h"

namespace CoreEngine
{
    namespace {
        constexpr size_t IndexOf(WaterWaveModelType type)
        {
            return static_cast<size_t>(type);
        }
    }

    WaterWaveModule::WaterWaveModule()
    {
        models_[IndexOf(WaterWaveModelType::Gerstner)] = std::make_unique<GerstnerWaveModel>();
        models_[IndexOf(WaterWaveModelType::FFTOcean)] = std::make_unique<FFTOceanWaveModel>();
    }

    WaterWaveModule::~WaterWaveModule() = default;

    void WaterWaveModule::Advance(float deltaSeconds, std::span<WaterSurfaceComponent* const> waterPlanes)
    {
        for (const std::unique_ptr<WaterWaveModel>& model : models_) {
            const bool drawn = std::any_of(waterPlanes.begin(), waterPlanes.end(),
                [&](const WaterSurfaceComponent* waterPlane) { return waterPlane->GetWaveModel() == model->GetType(); });
            model->Advance(deltaSeconds, drawn);
        }
    }

    const WaterWaveModel& WaterWaveModule::GetDrawnModel(const WaterSurfaceComponent& waterPlane) const
    {
        return *models_[IndexOf(waterPlane.GetWaveModel())];
    }

    float WaterWaveModule::GetFFTOceanTime() const
    {
        return models_[IndexOf(WaterWaveModelType::FFTOcean)]->GetTime();
    }

    void WaterWaveModule::ApplyModelSettings(RenderDomainContext& domain, const WaterWaveSettings& waves)
    {
        for (const std::unique_ptr<WaterWaveModel>& model : models_) {
            model->ApplySettings(domain, waves);
        }
    }

    void WaterWaveModule::ApplySettings(const WaterFrameContext& frame)
    {
        WaterSurfaceComponent& waterPlane = frame.waterPlane;
        for (const std::unique_ptr<WaterWaveModel>& model : models_) {
            model->ApplyToSurface(frame.domain, waterPlane);
        }

        // モデルの切り替えはシェーダーの作り直しを伴うので、変わったときだけ
        if (waterPlane.GetWaveModel() != frame.settings.waves.model) {
            waterPlane.SetWaveModel(frame.settings.waves.model);
        }

        ApplyModelSettings(frame.domain, frame.settings.waves);
    }

    void WaterWaveModule::ApplyToSecondary(const WaterFrameContext& frame) const
    {
        WaterSurfaceComponent& waterPlane = frame.waterPlane;
        models_[IndexOf(WaterWaveModelType::Gerstner)]->ApplyToSurface(frame.domain, waterPlane);
        if (waterPlane.GetWaveModel() != WaterWaveModelType::Gerstner) {
            waterPlane.SetWaveModel(WaterWaveModelType::Gerstner);
        }
    }

    void WaterWaveModule::Capture(
        RenderDomainContext& domain, const WaterSurfaceComponent& waterPlane, WaterSurfaceData& snapshot) const
    {
        GetDrawnModel(waterPlane).Capture(domain, waterPlane, snapshot);
    }

    void WaterWaveModule::Bind(RenderDomainContext& domain, WaterFrameBinding& binding) const
    {
        for (const std::unique_ptr<WaterWaveModel>& model : models_) {
            model->Bind(domain, binding);
        }
    }
}
