#include "pch.h"
#include "Script/ScriptSubsystem.h"

#include "Audio/AudioSystem.h"
#include "EngineSystem/EngineSystem.h"
#include "GameObject/Component/Core/ComponentFactory.h"
#include "Input/InputManager.h"
#include "Script/ScriptComponent.h"
#include "Script/ScriptComponentType.h"
#include "Script/ScriptHost.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Path/ProjectPaths.h"

#ifdef USE_IMGUI
#include <chrono>
#include <vector>
#endif

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kScriptRoot = "Application/Assets/Scripts";
#ifdef USE_IMGUI
        constexpr const char* kPredefinedFileName = "as.predefined";

        /// 変更が落ち着いたと見なすまでの時間（エディタは 1 回の保存で何度も変更を出す）
        constexpr std::chrono::milliseconds kSettleTime{ 200 };
#endif
    }

    ScriptSubsystem::ScriptSubsystem() = default;

    ScriptSubsystem::~ScriptSubsystem() = default;

    void ScriptSubsystem::Initialize(EngineSystem* engine, const EngineConfig& /*config*/)
    {
        ScriptServices services;
        services.input = engine ? engine->GetService<InputManager>() : nullptr;
        services.audio = engine ? engine->GetService<AudioSystem>() : nullptr;
        services.engine = engine;

        auto host = std::make_unique<ScriptHost>();
        if (!host->Initialize(services)) {
            return;
        }
        host_ = std::move(host);
        scriptRoot_ = ProjectPaths::Resolve(kScriptRoot);

#ifdef USE_IMGUI
        host_->WritePredefined(scriptRoot_ / kPredefinedFileName);
#endif

        status_.ok = host_->Build(scriptRoot_);
        if (status_.ok) {
            RegisterComponentTypes();
        }
        status_.typeCount = host_->GetTypes().size();

#ifdef USE_IMGUI
        // コンパイルに失敗していても見張る（直して保存すれば、そのときに読み直す）
        watcher_.Start(scriptRoot_);
#endif
    }

    void ScriptSubsystem::Finalize()
    {
#ifdef USE_IMGUI
        watcher_.Stop();
#endif
        ComponentFactory::Get().UnregisterRuntimeTypes();
        if (host_) {
            host_->Shutdown();
            host_.reset();
        }
    }

    void ScriptSubsystem::EndFrame()
    {
        if (!host_) {
            return;
        }

        // 読み直すと型が作り直されるので、先にこのフレームの実行時間を締める
        host_->EndFrameStats();

#ifdef USE_IMGUI
        // スクリプトを実行していないここで読み直す
        std::vector<std::filesystem::path> changed;
        if (watcher_.TakeSettledChanges(kSettleTime, changed)) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Script,
                "スクリプトが {} 件変わったので読み直します", changed.size());
            ReloadScripts();
        }
#endif

        host_->CollectGarbageStep();
    }

    void ScriptSubsystem::RegisterComponentTypes()
    {
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

#ifdef USE_IMGUI
    void ScriptSubsystem::ReloadScripts()
    {
        // 生成の登録は型ごと作り直すので、先に外す（読み直せなかったときは前の型で登録し直す）
        ComponentFactory::Get().UnregisterRuntimeTypes();
        const ScriptHost::ReloadReport report = host_->Reload(scriptRoot_);
        RegisterComponentTypes();

        status_.ok = report.compiled;
        status_.typeCount = host_->GetTypes().size();
        status_.restored = report.restored;
        status_.orphaned = report.orphaned;
        status_.elapsedMs = report.elapsedMs;

        if (!report.compiled) {
            return;
        }
        Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Script,
            "スクリプトを読み直しました（値を戻したコンポーネント {} 個・止めた {} 個・{:.1f}ms）",
            report.restored, report.orphaned, report.elapsedMs);
    }
#endif
}
