#include "pch.h"
#include "Script/ScriptHost.h"

#include "Script/Binding/AudioBinding.h"
#include "Script/Binding/CameraShakeBinding.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Script/Binding/LogBinding.h"
#include "Script/Binding/MathBinding.h"
#include "Script/Binding/RandomBinding.h"
#include "Script/Binding/RenderingBinding.h"
#include "Script/Binding/SceneBinding.h"
#include "Script/Binding/ScriptInputBinding.h"
#include "Script/Binding/SessionBinding.h"
#include "Script/Binding/TimeBinding.h"
#include "Script/Binding/TweenBinding.h"
#include "Script/Binding/UIBinding.h"
#include "Script/ScriptComponent.h"
#include "Script/ScriptComponentType.h"
#include "Script/ScriptDiagnostics.h"
#include "Script/ScriptPredefined.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <scriptarray/scriptarray.h>
#include <scriptbuilder/scriptbuilder.h>
#include <scripthelper/scripthelper.h>
#include <scriptmath/scriptmath.h>
#include <scriptstdstring/scriptstdstring.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        constexpr const char* kModuleName = "Game";
        constexpr const char* kComponentBaseName = "ScriptComponent";

        /// 1 回の呼び出しで実行してよい行数
        constexpr uint32_t kLineBudget = 200000;

        /// エンジンのユーザーデータに実行環境を置くときの種類
        constexpr asPWORD kHostUserDataType = 0x436F7245;

#ifdef USE_IMGUI
        struct LineBudget
        {
            uint32_t lines = 0;
        };

        void CountLine(asIScriptContext* context, void* userData)
        {
            auto* const budget = static_cast<LineBudget*>(userData);
            if (++budget->lines > kLineBudget) {
                context->Abort();
            }
        }
#endif

        /// @brief `#include` を読み飛ばす（フォルダの `.as` はすべて読み込み済み）
        int IgnoreInclude(const char* include, const char* from, CScriptBuilder* builder, void* userData)
        {
            (void)include;
            (void)from;
            (void)builder;
            (void)userData;
            return 0;
        }

        std::string ToGenericUtf8(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief フォルダの `.as` を集める（相対パスの綴り順）
        std::vector<std::filesystem::path> CollectScriptFiles(const std::filesystem::path& root)
        {
            std::vector<std::filesystem::path> files;
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec)) {
                return files;
            }

            for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                const std::filesystem::path relative = it->path().lexically_relative(root);
#ifndef USE_IMGUI
                if (it.depth() == 0 && it->is_directory(ec) && relative == "Editor") {
                    it.disable_recursion_pending();
                    continue;
                }
#endif
                if (!it->is_regular_file(ec) || it->path().extension() != ".as") {
                    continue;
                }
                files.push_back(relative);
            }

            std::sort(files.begin(), files.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
                return a.generic_u8string() < b.generic_u8string();
            });
            return files;
        }

        /// @brief ファイルの中身を読む（先頭の UTF-8 の BOM は落とす）
        bool ReadScriptText(const std::filesystem::path& path, std::string& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) {
                return false;
            }
            out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
            if (out.size() >= 3 &&
                static_cast<unsigned char>(out[0]) == 0xEF &&
                static_cast<unsigned char>(out[1]) == 0xBB &&
                static_cast<unsigned char>(out[2]) == 0xBF) {
                out.erase(0, 3);
            }
            return true;
        }
    }

    namespace
    {
        /// @brief 束縛した関数が投げた C++ の例外を、中身の分かるスクリプトの例外にする
        void TranslateAppException(asIScriptContext* context, void*)
        {
            try {
                throw;
            } catch (const std::exception& exception) {
                context->SetException(exception.what());
            } catch (...) {
                context->SetException("C++ の例外（型が分からない）");
            }
        }

        /// @brief スクリプトのクラスを書いた節（ファイル）の名前を、そのクラスのスクリプトの関数から引く
        /// @return 引けなければ空
        std::string FindDeclaringSection(const asITypeInfo& type)
        {
            const auto sectionOf = [](const asIScriptFunction* function) -> std::string {
                const char* section = nullptr;
                if (function && function->GetDeclaredAt(&section, nullptr, nullptr) >= 0 && section && section[0] != '\0') {
                    return section;
                }
                return {};
            };
            for (asUINT i = 0; i < type.GetMethodCount(); ++i) {
                const asIScriptFunction* const method = type.GetMethodByIndex(i, false);
                if (method && method->GetObjectType() == &type) {
                    if (std::string section = sectionOf(method); !section.empty()) {
                        return section;
                    }
                }
            }
            for (asUINT i = 0; i < type.GetBehaviourCount(); ++i) {
                asEBehaviours behaviour = asBEHAVE_CONSTRUCT;
                if (std::string section = sectionOf(type.GetBehaviourByIndex(i, &behaviour)); !section.empty()) {
                    return section;
                }
            }
            for (asUINT i = 0; i < type.GetFactoryCount(); ++i) {
                if (std::string section = sectionOf(type.GetFactoryByIndex(i)); !section.empty()) {
                    return section;
                }
            }
            return {};
        }

        /// @brief パスのファイル名から拡張子を除いた部分（区切りは `/`）
        std::string FileStem(const std::string& path)
        {
            const std::size_t slash = path.find_last_of('/');
            const std::size_t begin = slash == std::string::npos ? 0 : slash + 1;
            const std::size_t dot = path.find_last_of('.');
            const std::size_t end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
            return path.substr(begin, end - begin);
        }
    }

    ScriptHost::ScriptHost() = default;

    ScriptHost::~ScriptHost()
    {
        Shutdown();
    }

    bool ScriptHost::Initialize(const ScriptServices& services)
    {
        Logger& logger = Logger::GetInstance();

        engine_ = asCreateScriptEngine();
        if (!engine_) {
            logger.Logf(LogLevel::Error, LogCategory::Script, "AngelScript のエンジンを作れませんでした");
            return false;
        }

        engine_->SetUserData(this, kHostUserDataType);
        lifetimeToken_ = std::make_shared<int>(0);

        bool configured = engine_->SetMessageCallback(asFUNCTION(Script::OnCompilerMessage), nullptr, asCALL_CDECL) >= 0;
        configured = engine_->SetEngineProperty(asEP_COMPILER_WARNINGS, 2) >= 0 && configured;
        configured = engine_->SetContextCallbacks(&ScriptHost::RequestContext, &ScriptHost::ReturnContext, this) >= 0 && configured;
        configured = engine_->SetTranslateAppExceptionCallback(asFUNCTION(TranslateAppException), nullptr, asCALL_CDECL) >= 0 && configured;

        RegisterScriptArray(engine_, true);
        RegisterStdString(engine_);
        RegisterScriptMath(engine_);
        RegisterExceptionRoutines(engine_);
        configured = Script::RegisterLogBinding(engine_) && configured;
        configured = Script::RegisterMathBinding(engine_) && configured;
        configured = Script::RegisterTimeBinding(engine_) && configured;
        configured = Script::RegisterGameObjectBinding(engine_) && configured;
        configured = Script::RegisterInputBinding(engine_, services.input) && configured;
        if (!services.input) {
            logger.Logf(LogLevel::Warn, LogCategory::Script, "入力が見つからないので、スクリプトの Input は常に押されていないを返します");
        }
        configured = Script::RegisterTweenBinding(engine_) && configured;
        configured = Script::RegisterUIBinding(engine_, services.engine) && configured;
        configured = Script::RegisterAudioBinding(engine_, services.audio) && configured;
        if (!services.audio) {
            logger.Logf(LogLevel::Warn, LogCategory::Script, "音が見つからないので、スクリプトの Audio は何も鳴らしません");
        }
        configured = Script::RegisterCameraShakeBinding(engine_) && configured;
        configured = Script::RegisterSceneBinding(engine_, services.engine) && configured;
        configured = Script::RegisterRenderingBinding(engine_, services.engine) && configured;
        if (!services.engine) {
            logger.Logf(LogLevel::Warn, LogCategory::Script, "エンジンが見つからないので、スクリプトの Scene・Rendering・Font は何もしません");
        }
        configured = Script::RegisterSessionBinding(engine_) && configured;
        configured = Script::RegisterRandomBinding(engine_) && configured;

        stringTypeId_ = engine_->GetTypeIdByDecl("string");
        vector2TypeId_ = engine_->GetTypeIdByDecl("Vector2");
        vector3TypeId_ = engine_->GetTypeIdByDecl("Vector3");
        vector4TypeId_ = engine_->GetTypeIdByDecl("Vector4");
        gameObjectHandleTypeId_ = engine_->GetTypeIdByDecl("GameObject@");
        if (!configured || stringTypeId_ < 0 || vector2TypeId_ < 0 || vector3TypeId_ < 0 || vector4TypeId_ < 0 ||
            gameObjectHandleTypeId_ < 0) {
            logger.Logf(LogLevel::Error, LogCategory::Script, "スクリプトから使う型と関数を登録できませんでした");
            return false;
        }

        logger.Logf(LogLevel::Info, LogCategory::Script, "AngelScript {} を初期化しました", asGetLibraryVersion());
        return true;
    }

    bool ScriptHost::Build(const std::filesystem::path& root)
    {
        Logger& logger = Logger::GetInstance();
        if (!engine_) {
            return false;
        }
        if (!components_.empty()) {
            logger.Logf(LogLevel::Error, LogCategory::Script,
                "スクリプトのコンポーネントが {} 個残っているので、コンパイルし直せません", components_.size());
            return false;
        }
        DiscardModule();

        const auto started = std::chrono::steady_clock::now();
        const std::vector<std::filesystem::path> files = CollectScriptFiles(root);
        if (files.empty()) {
            logger.Logf(LogLevel::Warn, LogCategory::Script, "スクリプトがありません: {}", logger.PathToUtf8(root));
            return false;
        }

        CScriptBuilder builder;
        builder.SetIncludeCallback(&IgnoreInclude, nullptr);
        if (builder.StartNewModule(engine_, kModuleName) < 0) {
            logger.Logf(LogLevel::Error, LogCategory::Script, "スクリプトのモジュールを作れませんでした");
            return false;
        }

        std::string text;
        for (const std::filesystem::path& relative : files) {
            const std::string section = ToGenericUtf8(relative);
            if (!ReadScriptText(root / relative, text)) {
                logger.Logf(LogLevel::Error, LogCategory::Script, "スクリプトを読めません: {}", section);
                builder.GetModule()->Discard();
                return false;
            }
            if (builder.AddSectionFromMemory(section.c_str(), text.c_str(), static_cast<unsigned int>(text.size())) < 0) {
                logger.Logf(LogLevel::Error, LogCategory::Script, "スクリプトを追加できません: {}", section);
                builder.GetModule()->Discard();
                return false;
            }
        }

        if (builder.BuildModule() < 0) {
            logger.Logf(LogLevel::Error, LogCategory::Script,
                "スクリプトのコンパイルに失敗しました（{} ファイル）", files.size());
            builder.GetModule()->Discard();
            return false;
        }
        module_ = builder.GetModule();

        asITypeInfo* const base = module_->GetTypeInfoByName(kComponentBaseName);
        if (!base) {
            logger.Logf(LogLevel::Error, LogCategory::Script,
                "クラス {} がありません（Shared/ScriptComponent.as）", kComponentBaseName);
            DiscardModule();
            return false;
        }

        for (asUINT i = 0; i < module_->GetObjectTypeCount(); ++i) {
            asITypeInfo* const type = module_->GetObjectTypeByIndex(i);
            if (!type || type == base || !type->DerivesFrom(base)) {
                continue;
            }
            if ((type->GetFlags() & asOBJ_ABSTRACT) != 0) {
                continue;
            }
            types_.push_back(std::make_unique<ScriptComponentType>(*this, type, base, builder));

            const ScriptComponentType& added = *types_.back();
            logger.Logf(LogLevel::Info, LogCategory::Script, "コンポーネントの型 {}（{}）: プロパティ {} 個",
                added.GetName(), added.GetDisplayName(), added.GetDescriptor().properties.size());
        }

        // コンポーネントのクラスは「1 ファイルに 1 つ」「ファイル名 = クラス名」で書く。外れていても動かすが、警告を出す
        std::unordered_map<std::string, std::vector<std::string>> classesBySection;
        for (const auto& type : types_) {
            const std::string section = FindDeclaringSection(*type->GetTypeInfo());
            if (section.empty()) {
                continue;
            }
            classesBySection[section].push_back(type->GetName());
            if (FileStem(section) != type->GetName()) {
                logger.Logf(LogLevel::Warn, LogCategory::Script,
                    "コンポーネントのクラス {} が {} に書かれています。ファイル名をクラス名と同じにしてください",
                    type->GetName(), section);
            }
        }
        for (const auto& [section, names] : classesBySection) {
            if (names.size() < 2) {
                continue;
            }
            std::string joined;
            for (const std::string& name : names) {
                joined += (joined.empty() ? "" : "・") + name;
            }
            logger.Logf(LogLevel::Warn, LogCategory::Script,
                "{} にコンポーネントのクラスが {} 個あります（{}）。1 ファイルに 1 つにしてください",
                section, names.size(), joined);
        }

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        logger.Logf(LogLevel::Info, LogCategory::Script,
            "スクリプト {} ファイルをコンパイルしました（コンポーネントの型 {} 個・{:.1f}ms）",
            files.size(), types_.size(), elapsedMs);
        return true;
    }

    void ScriptHost::Shutdown()
    {
        const std::vector<ScriptComponent*> alive(components_.begin(), components_.end());
        for (ScriptComponent* const component : alive) {
            component->ReleaseScriptObject();
        }
        components_.clear();
        DiscardModule();

        if (!engine_) {
            return;
        }
        engine_->GarbageCollect(asGC_FULL_CYCLE);
        engine_->SetContextCallbacks(nullptr, nullptr, nullptr);
        for (asIScriptContext* const context : contextPool_) {
            context->Release();
        }
        contextPool_.clear();
        lifetimeToken_.reset();
        engine_->ShutDownAndRelease();
        engine_ = nullptr;
        stringTypeId_ = 0;
        vector2TypeId_ = 0;
        vector3TypeId_ = 0;
        vector4TypeId_ = 0;
        gameObjectHandleTypeId_ = 0;
    }

    void ScriptHost::CollectGarbageStep()
    {
        if (engine_) {
            engine_->GarbageCollect(asGC_ONE_STEP);
        }
    }

    bool ScriptHost::WritePredefined(const std::filesystem::path& file) const
    {
        return engine_ && Script::WriteScriptPredefined(*engine_, file);
    }

    asIScriptObject* ScriptHost::CreateObject(const ScriptComponentType& type)
    {
        if (!engine_ || !type.GetTypeInfo()) {
            return nullptr;
        }
        return static_cast<asIScriptObject*>(engine_->CreateScriptObject(type.GetTypeInfo()));
    }

    bool ScriptHost::CallMethod(asIScriptFunction* function, asIScriptObject* object,
                                const std::function<std::string()>& describeCaller)
    {
        if (!engine_ || !function || !object) {
            return false;
        }
        asIScriptContext* const context = engine_->RequestContext();
        if (!context) {
            return false;
        }

        int result = context->Prepare(function);
        if (result >= 0) {
            result = context->SetObject(object);
        }
        return RunPrepared(context, result, describeCaller);
    }

    bool ScriptHost::CallFunction(asIScriptFunction* function, const std::function<int(asIScriptContext*)>& setArguments,
                                  const std::function<std::string()>& describeCaller)
    {
        if (!engine_ || !function) {
            return false;
        }
        asIScriptContext* const context = engine_->RequestContext();
        if (!context) {
            return false;
        }

        int result = context->Prepare(function);
        if (result >= 0 && setArguments) {
            result = setArguments(context);
        }
        return RunPrepared(context, result, describeCaller);
    }

    ScriptHost* ScriptHost::FromEngine(asIScriptEngine* engine)
    {
        return engine ? static_cast<ScriptHost*>(engine->GetUserData(kHostUserDataType)) : nullptr;
    }

    bool ScriptHost::RunPrepared(asIScriptContext* context, int result, const std::function<std::string()>& describeCaller)
    {
#ifdef USE_IMGUI
        LineBudget budget;
        if (result >= 0) {
            result = context->SetLineCallback(asFUNCTION(CountLine), &budget, asCALL_CDECL);
        }
#endif
        if (result >= 0) {
            result = context->Execute();
        }

        const bool finished = result == asEXECUTION_FINISHED;
        if (!finished) {
            const std::string caller = describeCaller ? describeCaller() : std::string("スクリプト");
            if (result == asEXECUTION_EXCEPTION) {
                Script::LogFailedExecution(context, caller + " で例外が起きました");
            } else if (result == asEXECUTION_ABORTED) {
                Script::LogFailedExecution(context,
                    caller + " が 1 回の呼び出しで " + std::to_string(kLineBudget) + " 行を超えたので中断しました");
            } else {
                Script::LogFailedExecution(context,
                    caller + " を実行できませんでした（" + std::to_string(result) + "）");
            }
        }

#ifdef USE_IMGUI
        context->ClearLineCallback();
#endif
        engine_->ReturnContext(context);
        return finished;
    }

    const char* ScriptHost::GetTypeDeclaration(int typeId) const
    {
        return engine_ ? engine_->GetTypeDeclaration(typeId) : nullptr;
    }

    int ScriptHost::GetArrayElementTypeId(int typeId) const
    {
        if (!engine_ || (typeId & asTYPEID_OBJHANDLE) != 0) {
            return -1;
        }
        const asITypeInfo* const type = engine_->GetTypeInfoById(typeId);
        if (!type || type->GetSubTypeCount() != 1 || !type->GetName() || std::string(type->GetName()) != "array") {
            return -1;
        }
        return type->GetSubTypeId(0);
    }

    void ScriptHost::RegisterComponent(ScriptComponent* component)
    {
        if (component) {
            components_.insert(component);
        }
    }

    void ScriptHost::UnregisterComponent(ScriptComponent* component)
    {
        components_.erase(component);
    }

    asIScriptContext* ScriptHost::RequestContext(asIScriptEngine* engine, void* userData)
    {
        auto* const host = static_cast<ScriptHost*>(userData);
        if (!host->contextPool_.empty()) {
            asIScriptContext* const context = host->contextPool_.back();
            host->contextPool_.pop_back();
            return context;
        }
        return engine->CreateContext();
    }

    void ScriptHost::ReturnContext(asIScriptEngine* engine, asIScriptContext* context, void* userData)
    {
        (void)engine;
        if (!context) {
            return;
        }
        context->Unprepare();
        static_cast<ScriptHost*>(userData)->contextPool_.push_back(context);
    }

    void ScriptHost::DiscardModule()
    {
        types_.clear();
        if (module_) {
            module_->Discard();
            module_ = nullptr;
        }
    }
}
