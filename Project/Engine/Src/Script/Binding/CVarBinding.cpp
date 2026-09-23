#include "pch.h"
#include "Script/Binding/CVarBinding.h"

#include "Script/Binding/BindingRegistrar.h"
#include "Utility/CVar/CVar.h"
#include "Utility/CVar/CVarRegistry.h"
#include "Utility/Logger/Logger.h"

#include <angelscript.h>

#include <string>
#include <unordered_set>

namespace CoreEngine::Script
{
    namespace
    {
        /// @brief 同じ名前で何度も警告を出さないための控え
        /// @details スクリプトは毎フレーム走るので、名前を打ち間違えるとログが埋まる。
        ///          一度言ったことは黙る。
        std::unordered_set<std::string>& WarnedNames()
        {
            static std::unordered_set<std::string> names;
            return names;
        }

        /// @brief この名前でまだ警告していなければ true（一度言ったら黙る）
        bool ShouldWarn(const std::string& key)
        {
            return WarnedNames().insert(key).second;
        }

        /// @brief 型の名前（食い違いの訳に出す）
        const char* TypeName(CVarType type)
        {
            switch (type) {
            case CVarType::Bool:    return "bool";
            case CVarType::Int:     return "int";
            case CVarType::Float:   return "float";
            case CVarType::Vector2: return "Vector2";
            case CVarType::Vector3: return "Vector3";
            case CVarType::Color:   return "Color";
            }
            return "不明";
        }

        /// @brief 名前で引く（無ければ一度だけ警告して nullptr）
        ICVar* Find(const std::string& name)
        {
            ICVar* const cvar = CVarRegistry::Get().Find(name);
            if (!cvar && ShouldWarn("missing:" + name)) {
                Logger::GetInstance().Logf(LogLevel::Warn, LogCategory::Script,
                    "スクリプトが知らない CVar を触りました: {}", name);
            }
            return cvar;
        }

        /// @brief 期待した型かを確かめる（違えば一度だけ警告）
        bool HasType(ICVar* cvar, CVarType expected, const std::string& name)
        {
            if (!cvar || cvar->GetType() == expected) {
                return cvar != nullptr;
            }
            Logger& logger = Logger::GetInstance();
            if (ShouldWarn("type:" + name)) {
                logger.Logf(LogLevel::Warn, LogCategory::Script,
                    "CVar {} の型は {} です（スクリプトは {} として触りました）",
                    name, TypeName(cvar->GetType()), TypeName(expected));
            }
            return false;
        }

        /// @brief 型ごとの読み出し
        template <class T>
        T Get(const std::string& name, CVarType type, const T* (ICVar::*accessor)() const noexcept, T fallback)
        {
            ICVar* const cvar = Find(name);
            if (!HasType(cvar, type, name)) {
                return fallback;
            }
            const T* const value = (cvar->*accessor)();
            return value ? *value : fallback;
        }

        /// @brief 型ごとの書き込み
        template <class T>
        bool Set(const std::string& name, CVarType type, const T& value)
        {
            ICVar* const cvar = Find(name);
            if (!HasType(cvar, type, name)) {
                return false;
            }
            // 範囲のはみ出しは CVar 側が丸める。通番も進むので自動保存と UI が追従する
            cvar->SetFromPointer(&value);
            return true;
        }

        bool Exists(const std::string& name)
        {
            return CVarRegistry::Get().Find(name) != nullptr;
        }

        bool Reset(const std::string& name)
        {
            ICVar* const cvar = Find(name);
            if (!cvar) {
                return false;
            }
            cvar->ResetToDefault();
            return true;
        }

        bool    GetBool(const std::string& name, bool fallback) { return Get<bool>(name, CVarType::Bool, &ICVar::AsBool, fallback); }
        int     GetInt(const std::string& name, int fallback) { return Get<int>(name, CVarType::Int, &ICVar::AsInt, fallback); }
        float   GetFloat(const std::string& name, float fallback) { return Get<float>(name, CVarType::Float, &ICVar::AsFloat, fallback); }
        Vector2 GetVector2(const std::string& name) { return Get<Vector2>(name, CVarType::Vector2, &ICVar::AsVector2, Vector2{}); }
        Vector3 GetVector3(const std::string& name) { return Get<Vector3>(name, CVarType::Vector3, &ICVar::AsVector3, Vector3{}); }
        Vector4 GetColor(const std::string& name) { return Get<Vector4>(name, CVarType::Color, &ICVar::AsColor, Vector4{}); }

        bool SetBool(const std::string& name, bool value) { return Set(name, CVarType::Bool, value); }
        bool SetInt(const std::string& name, int value) { return Set(name, CVarType::Int, value); }
        bool SetFloat(const std::string& name, float value) { return Set(name, CVarType::Float, value); }
        bool SetVector2(const std::string& name, const Vector2& value) { return Set(name, CVarType::Vector2, value); }
        bool SetVector3(const std::string& name, const Vector3& value) { return Set(name, CVarType::Vector3, value); }
        bool SetColor(const std::string& name, const Vector4& value) { return Set(name, CVarType::Color, value); }
    }

    bool RegisterCVarBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        r.Namespace("CVar");

        r.Function("bool Exists(const string &in name)", asFUNCTION(Exists));
        r.Function("bool Reset(const string &in name)", asFUNCTION(Reset));

        r.Function("bool GetBool(const string &in name, bool fallback = false)", asFUNCTION(GetBool));
        r.Function("int GetInt(const string &in name, int fallback = 0)", asFUNCTION(GetInt));
        r.Function("float GetFloat(const string &in name, float fallback = 0.0f)", asFUNCTION(GetFloat));
        r.Function("Vector2 GetVector2(const string &in name)", asFUNCTION(GetVector2));
        r.Function("Vector3 GetVector3(const string &in name)", asFUNCTION(GetVector3));
        r.Function("Vector4 GetColor(const string &in name)", asFUNCTION(GetColor));

        r.Function("bool SetBool(const string &in name, bool value)", asFUNCTION(SetBool));
        r.Function("bool SetInt(const string &in name, int value)", asFUNCTION(SetInt));
        r.Function("bool SetFloat(const string &in name, float value)", asFUNCTION(SetFloat));
        r.Function("bool SetVector2(const string &in name, const Vector2 &in value)", asFUNCTION(SetVector2));
        r.Function("bool SetVector3(const string &in name, const Vector3 &in value)", asFUNCTION(SetVector3));
        r.Function("bool SetColor(const string &in name, const Vector4 &in value)", asFUNCTION(SetColor));

        r.Namespace("");
        return r.Succeeded();
    }
}
