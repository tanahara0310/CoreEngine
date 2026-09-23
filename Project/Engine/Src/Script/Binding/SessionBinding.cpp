#include "pch.h"
#include "Script/Binding/SessionBinding.h"

#include "Script/Binding/BindingRegistrar.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Session/SessionValues.h"

#include <angelscript.h>

#include <string>
#include <utility>

namespace CoreEngine::Script
{
    namespace
    {
        /// 保存データのファイルの置き場（プロジェクトの根からの相対）
        constexpr const char* kSaveRoot = "Application/Saved/";

        // ---------------------------------------------------------------- Session

        void SessionSetInt(const std::string& key, int value)
        {
            SessionValues::SetInt(key, value);
        }

        int SessionGetInt(const std::string& key, int fallback)
        {
            return static_cast<int>(SessionValues::GetInt(key, fallback));
        }

        void SessionSetFloat(const std::string& key, float value)
        {
            SessionValues::SetFloat(key, value);
        }

        float SessionGetFloat(const std::string& key, float fallback)
        {
            return SessionValues::GetFloat(key, fallback);
        }

        void SessionSetBool(const std::string& key, bool value)
        {
            SessionValues::SetBool(key, value);
        }

        bool SessionGetBool(const std::string& key, bool fallback)
        {
            return SessionValues::GetBool(key, fallback);
        }

        void SessionSetString(const std::string& key, const std::string& value)
        {
            SessionValues::SetString(key, value);
        }

        std::string SessionGetString(const std::string& key, const std::string& fallback)
        {
            return SessionValues::GetString(key, fallback);
        }

        bool SessionHas(const std::string& key)
        {
            return SessionValues::Has(key);
        }

        void SessionRemove(const std::string& key)
        {
            SessionValues::Remove(key);
        }

        // ---------------------------------------------------------------- SaveFile

        /// @brief 保存データの置き場からの相対パスとして使えるか（空・絶対パス・.. を含むものは使えない）
        bool IsSaveRelativePath(const std::string& path)
        {
            if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != std::string::npos) {
                return false;
            }
            std::size_t start = 0;
            while (start <= path.size()) {
                const std::size_t end = path.find_first_of("/\\", start);
                const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (part == "..") {
                    return false;
                }
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            return true;
        }

        /// @brief `Application/Saved` の下の JSON ファイル 1 つ（読み込んだ中身を持ち、Save で書き出す）
        class ScriptSaveFile
        {
        public:
            explicit ScriptSaveFile(std::string relativePath) : relativePath_(std::move(relativePath))
            {
                JsonManager& jm = JsonManager::GetInstance();
                const std::string path = FullPath();
                if (jm.FileExists(path)) {
                    data_ = jm.LoadJson(path);
                }
                if (!data_.is_object()) {
                    data_ = json::object();
                }
            }

            ScriptSaveFile(const ScriptSaveFile&) = delete;
            ScriptSaveFile& operator=(const ScriptSaveFile&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            const std::string& GetPath() const { return relativePath_; }

            bool Has(const std::string& key) const { return data_.contains(key); }

            int GetInt(const std::string& key, int fallback) const
            {
                const auto it = data_.find(key);
                return (it != data_.end() && it->is_number_integer()) ? it->get<int>() : fallback;
            }

            float GetFloat(const std::string& key, float fallback) const
            {
                const auto it = data_.find(key);
                return (it != data_.end() && it->is_number()) ? it->get<float>() : fallback;
            }

            bool GetBool(const std::string& key, bool fallback) const
            {
                const auto it = data_.find(key);
                return (it != data_.end() && it->is_boolean()) ? it->get<bool>() : fallback;
            }

            std::string GetString(const std::string& key, const std::string& fallback) const
            {
                const auto it = data_.find(key);
                return (it != data_.end() && it->is_string()) ? it->get<std::string>() : fallback;
            }

            void SetInt(const std::string& key, int value) { data_[key] = value; }
            void SetFloat(const std::string& key, float value) { data_[key] = value; }
            void SetBool(const std::string& key, bool value) { data_[key] = value; }
            void SetString(const std::string& key, const std::string& value) { data_[key] = value; }

            void Remove(const std::string& key) { data_.erase(key); }

            /// @brief 中身をファイルへ書き出す（フォルダが無ければ作る）
            bool Save() const
            {
                return JsonManager::GetInstance().SaveJson(FullPath(), data_);
            }

        private:
            ~ScriptSaveFile() = default;

            std::string FullPath() const { return std::string(kSaveRoot) + relativePath_; }

            std::string relativePath_;
            json data_;
            mutable int refCount_ = 1;
        };

        ScriptSaveFile* CreateSaveFile(const std::string& relativePath)
        {
            if (!IsSaveRelativePath(relativePath)) {
                if (asIScriptContext* const context = asGetActiveContext()) {
                    context->SetException("SaveFile には Application/Saved からの相対パスを渡します（空・絶対パス・.. は使えません）");
                }
                return nullptr;
            }
            return new ScriptSaveFile(relativePath);
        }

        void RegisterSession(BindingRegistrar& r)
        {
            r.Namespace("Session");
            r.Function("void SetInt(const string &in key, int value)", asFUNCTION(SessionSetInt));
            r.Function("int GetInt(const string &in key, int fallback = 0)", asFUNCTION(SessionGetInt));
            r.Function("void SetFloat(const string &in key, float value)", asFUNCTION(SessionSetFloat));
            r.Function("float GetFloat(const string &in key, float fallback = 0.0f)", asFUNCTION(SessionGetFloat));
            r.Function("void SetBool(const string &in key, bool value)", asFUNCTION(SessionSetBool));
            r.Function("bool GetBool(const string &in key, bool fallback = false)", asFUNCTION(SessionGetBool));
            r.Function("void SetString(const string &in key, const string &in value)", asFUNCTION(SessionSetString));
            r.Function("string GetString(const string &in key, const string &in fallback = \"\")", asFUNCTION(SessionGetString));
            r.Function("bool Has(const string &in key)", asFUNCTION(SessionHas));
            r.Function("void Remove(const string &in key)", asFUNCTION(SessionRemove));
            r.Namespace("");
        }

        void RegisterSaveFile(BindingRegistrar& r)
        {
            r.ReferenceType("SaveFile", asOBJ_REF);
            r.Behaviour("SaveFile", asBEHAVE_FACTORY, "SaveFile@ f(const string &in relativePath)", asFUNCTION(CreateSaveFile), asCALL_CDECL);
            r.Behaviour("SaveFile", asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptSaveFile, AddRef), asCALL_THISCALL);
            r.Behaviour("SaveFile", asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptSaveFile, Release), asCALL_THISCALL);
            r.Method("SaveFile", "const string &get_path() const property", asMETHOD(ScriptSaveFile, GetPath), asCALL_THISCALL);
            r.Method("SaveFile", "bool Has(const string &in key) const", asMETHOD(ScriptSaveFile, Has), asCALL_THISCALL);
            r.Method("SaveFile", "int GetInt(const string &in key, int fallback = 0) const", asMETHOD(ScriptSaveFile, GetInt), asCALL_THISCALL);
            r.Method("SaveFile", "float GetFloat(const string &in key, float fallback = 0.0f) const", asMETHOD(ScriptSaveFile, GetFloat), asCALL_THISCALL);
            r.Method("SaveFile", "bool GetBool(const string &in key, bool fallback = false) const", asMETHOD(ScriptSaveFile, GetBool), asCALL_THISCALL);
            r.Method("SaveFile", "string GetString(const string &in key, const string &in fallback = \"\") const", asMETHOD(ScriptSaveFile, GetString), asCALL_THISCALL);
            r.Method("SaveFile", "void SetInt(const string &in key, int value)", asMETHOD(ScriptSaveFile, SetInt), asCALL_THISCALL);
            r.Method("SaveFile", "void SetFloat(const string &in key, float value)", asMETHOD(ScriptSaveFile, SetFloat), asCALL_THISCALL);
            r.Method("SaveFile", "void SetBool(const string &in key, bool value)", asMETHOD(ScriptSaveFile, SetBool), asCALL_THISCALL);
            r.Method("SaveFile", "void SetString(const string &in key, const string &in value)", asMETHOD(ScriptSaveFile, SetString), asCALL_THISCALL);
            r.Method("SaveFile", "void Remove(const string &in key)", asMETHOD(ScriptSaveFile, Remove), asCALL_THISCALL);
            r.Method("SaveFile", "bool Save() const", asMETHOD(ScriptSaveFile, Save), asCALL_THISCALL);
        }
    }

    bool RegisterSessionBinding(asIScriptEngine* engine)
    {
        if (!engine) {
            return false;
        }
        BindingRegistrar r(engine);
        RegisterSession(r);
        RegisterSaveFile(r);
        return r.Succeeded();
    }
}
