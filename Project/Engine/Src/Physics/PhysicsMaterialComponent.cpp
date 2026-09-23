#include "pch.h"
#include "PhysicsMaterialComponent.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "Reflection/PropertyDescriptor.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <string>

REFLECT_REGISTER(CoreEngine::PhysicsMaterialComponent)
COMPONENT_REGISTER(CoreEngine::PhysicsMaterialComponent)

namespace CoreEngine
{
    void PhysicsMaterialComponent::Start()
    {
        LoadFromAsset();
    }

    void PhysicsMaterialComponent::OnPropertyChanged(const Reflection::PropertyDescriptor& property)
    {
        if (property.type == Reflection::PropertyType::AssetRef) {
            LoadFromAsset();
        }
    }

    void PhysicsMaterialComponent::SetMaterialAsset(std::string_view pathOrName)
    {
        materialAsset_.SetPath(pathOrName);
        LoadFromAsset();
    }

    void PhysicsMaterialComponent::LoadFromAsset()
    {
        if (!materialAsset_.IsSet()) {
            return;
        }

        const std::string path = materialAsset_.GetPath();
        const json document = JsonManager::GetInstance().LoadJson(path);
        if (!document.is_object()) {
            Logger::GetInstance().Warnf(LogCategory::System,
                "PhysicsMaterial: 材質ファイルを読めません: {}", path);
            return;
        }

        const auto readFloat = [&document](const char* key, float& target) {
            const auto found = document.find(key);
            if (found != document.end() && found->is_number()) {
                target = found->get<float>();
            }
        };
        const auto readCombine = [&document](const char* key, MaterialCombine& target) {
            const auto found = document.find(key);
            if (found == document.end() || !found->is_number_integer()) {
                return;
            }
            const int value = found->get<int>();
            if (value >= 0 && value < static_cast<int>(MaterialCombine::Count)) {
                target = static_cast<MaterialCombine>(value);
            }
        };

        readFloat("restitution", restitution_);
        readFloat("friction", friction_);
        readCombine("restitutionCombine", restitutionCombine_);
        readCombine("frictionCombine", frictionCombine_);
    }

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
