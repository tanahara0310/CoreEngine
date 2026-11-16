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

        /// @brief 名前で引いて期待した型の CVar として返す（無いか型が違えば一度だけ警告して nullptr）
        template <class T>
        CVar<T>* FindAs(const std::string& name)
        {
            ICVar* const cvar = Find(name);
            if (!cvar) {
                return nullptr;
            }
            if (CVar<T>* const typed = cvar->As<T>()) {
                return typed;
            }
            Logger& logger = Logger::GetInstance();
            if (ShouldWarn("type:" + name)) {
                logger.Logf(LogLevel::Warn, LogCategory::Script,
                    "CVar {} の型は {} です（スクリプトは {} として触りました）",
                    name, CVarTypeName(cvar->GetType()), CVarTypeName(kCVarTypeOf<T>));
            }
            return nullptr;
        }

        /// @brief 型ごとの読み出し
        template <class T>
        T Get(const std::string& name, T fallback)
        {
            const CVar<T>* const cvar = FindAs<T>(name);
            return cvar ? cvar->Get() : fallback;
        }

        /// @brief 型ごとの書き込み
        template <class T>
        bool Set(const std::string& name, const T& value)
        {
            CVar<T>* const cvar = FindAs<T>(name);
            if (!cvar) {
                return false;
            }
            // 範囲のはみ出しは CVar 側が丸める。通番も進むので自動保存と UI が追従する
            cvar->Set(value);
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

        bool    GetBool(const std::string& name, bool fallback) { return Get<bool>(name, fallback); }
        int     GetInt(const std::string& name, int fallback) { return Get<int>(name, fallback); }
        float   GetFloat(const std::string& name, float fallback) { return Get<float>(name, fallback); }
        Vector2 GetVector2(const std::string& name) { return Get<Vector2>(name, Vector2{}); }
        Vector3 GetVector3(const std::string& name) { return Get<Vector3>(name, Vector3{}); }
        Vector4 GetColor(const std::string& name) { return Get<Vector4>(name, Vector4{}); }

        bool SetBool(const std::string& name, bool value) { return Set(name, value); }
        bool SetInt(const std::string& name, int value) { return Set(name, value); }
        bool SetFloat(const std::string& name, float value) { return Set(name, value); }
        bool SetVector2(const std::string& name, const Vector2& value) { return Set(name, value); }
        bool SetVector3(const std::string& name, const Vector3& value) { return Set(name, value); }
        bool SetColor(const std::string& name, const Vector4& value) { return Set(name, value); }
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
