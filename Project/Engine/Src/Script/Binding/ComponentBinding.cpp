#include "pch.h"
#include "Script/Binding/ComponentBinding.h"

#include "GameObject/Component/Core/ComponentFactory.h"
#include "GameObject/Component/Core/IComponent.h"
#include "GameObject/GameObject.h"
#include "Graphics/Asset/AssetDatabase.h"
#include "Graphics/Asset/AssetRef.h"
#include "Math/Vector/Vector2.h"
#include "Math/Vector/Vector3.h"
#include "Math/Vector/Vector4.h"
#include "Reflection/TypeDescriptor.h"
#include "Script/Binding/BindingRegistrar.h"
#include "Script/Binding/GameObjectBinding.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace CoreEngine::Script
{
    namespace
    {
        using Reflection::MethodDescriptor;
        using Reflection::PropertyDescriptor;
        using Reflection::PropertyType;

        /// @brief スクリプトの型 1 つ分（エンジンのコンポーネントの型 1 つ）
        struct EngineComponentType
        {
            std::string name;                                        ///< スクリプトの型名（コンポーネントの型名）
            const Reflection::TypeDescriptor* descriptor = nullptr;  ///< 引き直しに使う記述子
            int typeId = 0;                                          ///< スクリプトの型 ID（ハンドルの印は含まない）
        };

        /// @brief 登録した型（ハンドルが型を指し続けるので、要素のアドレスが変わらない入れ物）
        std::vector<std::unique_ptr<EngineComponentType>>& Types()
        {
            static std::vector<std::unique_ptr<EngineComponentType>> types;
            return types;
        }

        /// @brief スクリプトへ渡すエンジンのコンポーネントのハンドル（どの型も同じ C++ のクラス）
        /// @details GameObject のハンドルの参照を 1 つ持ち、使うたびに記述子が同じコンポーネントを引き直す。
        class ScriptEngineComponent
        {
        public:
            ScriptEngineComponent(ScriptGameObject& owner, const EngineComponentType& type)
                : owner_(owner), type_(type)
            {
                owner_.AddRef();
            }

            ScriptEngineComponent(const ScriptEngineComponent&) = delete;
            ScriptEngineComponent& operator=(const ScriptEngineComponent&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            /// @brief 持ち主の GameObject があり、このコンポーネントが付いているか
            bool Exists() const { return Find() != nullptr; }

            bool IsEnabled() const
            {
                const IComponent* const component = Find();
                return component && component->IsEnabled();
            }

            void SetEnabled(bool enabled)
            {
                if (IComponent* const component = FindOrWarn("有効の切り替え")) {
                    component->SetEnabled(enabled);
                }
            }

            /// @brief 持ち主の GameObject のハンドル（参照を 1 つ足して返す）
            ScriptGameObject* GetGameObject() const
            {
                owner_.AddRef();
                return &owner_;
            }

            IComponent* Find() const
            {
                GameObject* const object = owner_.Resolve();
                if (!object) {
                    return nullptr;
                }
                for (const auto& component : object->GetAllComponents()) {
                    if (component && component->GetTypeDescriptor() == type_.descriptor) {
                        return component.get();
                    }
                }
                return nullptr;
            }

            /// @brief コンポーネントを引き、引けなければ 1 回だけ警告する
            IComponent* FindOrWarn(std::string_view action) const
            {
                IComponent* const component = Find();
                if (!component && !warned_) {
                    warned_ = true;
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "{} で {} をしようとしましたが、GameObject が無いか {} が付いていません",
                        type_.name, action, type_.name);
                }
                return component;
            }

        private:
            ~ScriptEngineComponent() { owner_.Release(); }

            ScriptGameObject& owner_;
            const EngineComponentType& type_;
            mutable int refCount_ = 1;
            mutable bool warned_ = false;
        };

        ScriptEngineComponent* Self(asIScriptGeneric* generic)
        {
            return static_cast<ScriptEngineComponent*>(generic->GetObject());
        }

        /// @brief 値の受け渡しに使う入れ物
        using Value = std::variant<bool, int, float, Vector2, Vector3, Vector4, std::string>;

        /// @brief 型の初期値（戻り値の置き場に使う）
        Value DefaultValue(PropertyType type)
        {
            switch (type) {
            case PropertyType::Bool:    return false;
            case PropertyType::Int:     return 0;
            case PropertyType::Float:   return 0.0f;
            case PropertyType::Vector2: return Vector2{};
            case PropertyType::Vector3: return Vector3{};
            case PropertyType::Vector4:
            case PropertyType::Color:   return Vector4{};
            default:                    return std::string{};
            }
        }

        /// @brief 汎用の呼び出しの戻り値へ値を入れる
        void SetReturn(asIScriptGeneric* generic, Value& value)
        {
            std::visit([generic](auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    generic->SetReturnByte(v ? 1 : 0);
                } else if constexpr (std::is_same_v<T, int>) {
                    generic->SetReturnDWord(static_cast<asDWORD>(v));
                } else if constexpr (std::is_same_v<T, float>) {
                    generic->SetReturnFloat(v);
                } else {
                    generic->SetReturnObject(&v);
                }
            }, value);
        }

        /// @brief 汎用の呼び出しの i 番目の引数を読む
        Value ReadArgument(asIScriptGeneric* generic, asUINT index, PropertyType type)
        {
            switch (type) {
            case PropertyType::Bool:    return generic->GetArgByte(index) != 0;
            case PropertyType::Int:     return static_cast<int>(generic->GetArgDWord(index));
            case PropertyType::Float:   return generic->GetArgFloat(index);
            case PropertyType::Vector2: return *static_cast<const Vector2*>(generic->GetArgAddress(index));
            case PropertyType::Vector3: return *static_cast<const Vector3*>(generic->GetArgAddress(index));
            case PropertyType::Vector4:
            case PropertyType::Color:   return *static_cast<const Vector4*>(generic->GetArgAddress(index));
            default:                    return *static_cast<const std::string*>(generic->GetArgAddress(index));
            }
        }

        void GenericGetProperty(asIScriptGeneric* generic)
        {
            const auto& property = *static_cast<const PropertyDescriptor*>(generic->GetAuxiliary());
            IComponent* const component = Self(generic)->FindOrWarn(std::string("読み取り（") + property.name + "）");
            void* const instance = component ? component->GetReflectionInstance() : nullptr;

            if (property.type == PropertyType::AssetRef) {
                Reflection::AssetRefValue asset;
                if (instance) {
                    property.Get(instance, &asset);
                }
                Value path = asset.path;
                SetReturn(generic, path);
                return;
            }

            Value value = DefaultValue(property.type);
            if (instance) {
                std::visit([&](auto& v) { property.Get(instance, &v); }, value);
            }
            SetReturn(generic, value);
        }

        void GenericSetProperty(asIScriptGeneric* generic)
        {
            const auto& property = *static_cast<const PropertyDescriptor*>(generic->GetAuxiliary());
            IComponent* const component = Self(generic)->FindOrWarn(std::string("書き込み（") + property.name + "）");
            if (!component) {
                return;
            }
            void* const instance = component->GetReflectionInstance();

            if (property.type == PropertyType::AssetRef) {
                // パスから GUID を引く（引けなければパスだけで持つ）
                Reflection::AssetRefValue asset;
                asset.path = *static_cast<const std::string*>(generic->GetArgAddress(0));
                if (const AssetInfo* const info = ResolveAssetRef(asset)) {
                    asset.guid = info->guid;
                    asset.path = ToAssetPath(*info);
                }
                property.Set(instance, &asset);
            } else {
                Value value = ReadArgument(generic, 0, property.type);
                std::visit([&](auto& v) { property.Set(instance, &v); }, value);
            }
            component->OnPropertyChanged(property);
        }

        void GenericInvokeMethod(asIScriptGeneric* generic)
        {
            const auto& method = *static_cast<const MethodDescriptor*>(generic->GetAuxiliary());
            IComponent* const component = Self(generic)->FindOrWarn(method.name);

            std::vector<Value> values;
            values.reserve(method.parameters.size());
            for (asUINT i = 0; i < method.parameters.size(); ++i) {
                values.push_back(ReadArgument(generic, i, method.parameters[i]));
            }
            std::vector<const void*> arguments;
            arguments.reserve(values.size());
            for (const Value& value : values) {
                arguments.push_back(std::visit([](const auto& v) -> const void* { return &v; }, value));
            }

            Value result = DefaultValue(method.hasReturn ? method.returnType : PropertyType::Bool);
            if (component) {
                void* const resultAddress = std::visit([](auto& v) -> void* { return &v; }, result);
                method.Invoke(component->GetReflectionInstance(), arguments.data(), resultAddress);
            }
            if (method.hasReturn) {
                SetReturn(generic, result);
            }
        }

        /// @brief GameObject からハンドルを作るプロパティ（`owner.particleSystem`）
        void GenericGetFromObject(asIScriptGeneric* generic)
        {
            auto* const owner = static_cast<ScriptGameObject*>(generic->GetObject());
            const auto& type = *static_cast<const EngineComponentType*>(generic->GetAuxiliary());
            generic->SetReturnAddress(new ScriptEngineComponent(*owner, type));
        }

        /// @brief スクリプトで値として受け取る型の宣言（使えない型なら nullptr）
        const char* ValueDeclaration(PropertyType type)
        {
            switch (type) {
            case PropertyType::Bool:     return "bool";
            case PropertyType::Int:      return "int";
            case PropertyType::Float:    return "float";
            case PropertyType::Vector2:  return "Vector2";
            case PropertyType::Vector3:  return "Vector3";
            case PropertyType::Vector4:
            case PropertyType::Color:    return "Vector4";
            case PropertyType::String:
            case PropertyType::AssetRef: return "string";
            default:                     return nullptr;
            }
        }

        /// @brief スクリプトで引数として受け取る型の宣言（使えない型なら nullptr）
        std::string ParameterDeclaration(PropertyType type)
        {
            const char* const value = ValueDeclaration(type);
            if (!value) {
                return {};
            }
            switch (type) {
            case PropertyType::Bool:
            case PropertyType::Int:
            case PropertyType::Float:
                return value;
            default:
                return std::string("const ") + value + " &in";
            }
        }

        /// @brief スクリプトの識別子として使える名前か
        bool IsIdentifier(std::string_view name)
        {
            if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
                return false;
            }
            for (const char c : name) {
                if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) {
                    return false;
                }
            }
            return true;
        }

        /// @brief GameObject のプロパティの名前（型名の先頭を小文字にする）
        std::string LowerFirst(const std::string& name)
        {
            std::string result = name;
            if (!result.empty()) {
                result[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[0])));
            }
            return result;
        }

        /// @brief 型 1 つ分を登録する
        void RegisterType(BindingRegistrar& r, EngineComponentType& type)
        {
            const char* const name = type.name.c_str();
            r.ReferenceType(name, asOBJ_REF);
            r.Behaviour(name, asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptEngineComponent, AddRef), asCALL_THISCALL);
            r.Behaviour(name, asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptEngineComponent, Release), asCALL_THISCALL);
            r.Method(name, "bool get_exists() const property", asMETHOD(ScriptEngineComponent, Exists), asCALL_THISCALL);
            r.Method(name, "bool get_enabled() const property", asMETHOD(ScriptEngineComponent, IsEnabled), asCALL_THISCALL);
            r.Method(name, "void set_enabled(bool) property", asMETHOD(ScriptEngineComponent, SetEnabled), asCALL_THISCALL);
            r.Method(name, "GameObject@ get_gameObject() const property", asMETHOD(ScriptEngineComponent, GetGameObject), asCALL_THISCALL);

            static constexpr std::array<std::string_view, 3> kReserved = { "exists", "enabled", "gameObject" };
            for (const PropertyDescriptor& property : type.descriptor->properties) {
                const char* const valueDeclaration = ValueDeclaration(property.type);
                const bool reserved = std::find(kReserved.begin(), kReserved.end(), property.name) != kReserved.end();
                if (!valueDeclaration || !property.get || reserved || !IsIdentifier(property.name)) {
                    continue;
                }
                auto* const auxiliary = const_cast<PropertyDescriptor*>(&property);
                r.GenericMethod(name, (std::string(valueDeclaration) + " get_" + property.name + "() const property").c_str(),
                    &GenericGetProperty, auxiliary);
                if (property.IsEditable() && property.set) {
                    r.GenericMethod(name, ("void set_" + property.name + "(" + ParameterDeclaration(property.type) + ") property").c_str(),
                        &GenericSetProperty, auxiliary);
                }
            }

            for (const MethodDescriptor& method : type.descriptor->methods) {
                std::string parameters;
                bool usable = IsIdentifier(method.name) && method.invoke;
                for (const PropertyType parameter : method.parameters) {
                    const std::string declaration = ParameterDeclaration(parameter);
                    usable = usable && !declaration.empty();
                    parameters += (parameters.empty() ? "" : ", ") + declaration;
                }
                const char* const returnDeclaration = method.hasReturn ? ValueDeclaration(method.returnType) : "void";
                if (!usable || !returnDeclaration) {
                    Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                        "{} の操作 {} は、引数か戻り値の型がスクリプトで使えないので出しません", type.name, method.name);
                    continue;
                }
                r.GenericMethod(name, (std::string(returnDeclaration) + " " + method.name + "(" + parameters + ")").c_str(),
                    &GenericInvokeMethod, const_cast<MethodDescriptor*>(&method));
            }

            r.GenericMethod("GameObject", (type.name + "@ get_" + LowerFirst(type.name) + "() property").c_str(),
                &GenericGetFromObject, &type);
        }
    }

    bool RegisterComponentBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        Types().clear();

        // 型の一覧を作るのに、型ごとの名前と記述子を引いておく（何度呼んでも 1 回だけ）
        ComponentFactory& factory = ComponentFactory::Get();
        factory.Prime();

        BindingRegistrar r(engine);
        for (const std::string& typeName : factory.GetRegisteredTypeNames()) {
            const Reflection::TypeDescriptor* const descriptor = factory.FindDescriptor(typeName);
            if (!descriptor || factory.IsRuntimeType(typeName) || !IsIdentifier(typeName)
                || engine->GetTypeInfoByName(typeName.c_str())) {
                continue;
            }
            auto type = std::make_unique<EngineComponentType>();
            type->name = typeName;
            type->descriptor = descriptor;
            RegisterType(r, *type);
            type->typeId = engine->GetTypeIdByDecl(typeName.c_str());
            Types().push_back(std::move(type));
        }
        return r.Succeeded();
    }

    std::optional<bool> GetEngineComponent(ScriptGameObject& owner, void* reference, int typeId)
    {
        const int objectTypeId = typeId & ~(asTYPEID_OBJHANDLE | asTYPEID_HANDLETOCONST);
        for (const auto& type : Types()) {
            if (type->typeId != objectTypeId) {
                continue;
            }
            auto* const handle = new ScriptEngineComponent(owner, *type);
            if (!handle->Exists()) {
                handle->Release();
                return false;
            }
            *static_cast<ScriptEngineComponent**>(reference) = handle;
            return true;
        }
        return std::nullopt;
    }
}
