#include "pch.h"
#include "Script/Binding/BindingRegistrar.h"

#include "Utility/Logger/Logger.h"

namespace CoreEngine::Script
{
    void BindingRegistrar::Namespace(const char* name)
    {
        Check(engine_->SetDefaultNamespace(name ? name : ""), "名前空間", name ? name : "");
    }

    void BindingRegistrar::ValueType(const char* name, int byteSize, asQWORD flags)
    {
        Check(engine_->RegisterObjectType(name, byteSize, flags), "値型", name);
    }

    void BindingRegistrar::ReferenceType(const char* name, asQWORD flags)
    {
        Check(engine_->RegisterObjectType(name, 0, flags), "参照型", name);
    }

    void BindingRegistrar::Behaviour(const char* type, asEBehaviours behaviour, const char* declaration,
                                     const asSFuncPtr& function, asDWORD callConvention)
    {
        Check(engine_->RegisterObjectBehaviour(type, behaviour, declaration, function, callConvention),
            "振る舞い", std::string(type) + " " + declaration);
    }

    void BindingRegistrar::Method(const char* type, const char* declaration, const asSFuncPtr& function,
                                  asDWORD callConvention)
    {
        Check(engine_->RegisterObjectMethod(type, declaration, function, callConvention),
            "メソッド", std::string(type) + " " + declaration);
    }

    void BindingRegistrar::Property(const char* type, const char* declaration, int byteOffset)
    {
        Check(engine_->RegisterObjectProperty(type, declaration, byteOffset),
            "プロパティ", std::string(type) + " " + declaration);
    }

    void BindingRegistrar::Function(const char* declaration, const asSFuncPtr& function, asDWORD callConvention)
    {
        Check(engine_->RegisterGlobalFunction(declaration, function, callConvention), "関数", declaration);
    }

    void BindingRegistrar::Funcdef(const char* declaration)
    {
        Check(engine_->RegisterFuncdef(declaration), "funcdef", declaration);
    }

    void BindingRegistrar::Enum(const char* name)
    {
        Check(engine_->RegisterEnum(name), "列挙", name);
    }

    void BindingRegistrar::EnumValue(const char* type, const char* name, int value)
    {
        Check(engine_->RegisterEnumValue(type, name, value), "列挙の値", std::string(type) + "::" + name);
    }

    void BindingRegistrar::Check(int result, const char* what, const std::string& declaration)
    {
        if (result >= 0) {
            return;
        }
        succeeded_ = false;
        Logger::GetInstance().Logf(LogLevel::Error, LogCategory::Script,
            "スクリプトへの{}の登録に失敗しました（{}）: {}", what, result, declaration);
    }
}
