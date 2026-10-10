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

#include <string>

#ifdef CORE_EDITOR
#include <chrono>
#include <fstream>
#include <iterator>
#include <vector>
#endif

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kScriptRoot = "Application/Assets/Scripts";

        /// @brief ファクトリに登録できなかったスクリプトのクラスについて、何とぶつかったかをログへ出す
        void ReportRejectedType(const ScriptHost& host, const ScriptComponentType& rejected)
        {
            const auto where = [](const ScriptComponentType& type) {
                return type.GetSourceSection().empty() ? std::string("場所不明") : type.GetSourceSection();
                };

            Logger& logger = Logger::GetInstance();
            const ScriptComponentType* const first = host.FindType(rejected.GetName());
            if (ComponentFactory::Get().IsRuntimeType(rejected.GetName()) && first && first != &rejected) {
                logger.Logf(LogLevel::Error, LogCategory::Script,
                    "コンポーネント {} がスクリプトに 2 つあります（{} と {}）。{} の方は使えません。"
                    "名前空間が違っても、コンポーネントのクラス名はスクリプト全体で 1 つにしてください",
                    rejected.GetName(), where(*first), where(rejected), where(rejected));
                return;
            }
            logger.Logf(LogLevel::Error, LogCategory::Script,
                "スクリプトのクラス {}（{}）は、エンジンのコンポーネントと同じ名前なので使えません。名前を変えてください",
                rejected.GetName(), where(rejected));
        }

#ifdef CORE_EDITOR
        constexpr const char* kPredefinedFileName = "as.predefined";

        /// @brief プロジェクトのスクリプトのフォルダへ写す基底クラス
        struct BaseScript
        {
            const char* source;      ///< 原本（エンジンの根からの相対パス）
            const char* destination; ///< 書き出す先（スクリプトのフォルダからの相対パス）
        };

        constexpr BaseScript kBaseScripts[] = {
            { "Engine/Templates/Scripts/ScriptComponent.as", "ScriptComponent.as" },
            { "Engine/Templates/Scripts/Editor/EditorWindow.as", "Editor/EditorWindow.as" },
            { "Engine/Templates/Scripts/Editor/ComponentEditor.as", "Editor/ComponentEditor.as" },
        };

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
        void WriteBaseScript(const std::filesystem::path& scriptRoot, const BaseScript& base)
        {
            Logger& logger = Logger::GetInstance();
            const std::filesystem::path source = ProjectPaths::Resolve(base.source);
            std::string text;
            if (!ReadAll(source, text)) {
                logger.Logf(LogLevel::Error, LogCategory::Script,
                    "スクリプトの基底クラスの原本を読めません: {}", logger.PathToUtf8(source));
                return;
            }

            const std::filesystem::path destination = scriptRoot / base.destination;
            std::string existing;
            if (ReadAll(destination, existing) && existing == text) {
                return;
            }

            std::error_code ec;
            std::filesystem::create_directories(destination.parent_path(), ec);
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
        for (const BaseScript& base : kBaseScripts) {
            WriteBaseScript(scriptRoot_, base);
        }
        host_->WritePredefined(scriptRoot_ / kPredefinedFileName);

        editorExtensions_ = std::make_unique<Editor::ScriptEditorExtensions>(*host_);
        Editor::ScriptEditorExtensions* const extensions = editorExtensions_.get();
        host_->SetModuleListener({
            .compiled = [extensions](asIScriptModule& module, CScriptBuilder& builder,
                                     const std::unordered_map<std::string, std::string>& sources) {
                extensions->OnModuleCompiled(module, builder, sources);
            },
            .discarding = [extensions] { extensions->OnModuleDiscarding(); },
            .swapped = [extensions] { extensions->OnModuleSwapped(); },
            });
#endif

        const bool built = host_->Build(scriptRoot_);
        status_.state = built ? ScriptBuildState::Ok : ScriptBuildState::NotBuilt;
        if (built) {
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
        if (host_) {
            host_->SetModuleListener({});
        }
        if (editorExtensions_) {
            editorExtensions_->Shutdown();
            editorExtensions_.reset();
        }
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
        if (editorExtensions_) {
            editorExtensions_->EndFrame();
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
                ReportRejectedType(*host_, *raw);
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

        // 組めたら、エラーのあるファイルが残っているかで決める。組めなければ、前のモジュールがあればそのまま動く
        if (report.compiled) {
            status_.state = report.staleSections.empty() ? ScriptBuildState::Ok : ScriptBuildState::RunningStale;
            status_.staleFiles = report.staleSections;
        } else if (status_.state != ScriptBuildState::NotBuilt) {
            status_.state = ScriptBuildState::KeptPrevious;
        }
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
