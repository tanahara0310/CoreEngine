#include "pch.h"
#include "Script/ScriptComponentType.h"

#include "Script/Metadata/MetadataParser.h"
#include "Script/ScriptComponent.h"
#include "Script/ScriptHost.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>
#include <scriptbuilder/scriptbuilder.h>

#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CoreEngine
{
    namespace
    {
        /// 基底クラスで持ち主のハンドルを入れるメンバ変数の名前
        constexpr const char* kOwnerPropertyName = "owner_";

        /// ライフサイクルの関数の宣言（`ScriptComponentType::Method` の並び）
        constexpr const char* kMethodDeclarations[] = {
            "void Awake()",
            "void Start()",
            "void Update()",
            "void LateUpdate()",
            "void OnDestroy()",
            "void OnScriptReloaded()",
            "void OnCollisionEnter(Collision@)",
            "void OnCollisionStay(Collision@)",
            "void OnCollisionExit(Collision@)",
            "void OnTriggerEnter(Collision@)",
            "void OnTriggerStay(Collision@)",
            "void OnTriggerExit(Collision@)",
        };

        /// ライフサイクルの関数の名前（`ScriptComponentType::Method` の並び）
        constexpr const char* kMethodNames[] = {
            "Awake",
            "Start",
            "Update",
            "LateUpdate",
            "OnDestroy",
            "OnScriptReloaded",
            "OnCollisionEnter",
            "OnCollisionStay",
            "OnCollisionExit",
            "OnTriggerEnter",
            "OnTriggerStay",
            "OnTriggerExit",
        };

        static_assert(std::size(kMethodDeclarations) == static_cast<std::size_t>(ScriptComponentType::Method::Count));
        static_assert(std::size(kMethodNames) == static_cast<std::size_t>(ScriptComponentType::Method::Count));

        void ReadScriptProperty(const Reflection::PropertyDescriptor& property, const void* instance, void* out)
        {
            static_cast<const ScriptComponent*>(instance)->ReadProperty(property, out);
        }

        void WriteScriptProperty(const Reflection::PropertyDescriptor& property, void* instance, const void* in)
        {
            static_cast<ScriptComponent*>(instance)->WriteProperty(property, in);
        }

        /// @brief スクリプトの型 ID から記述子のプロパティの型を引く
        /// @return インスペクタに出せない型なら空
        std::optional<Reflection::PropertyType> ToPropertyType(const ScriptHost& host, int typeId)
        {
            switch (typeId) {
            case asTYPEID_BOOL:  return Reflection::PropertyType::Bool;
            case asTYPEID_INT32: return Reflection::PropertyType::Int;
            case asTYPEID_FLOAT: return Reflection::PropertyType::Float;
            default:             break;
            }
            if (typeId == host.GetStringTypeId()) {
                return Reflection::PropertyType::String;
            }
            if (typeId == host.GetVector2TypeId()) {
                return Reflection::PropertyType::Vector2;
            }
            if (typeId == host.GetVector3TypeId()) {
                return Reflection::PropertyType::Vector3;
            }
            if (typeId == host.GetVector4TypeId()) {
                return Reflection::PropertyType::Vector4;
            }
            return std::nullopt;
        }

        void WarnScript(const std::string& message)
        {
            Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script, "{}", message);
        }

        /// @brief アセットの種類の名前（AssetTypeToString と同じ綴り）から種類を引く
        std::optional<AssetType> ParseAssetType(const std::string& name)
        {
            for (const AssetType type : { AssetType::Texture, AssetType::Model, AssetType::Shader, AssetType::Audio,
                                          AssetType::Material, AssetType::Scene, AssetType::Prefab, AssetType::Animation,
                                          AssetType::MaterialLibrary, AssetType::Json, AssetType::Csv }) {
                if (AssetTypeToString(type) == name) {
                    return type;
                }
            }
            return std::nullopt;
        }

        /// @brief 属性の並びに、指定した名前の属性があるか（読めない並びは飛ばす。理由は属性を当てるときに出す）
        bool HasAttribute(const std::vector<std::string>& metadata, const char* name)
        {
            std::vector<Script::MetadataAttribute> attributes;
            std::string error;
            for (const std::string& block : metadata) {
                if (!Script::ParseMetadata(block, attributes, error)) {
                    continue;
                }
                for (const Script::MetadataAttribute& attribute : attributes) {
                    if (attribute.name == name) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// @brief ハンドルの型 ID が ScriptComponent を継いだスクリプトのクラスを指すなら、そのクラスの型を返す
        const asITypeInfo* FindComponentClass(asIScriptEngine* engine, int typeId, const asITypeInfo* base)
        {
            if (!engine || !base || (typeId & asTYPEID_OBJHANDLE) == 0 || (typeId & asTYPEID_SCRIPTOBJECT) == 0) {
                return nullptr;
            }
            const asITypeInfo* const type = engine->GetTypeInfoById(typeId);
            return (type && type != base && type->DerivesFrom(base)) ? type : nullptr;
        }
    }

    ScriptComponentType::ScriptComponentType(ScriptHost& host, asITypeInfo* type, asITypeInfo* base, CScriptBuilder& builder)
        : host_(host)
        , typeInfo_(type)
    {
        typeInfo_->AddRef();
        name_ = typeInfo_->GetName();
        displayName_ = name_;
        ReadClassAttributes(builder);

        descriptor_.name = name_.c_str();
        descriptor_.displayName = displayName_.c_str();
        BuildProperties(base, builder);
        FindMethods(base);
        FindOwnerProperty();
    }

    ScriptComponentType::~ScriptComponentType()
    {
        for (asIScriptFunction* method : methods_) {
            if (method) {
                method->Release();
            }
        }
        if (typeInfo_) {
            typeInfo_->Release();
        }
    }

    const char* ScriptComponentType::GetMethodName(Method method)
    {
        const auto index = static_cast<std::size_t>(method);
        return index < std::size(kMethodNames) ? kMethodNames[index] : "?";
    }

    void ScriptComponentType::ReadClassAttributes(CScriptBuilder& builder)
    {
        std::vector<Script::MetadataAttribute> attributes;
        std::string error;
        for (const std::string& block : builder.GetMetadataForType(typeInfo_->GetTypeId())) {
            if (!Script::ParseMetadata(block, attributes, error)) {
                WarnScript(name_ + ": クラスの属性を読めません（" + error + "）: [" + block + "]");
                continue;
            }
            for (const Script::MetadataAttribute& attribute : attributes) {
                if (attribute.name != "DisplayName") {
                    WarnScript(name_ + ": クラスには属性「" + attribute.name + "」を付けられません");
                    continue;
                }
                if (attribute.arguments.size() != 1) {
                    WarnScript(name_ + ": 属性「DisplayName」の引数は表示名 1 つです");
                    continue;
                }
                displayName_ = attribute.arguments.front();
            }
        }
    }

    void ScriptComponentType::BuildProperties(asITypeInfo* base, CScriptBuilder& builder)
    {
        std::unordered_set<std::string> inherited;
        for (asUINT i = 0; i < base->GetPropertyCount(); ++i) {
            const char* name = nullptr;
            if (base->GetProperty(i, &name) >= 0 && name) {
                inherited.insert(name);
            }
        }

        std::vector<Script::MetadataAttribute> attributes;
        std::string error;
        for (asUINT i = 0; i < typeInfo_->GetPropertyCount(); ++i) {
            const char* name = nullptr;
            int typeId = 0;
            bool isPrivate = false;
            bool isProtected = false;
            if (typeInfo_->GetProperty(i, &name, &typeId, &isPrivate, &isProtected) < 0 || !name) {
                continue;
            }
            if (inherited.contains(name) || isPrivate || isProtected) {
                continue;
            }

            const std::vector<std::string> metadata =
                builder.GetMetadataForTypeProperty(typeInfo_->GetTypeId(), static_cast<int>(i));

            std::optional<Reflection::PropertyType> propertyType = ToPropertyType(host_, typeId);
            std::optional<Reflection::PropertyType> elementType;
            if (!propertyType) {
                const int elementTypeId = host_.GetArrayElementTypeId(typeId);
                elementType = elementTypeId >= 0 ? ToPropertyType(host_, elementTypeId) : std::nullopt;
                if (elementType) {
                    propertyType = Reflection::PropertyType::Array;
                }
            }
            // [ObjectRef] を付けた GameObject@ と、ScriptComponent を継いだクラスのハンドルは、別オブジェクトへの参照にする
            const char* referenceTypeName = nullptr;
            if (!propertyType && HasAttribute(metadata, "ObjectRef")) {
                if (typeId == host_.GetGameObjectHandleTypeId()) {
                    propertyType = Reflection::PropertyType::ObjectRef;
                } else if (const asITypeInfo* const referenced = FindComponentClass(typeInfo_->GetEngine(), typeId, base)) {
                    propertyType = Reflection::PropertyType::ObjectRef;
                    referenceTypeName = StoreText(referenced->GetName());
                }
            }
            if (!propertyType) {
                const char* declaration = host_.GetTypeDeclaration(typeId);
                WarnScript(name_ + "." + name + ": " + (declaration ? declaration : "?") +
                    " 型のメンバ変数はインスペクタに出せず、保存もしません");
                continue;
            }

            Reflection::PropertyDescriptor property;
            property.name = name;
            property.displayName = StoreText(name);
            property.type = *propertyType;
            if (elementType) {
                property.elementType = *elementType;
            }
            property.get = &ReadScriptProperty;
            property.set = &WriteScriptProperty;
            property.index = i;
            property.acceptsComponentType = referenceTypeName;

            for (const std::string& block : metadata) {
                if (!Script::ParseMetadata(block, attributes, error)) {
                    WarnScript(name_ + "." + name + ": 属性を読めません（" + error + "）: [" + block + "]");
                    continue;
                }
                for (const Script::MetadataAttribute& attribute : attributes) {
                    if (!ApplyPropertyAttribute(property, attribute, error)) {
                        WarnScript(name_ + "." + name + ": " + error);
                    }
                }
            }
            descriptor_.properties.push_back(std::move(property));
        }
    }

    void ScriptComponentType::FindMethods(asITypeInfo* base)
    {
        for (std::size_t i = 0; i < methods_.size(); ++i) {
            asIScriptFunction* const own = typeInfo_->GetMethodByDecl(kMethodDeclarations[i], false);
            const asIScriptFunction* const inherited = base->GetMethodByDecl(kMethodDeclarations[i], false);
            if (own && own != inherited) {
                own->AddRef();
                methods_[i] = own;
            }
        }
    }

    void ScriptComponentType::FindOwnerProperty()
    {
        for (asUINT i = 0; i < typeInfo_->GetPropertyCount(); ++i) {
            const char* name = nullptr;
            int typeId = 0;
            if (typeInfo_->GetProperty(i, &name, &typeId) < 0 || !name || std::string_view(name) != kOwnerPropertyName) {
                continue;
            }
            if (typeId != host_.GetGameObjectHandleTypeId()) {
                WarnScript(name_ + ": 基底クラスの " + kOwnerPropertyName + " が GameObject@ ではないので、owner を入れられません");
                return;
            }
            ownerPropertyIndex_ = static_cast<int>(i);
            return;
        }
        WarnScript(name_ + ": 基底クラスに " + kOwnerPropertyName + " が無いので、owner を入れられません");
    }

    bool ScriptComponentType::ApplyPropertyAttribute(Reflection::PropertyDescriptor& property,
        const Script::MetadataAttribute& attribute, std::string& error)
    {
        const std::vector<std::string>& arguments = attribute.arguments;
        const auto readNumber = [&](const std::string& argument, float& out) {
            if (const std::optional<float> value = Script::ParseFloatArgument(argument)) {
                out = *value;
                return true;
            }
            error = "属性「" + attribute.name + "」の引数「" + argument + "」が数値ではありません";
            return false;
        };

        if (attribute.name == "DisplayName") {
            if (arguments.size() != 1) {
                error = "属性「DisplayName」の引数は表示名 1 つです";
                return false;
            }
            property.displayName = StoreText(arguments.front());
            return true;
        }

        if (attribute.name == "Range") {
            if (arguments.size() != 2 && arguments.size() != 3) {
                error = "属性「Range」の引数は（最小, 最大）か（最小, 最大, ドラッグ速度）です";
                return false;
            }
            float minValue = 0.0f;
            float maxValue = 0.0f;
            float speed = property.range.speed;
            if (!readNumber(arguments[0], minValue) || !readNumber(arguments[1], maxValue)) {
                return false;
            }
            if (arguments.size() == 3 && !readNumber(arguments[2], speed)) {
                return false;
            }
            property.range = Reflection::PropertyRange(minValue, maxValue, speed);
            return true;
        }

        if (attribute.name == "Speed") {
            if (arguments.size() != 1) {
                error = "属性「Speed」の引数はドラッグ速度 1 つです";
                return false;
            }
            float speed = 0.0f;
            if (!readNumber(arguments.front(), speed)) {
                return false;
            }
            property.range.speed = speed;
            return true;
        }

        if (attribute.name == "Color") {
            if (!arguments.empty()) {
                error = "属性「Color」は引数を取りません";
                return false;
            }
            if (property.type == Reflection::PropertyType::Array &&
                property.elementType == Reflection::PropertyType::Vector4) {
                property.elementType = Reflection::PropertyType::Color;
                return true;
            }
            if (property.type != Reflection::PropertyType::Vector4) {
                error = "属性「Color」は Vector4 か array<Vector4> のメンバ変数にだけ付けられます";
                return false;
            }
            property.type = Reflection::PropertyType::Color;
            return true;
        }

        if (attribute.name == "ObjectRef") {
            if (!arguments.empty()) {
                error = "属性「ObjectRef」は引数を取りません";
                return false;
            }
            if (property.type != Reflection::PropertyType::ObjectRef) {
                error = "属性「ObjectRef」は GameObject@ か、ScriptComponent を継いだクラスのハンドルのメンバ変数にだけ付けられます";
                return false;
            }
            return true;
        }

        if (attribute.name == "Tooltip") {
            if (arguments.size() != 1) {
                error = "属性「Tooltip」の引数は説明 1 つです";
                return false;
            }
            property.tooltip = StoreText(arguments.front());
            return true;
        }

        if (attribute.name == "Asset") {
            if (arguments.size() != 1) {
                error = "属性「Asset」の引数はアセットの種類 1 つです（Texture / Model / Audio / Prefab など）";
                return false;
            }
            if (property.type != Reflection::PropertyType::String) {
                error = "属性「Asset」は string のメンバ変数にだけ付けられます";
                return false;
            }
            const std::optional<AssetType> assetType = ParseAssetType(arguments.front());
            if (!assetType) {
                error = "属性「Asset」の種類「" + arguments.front() + "」はありません";
                return false;
            }
            property.type = Reflection::PropertyType::AssetRef;
            property.assetType = *assetType;
            return true;
        }

        Reflection::PropertyFlags flag = Reflection::PropertyFlags::None;
        if (attribute.name == "ReadOnly") {
            flag = Reflection::PropertyFlags::ReadOnly;
        } else if (attribute.name == "Hidden") {
            flag = Reflection::PropertyFlags::Hidden;
        } else if (attribute.name == "Transient") {
            flag = Reflection::PropertyFlags::NoSave;
        } else {
            error = "属性「" + attribute.name + "」はメンバ変数に付けられません";
            return false;
        }
        if (!arguments.empty()) {
            error = "属性「" + attribute.name + "」は引数を取りません";
            return false;
        }
        property.flags = property.flags | flag;
        return true;
    }

    const char* ScriptComponentType::StoreText(std::string text)
    {
        texts_.push_back(std::move(text));
        return texts_.back().c_str();
    }
}
