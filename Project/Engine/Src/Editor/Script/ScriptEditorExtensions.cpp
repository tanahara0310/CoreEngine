#include "pch.h"
#include "Editor/Script/ScriptEditorExtensions.h"

#ifdef CORE_EDITOR

#include "Editor/ImGui/EditorTheme.h"
#include "Editor/Panel/EditorMenuRegistry.h"
#include "Editor/Panel/EditorPanelRegistry.h"
#include "Editor/Script/EditorScriptBinding.h"
#include "Script/Metadata/MetadataParser.h"
#include "Script/ScriptHost.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <imgui.h>
#include <scriptarray/scriptarray.h>
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

        /// ウィンドウの値を残すファイル（プロジェクトの根からの相対）
        constexpr const char* kPersistedFile = "Application/Saved/EditorSettings/ScriptWindows.json";

        /// @brief Vector2〜Vector4 の成分の数（Vector でなければ 0）
        int VectorComponents(const ScriptHost& host, int typeId)
        {
            if (typeId == host.GetVector2TypeId()) {
                return 2;
            }
            if (typeId == host.GetVector3TypeId()) {
                return 3;
            }
            if (typeId == host.GetVector4TypeId()) {
                return 4;
            }
            return 0;
        }

        /// @brief メンバ変数の値を JSON にする（数・bool・string・Vector・列挙と、それらの array）
        /// @return 扱えない型なら false
        bool ValueToJson(const ScriptHost& host, const asIScriptEngine& engine, int typeId, const void* address, json& out)
        {
            switch (typeId) {
            case asTYPEID_BOOL: out = *static_cast<const bool*>(address); return true;
            case asTYPEID_INT8: out = *static_cast<const std::int8_t*>(address); return true;
            case asTYPEID_INT16: out = *static_cast<const std::int16_t*>(address); return true;
            case asTYPEID_INT32: out = *static_cast<const std::int32_t*>(address); return true;
            case asTYPEID_INT64: out = *static_cast<const std::int64_t*>(address); return true;
            case asTYPEID_UINT8: out = *static_cast<const std::uint8_t*>(address); return true;
            case asTYPEID_UINT16: out = *static_cast<const std::uint16_t*>(address); return true;
            case asTYPEID_UINT32: out = *static_cast<const std::uint32_t*>(address); return true;
            case asTYPEID_UINT64: out = *static_cast<const std::uint64_t*>(address); return true;
            case asTYPEID_FLOAT: out = *static_cast<const float*>(address); return true;
            case asTYPEID_DOUBLE: out = *static_cast<const double*>(address); return true;
            default: break;
            }
            if ((typeId & asTYPEID_OBJHANDLE) != 0) {
                return false;
            }
            if (typeId == host.GetStringTypeId()) {
                out = *static_cast<const std::string*>(address);
                return true;
            }
            if (const int components = VectorComponents(host, typeId); components > 0) {
                const auto* const values = static_cast<const float*>(address);
                out = json::array();
                for (int i = 0; i < components; ++i) {
                    out.push_back(values[i]);
                }
                return true;
            }
            const asITypeInfo* const type = engine.GetTypeInfoById(typeId);
            if (type && (type->GetFlags() & asOBJ_ENUM) != 0 && type->GetSize() == sizeof(std::int32_t)) {
                out = *static_cast<const std::int32_t*>(address);
                return true;
            }
            if (host.GetArrayElementTypeId(typeId) >= 0) {
                const auto* const array = static_cast<const CScriptArray*>(address);
                json elements = json::array();
                for (asUINT i = 0; i < array->GetSize(); ++i) {
                    json element;
                    if (!ValueToJson(host, engine, array->GetElementTypeId(), array->At(i), element)) {
                        return false;
                    }
                    elements.push_back(std::move(element));
                }
                out = std::move(elements);
                return true;
            }
            return false;
        }

        /// @brief JSON の値をメンバ変数へ書く（ValueToJson と同じ型を扱う）
        /// @return 型が合わなければ何もせずに false
        bool JsonToValue(const ScriptHost& host, const asIScriptEngine& engine, int typeId, void* address, const json& in)
        {
            const auto assign = [&in, address]<class T>(T*) {
                if (!in.is_number()) {
                    return false;
                }
                *static_cast<T*>(address) = in.get<T>();
                return true;
                };
            switch (typeId) {
            case asTYPEID_BOOL:
                if (!in.is_boolean()) {
                    return false;
                }
                *static_cast<bool*>(address) = in.get<bool>();
                return true;
            case asTYPEID_INT8: return assign(static_cast<std::int8_t*>(nullptr));
            case asTYPEID_INT16: return assign(static_cast<std::int16_t*>(nullptr));
            case asTYPEID_INT32: return assign(static_cast<std::int32_t*>(nullptr));
            case asTYPEID_INT64: return assign(static_cast<std::int64_t*>(nullptr));
            case asTYPEID_UINT8: return assign(static_cast<std::uint8_t*>(nullptr));
            case asTYPEID_UINT16: return assign(static_cast<std::uint16_t*>(nullptr));
            case asTYPEID_UINT32: return assign(static_cast<std::uint32_t*>(nullptr));
            case asTYPEID_UINT64: return assign(static_cast<std::uint64_t*>(nullptr));
            case asTYPEID_FLOAT: return assign(static_cast<float*>(nullptr));
            case asTYPEID_DOUBLE: return assign(static_cast<double*>(nullptr));
            default: break;
            }
            if ((typeId & asTYPEID_OBJHANDLE) != 0) {
                return false;
            }
            if (typeId == host.GetStringTypeId()) {
                if (!in.is_string()) {
                    return false;
                }
                *static_cast<std::string*>(address) = in.get<std::string>();
                return true;
            }
            if (const int components = VectorComponents(host, typeId); components > 0) {
                if (!in.is_array() || in.size() != static_cast<std::size_t>(components)
                    || !std::all_of(in.begin(), in.end(), [](const json& value) { return value.is_number(); })) {
                    return false;
                }
                auto* const values = static_cast<float*>(address);
                for (int i = 0; i < components; ++i) {
                    values[i] = in[static_cast<std::size_t>(i)].get<float>();
                }
                return true;
            }
            const asITypeInfo* const type = engine.GetTypeInfoById(typeId);
            if (type && (type->GetFlags() & asOBJ_ENUM) != 0 && type->GetSize() == sizeof(std::int32_t)) {
                if (!in.is_number_integer()) {
                    return false;
                }
                *static_cast<std::int32_t*>(address) = in.get<std::int32_t>();
                return true;
            }
            if (host.GetArrayElementTypeId(typeId) >= 0) {
                if (!in.is_array()) {
                    return false;
                }
                auto* const array = static_cast<CScriptArray*>(address);
                array->Resize(static_cast<asUINT>(in.size()));
                for (asUINT i = 0; i < array->GetSize(); ++i) {
                    JsonToValue(host, engine, array->GetElementTypeId(), array->At(i), in[i]);
                }
                return true;
            }
            return false;
        }

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

    struct ScriptEditorExtensions::Persisted
    {
        json values = json::object();
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
            const bool created = found == windows_.end();
            if (created) {
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
            if (created) {
                ApplyPersisted(window);
            }

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
        SavePersisted();
        ScriptBinding::ReleaseEditorGUITextures();
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

    void ScriptEditorExtensions::ApplyPersisted(Window& window)
    {
        if (!persisted_) {
            persisted_ = std::make_unique<Persisted>();
            JsonManager& jsonManager = JsonManager::GetInstance();
            if (jsonManager.FileExists(kPersistedFile)) {
                persisted_->values = jsonManager.LoadJson(kPersistedFile);
            }
            if (!persisted_->values.is_object()) {
                persisted_->values = json::object();
            }
        }
        const auto entry = persisted_->values.find(window.windowClass.className);
        if (entry == persisted_->values.end() || !entry->is_object() || !window.object) {
            return;
        }

        const asITypeInfo* const type = window.object->GetObjectType();
        const asIScriptEngine& engine = *type->GetEngine();
        for (asUINT i = 0; i < type->GetPropertyCount(); ++i) {
            const char* name = nullptr;
            int typeId = 0;
            bool isPrivate = false;
            bool isProtected = false;
            if (type->GetProperty(i, &name, &typeId, &isPrivate, &isProtected) < 0 || !name || isPrivate || isProtected) {
                continue;
            }
            const auto value = entry->find(name);
            void* const address = window.object->GetAddressOfProperty(i);
            if (value != entry->end() && address) {
                JsonToValue(host_, engine, typeId, address, *value);
            }
        }
    }

    void ScriptEditorExtensions::SavePersisted()
    {
        if (windows_.empty()) {
            return;
        }
        if (!persisted_) {
            persisted_ = std::make_unique<Persisted>();
        }
        bool any = false;
        for (const std::unique_ptr<Window>& window : windows_) {
            if (!window->object) {
                continue;
            }
            const asITypeInfo* const type = window->object->GetObjectType();
            const asIScriptEngine& engine = *type->GetEngine();
            json values = json::object();
            for (asUINT i = 0; i < type->GetPropertyCount(); ++i) {
                const char* name = nullptr;
                int typeId = 0;
                bool isPrivate = false;
                bool isProtected = false;
                if (type->GetProperty(i, &name, &typeId, &isPrivate, &isProtected) < 0 || !name || isPrivate || isProtected) {
                    continue;
                }
                const void* const address = window->object->GetAddressOfProperty(i);
                json value;
                if (address && ValueToJson(host_, engine, typeId, address, value)) {
                    values[name] = std::move(value);
                }
            }
            persisted_->values[window->windowClass.className] = std::move(values);
            any = true;
        }
        if (any && !JsonManager::GetInstance().SaveJson(kPersistedFile, persisted_->values)) {
            LogScript(LogLevel::Warn, std::string("エディタのウィンドウの値を保存できませんでした: ") + kPersistedFile);
        }
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
