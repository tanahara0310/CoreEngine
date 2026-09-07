#include "pch.h"
#include "CameraRig.h"

namespace CoreEngine
{
    bool CameraRig::Activate(const std::string& name, const CameraRigActivateOptions& options)
    {
        if (!activeRuntime_) {
            return false;
        }

        auto asset = library_.Get(name);
        if (!asset) {
            return false;
        }

        activeRuntime_->Activate(std::move(asset), options, name);
        return activeRuntime_->IsActive();
    }

    void CameraRig::Deactivate()
    {
        if (activeRuntime_) {
            activeRuntime_->Deactivate();
        }
    }

    bool CameraRig::IsActive()
    {
        return activeRuntime_ && activeRuntime_->IsActive();
    }

    std::string CameraRig::GetActiveName()
    {
        return activeRuntime_ ? activeRuntime_->GetName() : std::string{};
    }
}
