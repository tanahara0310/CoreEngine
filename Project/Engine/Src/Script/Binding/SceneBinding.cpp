#include "pch.h"
#include "Script/Binding/SceneBinding.h"

#include "EngineSystem/EngineSystem.h"
#include "Scene/SceneManager.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Utility/Logger/Logger.h"

#include <string>

namespace CoreEngine::Script
{
    namespace
    {
        /// シーンマネージャを引く先（登録時に受け取る）
        EngineSystem* sEngineSystem = nullptr;

        SceneManager* CurrentSceneManager()
        {
            return sEngineSystem ? sEngineSystem->GetSceneManager() : nullptr;
        }

        void ChangeScene(const std::string& name)
        {
            SceneManager* const manager = CurrentSceneManager();
            if (!manager) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "シーンマネージャが無いので、シーン {} へ切り替えられません", name);
                return;
            }
            if (!manager->HasScene(name)) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                    "シーン {} は登録されていないので、切り替えられません", name);
                return;
            }
            manager->ChangeScene(name);
        }

        std::string GetCurrentName()
        {
            const SceneManager* const manager = CurrentSceneManager();
            return manager ? manager->GetCurrentSceneName() : std::string();
        }
    }

    bool RegisterSceneBinding(asIScriptEngine* engine, EngineSystem* engineSystem)
    {
        if (!engine) {
            return false;
        }
        sEngineSystem = engineSystem;

        BindingRegistrar r(engine);
        r.Namespace("Scene");
        r.Function("void ChangeScene(const string &in name)", asFUNCTION(ChangeScene));
        r.Function("string GetCurrentName()", asFUNCTION(GetCurrentName));
        r.Namespace("");
        return r.Succeeded();
    }
}
