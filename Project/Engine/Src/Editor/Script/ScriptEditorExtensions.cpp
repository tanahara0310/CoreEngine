#include "pch.h"
#include "Editor/Script/ScriptEditorExtensions.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Panel/EditorMenuRegistry.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Editor/Script/EditorScriptBinding.h"
#include "Script/Metadata/MetadataParser.h"
#include "Script/ScriptHost.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <imgui.h>
#include <scriptbuilder/scriptbuilder.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string_view>
#include <utility>

namespace CoreEngine::Editor
{
    namespace
    {
        /// ウィンドウの基底クラスの名前
        constexpr const char* kWindowBaseName = "EditorWindow";

        /// エディタだけで読むスクリプトのフォルダ（スクリプトのフォルダからの相対）
        constexpr std::string_view kEditorFolder = "Editor/";

        /// [MenuItem] を書かなかったウィンドウを置くメニュー
        constexpr const char* kDefaultMenuRoot = "Tools/";

        /// ウィンドウのパネルの ID に付ける印
        constexpr const char* kPanelIdPrefix = "###EditorWindow.";

        /// ウィンドウの初回の大きさ
        constexpr float kDefaultWindowWidth = 440.0f;
        constexpr float kDefaultWindowHeight = 720.0f;

        void LogScript(LogLevel level, const std::string& message)
        {
            Logger::GetInstance().Log(message, level, LogCategory::Script);
        }

        /// @brief 属性の並びから [MenuItem("場所")] の場所を読む
        /// @param owner ログに出すクラス名か関数名
        /// @return 付いていなければ空
        std::string ReadMenuPath(const std::vector<std::string>& blocks, const std::string& owner)
        {
            std::vector<Script::MetadataAttribute> attributes;
            std::string error;
            for (const std::string& block : blocks) {
                attributes.clear();
                if (!Script::ParseMetadata(block, attributes, error)) {
                    continue;
                }
                for (const Script::MetadataAttribute& attribute : attributes) {
                    if (attribute.name != "MenuItem") {
                        continue;
                    }
                    const std::string& path = attribute.arguments.empty() ? std::string() : attribute.arguments.front();
                    if (attribute.arguments.size() != 1 || path.find('/') == std::string::npos
                        || path.front() == '/' || path.back() == '/' || path.find("//") != std::string::npos) {
                        LogScript(LogLevel::Warn, owner + ": [MenuItem] の引数は「Tools/マップ生成」のように / で区切った場所 1 つです");
                        return {};
                    }
                    return path;
                }
            }
            return {};
        }

        /// @brief 場所の最後の区切り（ウィンドウの題名）
        std::string LastSegment(const std::string& path)
        {
            const std::size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        /// @brief 関数の名前空間つきの名前
        std::string QualifiedName(const asIScriptFunction& function)
        {
            const char* const nameSpace = function.GetNamespace();
            return (nameSpace && nameSpace[0] != '\0')
                ? std::string(nameSpace) + "::" + function.GetName()
                : std::string(function.GetName());
        }

        /// @brief スクリプトで宣言した型を含まない型か（モジュールを捨てても値を持っておける）
        bool IsModuleIndependent(const asIScriptEngine& engine, int typeId)
        {
            if ((typeId & (asTYPEID_OBJHANDLE | asTYPEID_SCRIPTOBJECT)) != 0) {
                return false;
            }
            if (typeId <= asTYPEID_DOUBLE) {
                return typeId != asTYPEID_VOID;
            }
            const asITypeInfo* const type = engine.GetTypeInfoById(typeId);
            if (!type || (type->GetFlags() & (asOBJ_SCRIPT_OBJECT | asOBJ_ENUM | asOBJ_FUNCDEF)) != 0) {
                return false;
            }
            for (asUINT i = 0; i < type->GetSubTypeCount(); ++i) {
                if (!IsModuleIndependent(engine, type->GetSubTypeId(i))) {
                    return false;
                }
            }
            return true;
        }
    }

    struct ScriptEditorExtensions::SavedProperty
    {
        std::string name;
        int typeId = 0;

        /// 列挙の型の宣言（列挙は型 ID でなく宣言で突き合わせる）
        std::string enumDeclaration;

        /// 数値・bool・列挙の値
        std::uint64_t value = 0;
        std::size_t valueSize = 0;

        /// 値型・参照型の複製（数値などのときは nullptr）
        void* object = nullptr;
        asITypeInfo* objectType = nullptr;

        SavedProperty() = default;
        SavedProperty(const SavedProperty&) = delete;
        SavedProperty& operator=(const SavedProperty&) = delete;

        SavedProperty(SavedProperty&& other) noexcept
            : name(std::move(other.name)), typeId(other.typeId), enumDeclaration(std::move(other.enumDeclaration)),
              value(other.value), valueSize(other.valueSize),
              object(std::exchange(other.object, nullptr)), objectType(std::exchange(other.objectType, nullptr)) {}

        SavedProperty& operator=(SavedProperty&&) = delete;

        ~SavedProperty()
        {
            if (objectType) {
                if (object) {
                    objectType->GetEngine()->ReleaseScriptObject(object, objectType);
                }
                objectType->Release();
            }
        }
    };

    struct ScriptEditorExtensions::Window
    {
        WindowClass windowClass;
        std::string panelId;
        asIScriptObject* object = nullptr;
        asIScriptFunction* onEnable = nullptr;
        asIScriptFunction* onGUI = nullptr;
        asIScriptFunction* onDisable = nullptr;

        /// OnEnable を呼んでから OnDisable を呼ぶまでの間か
        bool enabled = false;

        /// 例外などで止まったか（止まったら、読み直すか「もう一度動かす」を押すまで呼ばない）
        bool stopped = false;

        ScopedRegistration panel;

        ~Window()
        {
            panel.Reset();
            ReleaseObject();
        }

        void ReleaseObject()
        {
            if (object) {
                object->Release();
                object = nullptr;
            }
            onEnable = nullptr;
            onGUI = nullptr;
            onDisable = nullptr;
        }

        bool IsVisible() const
        {
            const EditorPanel* const found = EditorPanelRegistry::Get().Find(panelId);
            return found && found->visible;
        }
    };

    ScriptEditorExtensions::ScriptEditorExtensions(ScriptHost& host)
        : host_(host)
    {
    }

    ScriptEditorExtensions::~ScriptEditorExtensions()
    {
        Shutdown();
    }

    void ScriptEditorExtensions::OnModuleCompiled(asIScriptModule& module, CScriptBuilder& builder,
                                                  const std::unordered_map<std::string, std::string>& sources)
    {
        pendingWindows_.clear();
        pendingMenus_.clear();

        asITypeInfo* const base = module.GetTypeInfoByName(kWindowBaseName);
        for (asUINT i = 0; i < module.GetObjectTypeCount(); ++i) {
            asITypeInfo* const type = module.GetObjectTypeByIndex(i);
            if (!type) {
                continue;
            }
            const std::string className = type->GetName();
            std::string menuPath = ReadMenuPath(builder.GetMetadataForType(type->GetTypeId()), className);
            const bool isWindow = base && type != base && type->DerivesFrom(base)
                && (type->GetFlags() & asOBJ_ABSTRACT) == 0;
            if (!isWindow) {
                if (!menuPath.empty()) {
                    LogScript(LogLevel::Warn, className + ": [MenuItem] は EditorWindow を継いだクラスか、関数に付けます");
                }
                continue;
            }
            if (menuPath.empty()) {
                menuPath = std::string(kDefaultMenuRoot) + className;
            }
            pendingWindows_.push_back(WindowClass{ type, className, std::move(menuPath) });
        }

        for (asUINT i = 0; i < module.GetFunctionCount(); ++i) {
            asIScriptFunction* const function = module.GetFunctionByIndex(i);
            if (!function) {
                continue;
            }
            const std::string name = QualifiedName(*function);
            std::string menuPath = ReadMenuPath(builder.GetMetadataForFunc(function), name);
            if (menuPath.empty()) {
                continue;
            }
            if (function->GetReturnTypeId() != asTYPEID_VOID || function->GetParamCount() != 0) {
                LogScript(LogLevel::Warn, name + ": [MenuItem] を付ける関数は、引数も戻り値も無い形（void 名前()）にします");
                continue;
            }
            pendingMenus_.push_back(MenuFunction{ function, name, std::move(menuPath) });
        }

        ReportEditorApiOutsideEditorFolder(module, sources);
    }

    void ScriptEditorExtensions::OnModuleDiscarding()
    {
        menuRegistrations_.clear();

        for (const std::unique_ptr<Window>& window : windows_) {
            if (!window->object) {
                continue;
            }
            if (window->enabled) {
                CallWindowMethod(*window, window->onDisable, "OnDisable");
                window->enabled = false;
            }

            // モジュールに依らない型のメンバ変数の値を控える
            std::vector<SavedProperty>& saved = savedProperties_[window->windowClass.className];
            saved.clear();
            const asITypeInfo* const type = window->object->GetObjectType();
            asIScriptEngine* const engine = type->GetEngine();
            for (asUINT i = 0; i < type->GetPropertyCount(); ++i) {
                const char* name = nullptr;
                int typeId = 0;
                if (type->GetProperty(i, &name, &typeId) < 0 || !name) {
                    continue;
                }
                const void* const address = window->object->GetAddressOfProperty(i);
                if (!address || (typeId & asTYPEID_OBJHANDLE) != 0) {
                    continue;
                }

                SavedProperty property;
                property.name = name;
                property.typeId = typeId;
                if (typeId <= asTYPEID_DOUBLE) {
                    property.valueSize = static_cast<std::size_t>(engine->GetSizeOfPrimitiveType(typeId));
                } else if (asITypeInfo* const propertyType = engine->GetTypeInfoById(typeId);
                           propertyType && (propertyType->GetFlags() & asOBJ_ENUM) != 0) {
                    property.enumDeclaration = engine->GetTypeDeclaration(typeId, true);
                    property.valueSize = propertyType->GetSize();
                } else if (propertyType && IsModuleIndependent(*engine, typeId)) {
                    property.object = engine->CreateScriptObjectCopy(const_cast<void*>(address), propertyType);
                    if (!property.object) {
                        continue;
                    }
                    propertyType->AddRef();
                    property.objectType = propertyType;
                } else {
                    continue;
                }
                if (property.valueSize > 0 && property.valueSize <= sizeof(property.value)) {
                    std::memcpy(&property.value, address, property.valueSize);
                }
                saved.push_back(std::move(property));
            }

            window->ReleaseObject();
        }
    }

    void ScriptEditorExtensions::OnModuleSwapped()
    {
        // クラスが無くなったウィンドウを外す
        std::erase_if(windows_, [this](const std::unique_ptr<Window>& window) {
            return std::none_of(pendingWindows_.begin(), pendingWindows_.end(), [&window](const WindowClass& windowClass) {
                return windowClass.className == window->windowClass.className;
                });
            });

        for (WindowClass& windowClass : pendingWindows_) {
            auto found = std::find_if(windows_.begin(), windows_.end(), [&windowClass](const std::unique_ptr<Window>& window) {
                return window->windowClass.className == windowClass.className;
                });
            if (found == windows_.end()) {
                windows_.push_back(std::make_unique<Window>());
                found = std::prev(windows_.end());
            }
            Window& window = **found;
            const std::string className = windowClass.className;
            const std::string panelId = LastSegment(windowClass.menuPath) + kPanelIdPrefix + className;
            window.windowClass = std::move(windowClass);
            window.enabled = false;
            window.stopped = false;

            if (!window.panel || window.panelId != panelId) {
                window.panel.Reset();
                window.panelId = panelId;
                window.panel = EditorPanelRegistry::Get().Register({
                    .id = panelId,
                    .placement = PanelPlacement::Window,
                    .group = PanelGroup::Application,
                    .defaultWidth = kDefaultWindowWidth,
                    .defaultHeight = kDefaultWindowHeight,
                    .draw = [this, className] { DrawWindow(className); },
                    });
            }

            asITypeInfo* const type = window.windowClass.type;
            window.object = static_cast<asIScriptObject*>(type->GetEngine()->CreateScriptObject(type));
            if (!window.object) {
                LogScript(LogLevel::Error, "ウィンドウ " + className + " を作れませんでした（コンストラクタかメンバ変数の初期値で止まった可能性があります）");
                window.stopped = true;
                continue;
            }
            window.onEnable = type->GetMethodByDecl("void OnEnable()");
            window.onGUI = type->GetMethodByDecl("void OnGUI()");
            window.onDisable = type->GetMethodByDecl("void OnDisable()");

            // 控えた値を、名前と型が同じメンバ変数へ戻す
            const auto saved = savedProperties_.find(className);
            if (saved == savedProperties_.end()) {
                continue;
            }
            asIScriptEngine* const engine = type->GetEngine();
            for (asUINT i = 0; i < type->GetPropertyCount(); ++i) {
                const char* name = nullptr;
                int typeId = 0;
                if (type->GetProperty(i, &name, &typeId) < 0 || !name) {
                    continue;
                }
                const auto property = std::find_if(saved->second.begin(), saved->second.end(),
                    [name](const SavedProperty& candidate) { return candidate.name == name; });
                void* const address = window.object->GetAddressOfProperty(i);
                if (property == saved->second.end() || !address) {
                    continue;
                }
                if (property->objectType) {
                    if (property->typeId == typeId) {
                        engine->AssignScriptObject(address, property->object, property->objectType);
                    }
                    continue;
                }
                const bool sameType = property->enumDeclaration.empty()
                    ? property->typeId == typeId
                    : property->enumDeclaration == engine->GetTypeDeclaration(typeId, true);
                if (sameType && property->valueSize > 0 && property->valueSize <= sizeof(property->value)) {
                    std::memcpy(address, &property->value, property->valueSize);
                }
            }
        }
        savedProperties_.clear();
        pendingWindows_.clear();

        // メニューを登録し直す（ウィンドウは開閉、関数は呼び出し）
        for (const std::unique_ptr<Window>& window : windows_) {
            const std::string panelId = window->panelId;
            menuRegistrations_.push_back(EditorMenuRegistry::Get().Register({
                .path = window->windowClass.menuPath,
                .action = [panelId] {
                    if (EditorPanel* const panel = EditorPanelRegistry::Get().Find(panelId)) {
                        panel->visible = !panel->visible;
                    }
                },
                .checked = [panelId] {
                    const EditorPanel* const panel = EditorPanelRegistry::Get().Find(panelId);
                    return panel && panel->visible;
                },
                }));
        }
        for (const MenuFunction& menu : pendingMenus_) {
            asIScriptFunction* const function = menu.function;
            const std::string name = menu.name;
            menuRegistrations_.push_back(EditorMenuRegistry::Get().Register({
                .path = menu.menuPath,
                .action = [this, function, name] {
                    ScriptBinding::CallScope scope;
                    host_.CallFunction(function, nullptr, [&name] { return "メニューの関数 " + name; },
                        ScriptHost::kEditorLineBudget);
                },
                }));
        }
        pendingMenus_.clear();
    }

    void ScriptEditorExtensions::EndFrame()
    {
        for (const std::unique_ptr<Window>& window : windows_) {
            if (window->enabled && !window->IsVisible()) {
                window->enabled = false;
                if (!window->stopped && !CallWindowMethod(*window, window->onDisable, "OnDisable")) {
                    window->stopped = true;
                }
            }
        }
    }

    void ScriptEditorExtensions::Shutdown()
    {
        menuRegistrations_.clear();
        for (const std::unique_ptr<Window>& window : windows_) {
            if (window->enabled && window->object && !window->stopped) {
                CallWindowMethod(*window, window->onDisable, "OnDisable");
            }
            window->enabled = false;
        }
        windows_.clear();
        savedProperties_.clear();
        pendingWindows_.clear();
        pendingMenus_.clear();
    }

    void ScriptEditorExtensions::DrawWindow(const std::string& className)
    {
        const auto found = std::find_if(windows_.begin(), windows_.end(), [&className](const std::unique_ptr<Window>& window) {
            return window->windowClass.className == className;
            });
        Window* const window = found == windows_.end() ? nullptr : found->get();
        if (!window || !window->object) {
            ImGui::TextDisabled("スクリプトのウィンドウを作れていません（Console を見てください）");
            return;
        }

        if (window->stopped) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(Theme::kError, "%s",
                "スクリプトが止まったので、このウィンドウを止めています。Console のエラーを直して保存すると、読み直して動き始めます。");
            ImGui::PopTextWrapPos();
            if (ImGui::Button("もう一度動かす")) {
                window->stopped = false;
            }
            return;
        }

        if (!window->enabled) {
            window->enabled = true;
            if (!CallWindowMethod(*window, window->onEnable, "OnEnable")) {
                window->stopped = true;
                return;
            }
        }

        ScriptBinding::GUIScope gui;
        if (!CallWindowMethod(*window, window->onGUI, "OnGUI")) {
            window->stopped = true;
        }
    }

    bool ScriptEditorExtensions::CallWindowMethod(Window& window, asIScriptFunction* method, const char* methodName)
    {
        if (!method || !window.object) {
            return true;
        }
        ScriptBinding::CallScope scope;
        return host_.CallMethod(method, window.object, [&window, methodName] {
            return window.windowClass.className + "." + methodName;
            }, ScriptHost::kEditorLineBudget);
    }

    void ScriptEditorExtensions::ReportEditorApiOutsideEditorFolder(
        const asIScriptModule& module, const std::unordered_map<std::string, std::string>& sources) const
    {
        const asIScriptEngine* const engine = module.GetEngine();
        for (const auto& [section, text] : sources) {
            if (section.starts_with(kEditorFolder)) {
                continue;
            }

            int row = 1;
            std::string_view previous;
            for (std::size_t at = 0; at < text.size();) {
                asUINT length = 0;
                const asETokenClass kind = engine->ParseToken(text.data() + at, text.size() - at, &length);
                const std::size_t step = (std::min)(static_cast<std::size_t>((std::max)(length, 1u)), text.size() - at);
                const std::string_view token(text.data() + at, step);
                at += step;

                if (kind == asTC_WHITESPACE || kind == asTC_COMMENT) {
                    row += static_cast<int>(std::count(token.begin(), token.end(), '\n'));
                    continue;
                }

                std::string_view used;
                if (kind == asTC_IDENTIFIER && (token == "EditorWindow" || token == "EditorGUI")) {
                    used = token;
                } else if (kind == asTC_KEYWORD && token == "::" && previous == "Editor") {
                    used = "Editor";
                }
                if (!used.empty()) {
                    LogScript(LogLevel::Error, section + "(" + std::to_string(row) + ") で " + std::string(used)
                        + " を使っています。エディタの機能は Scripts/Editor フォルダの中だけで使えます"
                        "（書き出したゲームでは、このファイルがコンパイルできません）");
                    break;
                }
                previous = kind == asTC_IDENTIFIER ? token : std::string_view();
                row += static_cast<int>(std::count(token.begin(), token.end(), '\n'));
            }
        }
    }
}

#endif // CORE_EDITOR
