#include "pch.h"
#include "PhysicsMaterialComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"

#include <algorithm>

REFLECT_REGISTER(CoreEngine::PhysicsMaterialComponent)
COMPONENT_REGISTER(CoreEngine::PhysicsMaterialComponent)

namespace CoreEngine
{
    float PhysicsMaterialComponent::Combine(float a, float b, MaterialCombine rule)
    {
        switch (rule) {
        case MaterialCombine::Minimum:  return (std::min)(a, b);
        case MaterialCombine::Maximum:  return (std::max)(a, b);
        case MaterialCombine::Multiply: return a * b;
        case MaterialCombine::Average:
        default:                        return (a + b) * 0.5f;
        }
    }

    float PhysicsMaterialComponent::CombineRestitution(const PhysicsMaterialComponent* a,
                                                      const PhysicsMaterialComponent* b)
    {
        const float valueA = a ? a->GetRestitution() : kDefaultRestitution;
        const float valueB = b ? b->GetRestitution() : kDefaultRestitution;

        const MaterialCombine ruleA = a ? a->GetRestitutionCombine() : MaterialCombine::Maximum;
        const MaterialCombine ruleB = b ? b->GetRestitutionCombine() : MaterialCombine::Maximum;

        return Combine(valueA, valueB, (std::max)(ruleA, ruleB));
    }

    float PhysicsMaterialComponent::CombineFriction(const PhysicsMaterialComponent* a,
                                                   const PhysicsMaterialComponent* b)
    {
        const float valueA = a ? a->GetFriction() : kDefaultFriction;
        const float valueB = b ? b->GetFriction() : kDefaultFriction;

        const MaterialCombine ruleA = a ? a->GetFrictionCombine() : MaterialCombine::Multiply;
        const MaterialCombine ruleB = b ? b->GetFrictionCombine() : MaterialCombine::Multiply;

        return Combine(valueA, valueB, (std::max)(ruleA, ruleB));
    }
}
