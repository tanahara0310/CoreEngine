#include "pch.h"
#include "PostEffectBase.h"
#include "Graphics/Shader/ShaderReflectionData.h"


namespace CoreEngine
{
    int PostEffectBase::GetRootParamIndex(const std::string& resourceName) const {
        if (!reflectionData_) {
            return -1;
        }
        return reflectionData_->GetRootParameterIndexByName(resourceName);
    }
}
