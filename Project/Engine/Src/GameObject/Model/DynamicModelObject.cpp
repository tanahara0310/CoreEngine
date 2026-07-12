#include "pch.h"
#include "DynamicModelObject.h"

namespace CoreEngine
{
    void DynamicModelObject::SetModelPath(const std::string& modelPath)
    {
        modelPath_ = modelPath;
    }

    const std::string& DynamicModelObject::GetDynamicModelPath() const
    {
        return modelPath_;
    }

    const char* DynamicModelObject::GetObjectName() const
    {
        return "DynamicModel";
    }

    std::string DynamicModelObject::GetModelPath() const
    {
        return modelPath_;
    }
}
