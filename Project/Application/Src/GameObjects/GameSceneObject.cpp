#include "pch.h"
#include "GameSceneObject.h"

#include "GameObject/Component/Render/MeshRendererComponent.h"
#include "GameObject/Component/Transform/TransformComponent.h"
#include "Graphics/Pipeline/PipelineStateManager.h"
#include "Utility/JsonManager/JsonManager.h"

#include <string>
#include <unordered_map>

namespace GameScene
{
    namespace
    {
        json SerializeParameters(const CoreEngine::IComponent& component)
        {
            if (const auto* transform = dynamic_cast<const CoreEngine::TransformComponent*>(&component)) {
                const auto& value = transform->Get();
                return {
                    { "translate", CoreEngine::JsonManager::Vector3ToJson(value.translate) },
                    { "rotate", CoreEngine::JsonManager::Vector3ToJson(value.rotate) },
                    { "scale", CoreEngine::JsonManager::Vector3ToJson(value.scale) }
                };
            }
            if (const auto* renderer = dynamic_cast<const CoreEngine::MeshRendererComponent*>(&component)) {
                return { { "blendMode", static_cast<int>(renderer->GetBlendMode()) } };
            }
            return component.OnSerialize();
        }

        void DeserializeParameters(CoreEngine::IComponent& component, const json& parameters)
        {
            if (auto* transform = dynamic_cast<CoreEngine::TransformComponent*>(&component)) {
                auto& value = transform->Get();
                value.translate = CoreEngine::JsonManager::SafeGetVector3(
                    parameters, "translate", value.translate);
                value.rotate = CoreEngine::JsonManager::SafeGetVector3(
                    parameters, "rotate", value.rotate);
                value.scale = CoreEngine::JsonManager::SafeGetVector3(
                    parameters, "scale", value.scale);
                value.TransferMatrix();
                return;
            }
            if (auto* renderer = dynamic_cast<CoreEngine::MeshRendererComponent*>(&component)) {
                const int blendMode = CoreEngine::JsonManager::SafeGet<int>(
                    parameters, "blendMode", static_cast<int>(renderer->GetBlendMode()));
                if (blendMode >= 0 && blendMode < static_cast<int>(CoreEngine::kBlendModeCount)) {
                    renderer->SetBlendMode(static_cast<CoreEngine::BlendMode>(blendMode));
                }
                return;
            }
            component.OnDeserialize(parameters);
        }
    }

    json GameSceneObject::OnSerialize() const
    {
        json result = {
            { "active", IsActive() },
            { "name", GetName() }
        };
        json components = json::array();
        for (const auto& component : GetAllComponents()) {
            if (!component) continue;
            json entry = {
                { "type", component->GetTypeName() },
                { "enabled", component->IsEnabled() }
            };
            const json parameters = SerializeParameters(*component);
            if (!parameters.empty()) {
                entry["parameters"] = parameters;
            }
            components.push_back(std::move(entry));
        }
        result["components"] = std::move(components);
        return result;
    }

    void GameSceneObject::OnDeserialize(const json& j)
    {
        SetActive(CoreEngine::JsonManager::SafeGet<bool>(j, "active", IsActive()));
        if (j.contains("name") && j["name"].is_string()) {
            SetName(j["name"].get<std::string>());
        }
        if (!j.contains("components") || !j["components"].is_array()) return;

        std::unordered_map<std::string, std::size_t> nextSearchIndex;
        const auto& currentComponents = GetAllComponents();
        for (const auto& entry : j["components"]) {
            if (!entry.is_object()) continue;
            const std::string type = CoreEngine::JsonManager::SafeGet<std::string>(
                entry, "type", "");
            if (type.empty()) continue;

            std::size_t& searchIndex = nextSearchIndex[type];
            for (; searchIndex < currentComponents.size(); ++searchIndex) {
                CoreEngine::IComponent* component = currentComponents[searchIndex].get();
                if (!component || type != component->GetTypeName()) continue;
                component->SetEnabled(CoreEngine::JsonManager::SafeGet<bool>(
                    entry, "enabled", component->IsEnabled()));
                if (entry.contains("parameters") && entry["parameters"].is_object()) {
                    DeserializeParameters(*component, entry["parameters"]);
                }
                ++searchIndex;
                break;
            }
        }
    }
}
