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

#ifdef CORE_EDITOR
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#endif

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kScriptRoot = "Application/Assets/Scripts";
#ifdef CORE_EDITOR
        constexpr const char* kPredefinedFileName = "as.predefined";

        /// スクリプトの基底クラスの原本
        constexpr const char* kBaseScriptSource = "Engine/Templates/Scripts/ScriptComponent.as";

        /// 基底クラスをプロジェクトのスクリプトのフォルダへ書き出すときの名前
        constexpr const char* kBaseScriptFileName = "ScriptComponent.as";

        /// 変更が落ち着いたと見なすまでの時間（エディタは 1 回の保存で何度も変更を出す）
        constexpr std::chrono::milliseconds kSettleTime{ 200 };

        /// @brief ファイルの中身をそのまま読む（読めなければ false）
        bool ReadAll(const std::filesystem::path& path, std::string& out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                return false;
            }
            out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        /// @brief 基底クラスの原本をプロジェクトのスクリプトのフォルダへ写す（中身が同じなら書かない）
        void WriteBaseScript(const std::filesystem::path& scriptRoot)
        {
            Logger& logger = Logger::GetInstance();
            const std::filesystem::path source = ProjectPaths::Resolve(kBaseScriptSource);
            std::string text;
            if (!ReadAll(source, text)) {
                logger.Logf(LogLevel::Error, LogCategory::Script,
                    "スクリプトの基底クラスの原本を読めません: {}", logger.PathToUtf8(source));
                return;
            }

            const std::filesystem::path destination = scriptRoot / kBaseScriptFileName;
            std::string existing;
            if (ReadAll(destination, existing) && existing == text) {
                return;
            }

            std::error_code ec;
            std::filesystem::create_directories(scriptRoot, ec);
            std::ofstream out(destination, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            out.close();
            if (!out) {
                logger.Logf(LogLevel::Error, LogCategory::Script,
                    "スクリプトの基底クラスを書き出せませんでした: {}", logger.PathToUtf8(destination));
                return;
            }
            logger.Logf(LogLevel::Info, LogCategory::Script,
                "スクリプトの基底クラスを書き出しました: {}", logger.PathToUtf8(destination));
        }
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

#ifdef CORE_EDITOR
        WriteBaseScript(scriptRoot_);
        host_->WritePredefined(scriptRoot_ / kPredefinedFileName);
#endif

        status_.ok = host_->Build(scriptRoot_);
        if (status_.ok) {
            RegisterComponentTypes();
        }
        status_.typeCount = host_->GetTypes().size();

#ifdef CORE_EDITOR
        // コンパイルに失敗していても見張る（直して保存すれば、そのときに読み直す）
        watcher_.Start(scriptRoot_);
#endif
    }

    void ScriptSubsystem::Finalize()
    {
#ifdef CORE_EDITOR
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

#ifdef CORE_EDITOR
        // スクリプトを実行していないここで読み直す
        std::vector<std::filesystem::path> changed;
        const bool settled = watcher_.TakeSettledChanges(kSettleTime, changed);
        if (settled) {
            Logger::GetInstance().Logf(LogLevel::Info, LogCategory::Script,
                "スクリプトが {} 件変わったので読み直します", changed.size());
        } else if (reloadRequested_) {
            Logger::GetInstance().Log("スクリプトを読み直します", LogLevel::Info, LogCategory::Script);
        }
        if (settled || reloadRequested_) {
            reloadRequested_ = false;
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
            const std::filesystem::path sourceFile = raw->GetSourceSection().empty()
                ? std::filesystem::path{}
                : scriptRoot_ / Logger::GetInstance().Utf8ToPath(raw->GetSourceSection());
            const bool registered = factory.RegisterRuntime(
                raw->GetName(),
                [raw]() -> std::unique_ptr<IComponent> { return std::make_unique<ScriptComponent>(*raw); },
                &raw->GetDescriptor(),
                sourceFile);
            if (!registered) {
                Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
                    "スクリプトのクラス {} は、同じ名前のコンポーネントが既にあるので使えません", raw->GetName());
            }
        }
    }

#ifdef CORE_EDITOR
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
