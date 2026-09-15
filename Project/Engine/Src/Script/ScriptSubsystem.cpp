#include "pch.h"
#include "Script/ScriptSubsystem.h"

#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Input/InputManager.h"
#include "Script/ScriptComponent.h"
#include "Script/ScriptComponentType.h"
#include "Script/ScriptHost.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kScriptRoot = "Application/Assets/Scripts";
#ifdef USE_IMGUI
        constexpr const char* kPredefinedFileName = "as.predefined";
#endif
    }

    ScriptSubsystem::ScriptSubsystem() = default;

    ScriptSubsystem::~ScriptSubsystem() = default;

    void ScriptSubsystem::Initialize(EngineSystem* engine, const EngineConfig& /*config*/)
    {
        ScriptServices services;
        services.input = engine ? engine->GetService<InputManager>() : nullptr;

        auto host = std::make_unique<ScriptHost>();
        if (!host->Initialize(services)) {
            return;
        }
        host_ = std::move(host);

#ifdef USE_IMGUI
        host_->WritePredefined(ProjectPaths::Resolve(kScriptRoot) / kPredefinedFileName);
#endif

        if (!host_->Build(ProjectPaths::Resolve(kScriptRoot))) {
            return;
        }

        ComponentFactory& factory = ComponentFactory::Get();
        for (const std::unique_ptr<ScriptComponentType>& type : host_->GetTypes()) {
            const ScriptComponentType* const raw = type.get();
            const bool registered = factory.RegisterRuntime(
                raw->GetName(),
                [raw]() -> std::unique_ptr<IComponent> { return std::make_unique<ScriptComponent>(*raw); },
                &raw->GetDescriptor(),
                raw->GetDisplayName());
            if (!registered) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                    "スクリプトのクラス {} は、同じ名前のコンポーネントが既にあるので使えません", raw->GetName());
            }
        }
    }

    void ScriptSubsystem::Finalize()
    {
        ComponentFactory::Get().UnregisterRuntimeTypes();
        if (host_) {
            host_->Shutdown();
            host_.reset();
        }
    }

    void ScriptSubsystem::EndFrame()
    {
        if (host_) {
            host_->CollectGarbageStep();
        }
    }
}
