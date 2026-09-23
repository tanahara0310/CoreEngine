#include "pch.h"
#include "GameObject/Component/Core/MissingComponent.h"

#include <utility>

namespace CoreEngine
{
    MissingComponent::MissingComponent(std::string typeName)
        : typeName_(std::move(typeName))
    {
    }
}
