#include "pch.h"
#include "Script/Binding/SessionBinding.h"

#include "Script/Binding/BindingRegistrar.h"
#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"
#include "Utility/Session/SessionValues.h"

#ifdef CORE_EDITOR
#include "Graphics/Asset/AssetDatabase.h"
#endif

#include <angelscript.h>
#include <scriptarray/scriptarray.h>

#include <string>
#include <string_view>
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

        // ---------------------------------------------------------------- SaveFile と DataFile

        /// アセットの置き場（プロジェクトの根からの相対）
        constexpr const char* kAssetRoot = "Application/Assets/";

        void ThrowScriptException(const std::string& message)
        {
            if (asIScriptContext* const context = asGetActiveContext()) {
                context->SetException(message.c_str());
            }
        }

        /// @brief 要素の型を指定した空の配列を作る（スクリプトの中から呼ばれたときだけ作れる）
        CScriptArray* CreateArray(const char* declaration)
        {
            asIScriptContext* const context = asGetActiveContext();
            asIScriptEngine* const engine = context ? context->GetEngine() : nullptr;
            asITypeInfo* const type = engine ? engine->GetTypeInfoByDecl(declaration) : nullptr;
            return type ? CScriptArray::Create(type) : nullptr;
        }

        /// @brief 置き場からの相対パスとして使えるか（空・絶対パス・.. を含むものは使えない）
        bool IsRelativeFilePath(const std::string& path)
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

        /// @brief JSON ファイル 1 つ（読み込んだ中身を持ち、Save で書き出す）
        /// @details SaveFile（`Application/Saved` の下・遊んだ記録）と DataFile（`Application/Assets` の下・ゲームのデータ）が使う。
        class ScriptJsonFile
        {
        public:
            /// @param root 置き場（プロジェクトの根からの相対。末尾は /）
            /// @param relativePath 置き場からの相対パス
            /// @param isAsset DataFile か（書き出したゲームでは保存できず、エディタでは保存するとアセットに登録する）
            ScriptJsonFile(std::string root, std::string relativePath, bool isAsset)
                : root_(std::move(root)), relativePath_(std::move(relativePath)), isAsset_(isAsset)
            {
                JsonManager& jm = JsonManager::GetInstance();
                const std::string path = FullPath();
                if (jm.FileExists(path)) {
                    data_ = jm.LoadJson(path);
                    existed_ = true;
                }
                if (!data_.is_object()) {
                    data_ = json::object();
                }
            }

            ScriptJsonFile(const ScriptJsonFile&) = delete;
            ScriptJsonFile& operator=(const ScriptJsonFile&) = delete;

            void AddRef() const { ++refCount_; }

            void Release() const
            {
                if (--refCount_ == 0) {
                    delete this;
                }
            }

            const std::string& GetPath() const { return relativePath_; }

            /// @brief 作ったときにファイルがあったか
            bool Exists() const { return existed_; }

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

            /// @brief 整数の配列（無いか配列でなければ空の配列。整数でない要素は飛ばす）
            CScriptArray* GetIntArray(const std::string& key) const
            {
                CScriptArray* const result = CreateArray("array<int>");
                const auto it = data_.find(key);
                if (!result || it == data_.end() || !it->is_array()) {
                    return result;
                }
                for (const json& element : *it) {
                    if (element.is_number_integer()) {
                        int value = element.get<int>();
                        result->InsertLast(&value);
                    }
                }
                return result;
            }

            /// @brief 小数の配列（無いか配列でなければ空の配列。数でない要素は飛ばす）
            CScriptArray* GetFloatArray(const std::string& key) const
            {
                CScriptArray* const result = CreateArray("array<float>");
                const auto it = data_.find(key);
                if (!result || it == data_.end() || !it->is_array()) {
                    return result;
                }
                for (const json& element : *it) {
                    if (element.is_number()) {
                        float value = element.get<float>();
                        result->InsertLast(&value);
                    }
                }
                return result;
            }

            /// @brief 文字列の配列（無いか配列でなければ空の配列。文字列でない要素は飛ばす）
            CScriptArray* GetStringArray(const std::string& key) const
            {
                CScriptArray* const result = CreateArray("array<string>");
                const auto it = data_.find(key);
                if (!result || it == data_.end() || !it->is_array()) {
                    return result;
                }
                for (const json& element : *it) {
                    if (element.is_string()) {
                        std::string value = element.get<std::string>();
                        result->InsertLast(&value);
                    }
                }
                return result;
            }

            /// @brief 入っているキーの一覧
            CScriptArray* GetKeys() const
            {
                CScriptArray* const result = CreateArray("array<string>");
                if (!result) {
                    return result;
                }
                for (auto it = data_.begin(); it != data_.end(); ++it) {
                    std::string key = it.key();
                    result->InsertLast(&key);
                }
                return result;
            }

            void SetInt(const std::string& key, int value) { data_[key] = value; }
            void SetFloat(const std::string& key, float value) { data_[key] = value; }
            void SetBool(const std::string& key, bool value) { data_[key] = value; }
            void SetString(const std::string& key, const std::string& value) { data_[key] = value; }

            void SetIntArray(const std::string& key, const CScriptArray& values)
            {
                json elements = json::array();
                for (asUINT i = 0; i < values.GetSize(); ++i) {
                    elements.push_back(*static_cast<const int*>(values.At(i)));
                }
                data_[key] = std::move(elements);
            }

            void SetFloatArray(const std::string& key, const CScriptArray& values)
            {
                json elements = json::array();
                for (asUINT i = 0; i < values.GetSize(); ++i) {
                    elements.push_back(*static_cast<const float*>(values.At(i)));
                }
                data_[key] = std::move(elements);
            }

            void SetStringArray(const std::string& key, const CScriptArray& values)
            {
                json elements = json::array();
                for (asUINT i = 0; i < values.GetSize(); ++i) {
                    elements.push_back(*static_cast<const std::string*>(values.At(i)));
                }
                data_[key] = std::move(elements);
            }

            void Remove(const std::string& key) { data_.erase(key); }

            /// @brief 中身をファイルへ書き出す（フォルダが無ければ作る）
            bool Save() const
            {
                if (!isAsset_) {
                    return JsonManager::GetInstance().SaveJson(FullPath(), data_);
                }
#ifdef CORE_EDITOR
                if (!JsonManager::GetInstance().SaveJson(FullPath(), data_)) {
                    return false;
                }
                // 書いたファイルをアセットとして登録する（.meta を作る）
                AssetDatabase::GetInstance().ImportAsset(Logger::GetInstance().Utf8ToPath(FullPath()));
                return true;
#else
                ThrowScriptException("DataFile は、書き出したゲームでは保存できません（遊んだ記録は SaveFile に保存します）");
                return false;
#endif
            }

        private:
            ~ScriptJsonFile() = default;

            std::string FullPath() const { return root_ + relativePath_; }

            std::string root_;
            std::string relativePath_;
            bool isAsset_ = false;
            bool existed_ = false;
            json data_;
            mutable int refCount_ = 1;
        };

        ScriptJsonFile* CreateSaveFile(const std::string& relativePath)
        {
            if (!IsRelativeFilePath(relativePath)) {
                ThrowScriptException("SaveFile には Application/Saved からの相対パスを渡します（空・絶対パス・.. は使えません）");
                return nullptr;
            }
            return new ScriptJsonFile(kSaveRoot, relativePath, false);
        }

        ScriptJsonFile* CreateDataFile(const std::string& path)
        {
            // [Asset] の値（Application/Assets/ から始まる）もそのまま受け取る
            std::string relativePath = path;
            if (relativePath.starts_with(kAssetRoot)) {
                relativePath.erase(0, std::string_view(kAssetRoot).size());
            }
            if (!IsRelativeFilePath(relativePath) || !relativePath.ends_with(".json")) {
                ThrowScriptException("DataFile には Application/Assets からの相対パスで .json のファイルを渡します（空・絶対パス・.. は使えません）");
                return nullptr;
            }
            if (relativePath.starts_with("Scenes/") || relativePath.starts_with("Scenes\\")) {
                ThrowScriptException("DataFile では Scenes フォルダのファイルを扱えません（シーンの保存データです）");
                return nullptr;
            }
            return new ScriptJsonFile(kAssetRoot, relativePath, true);
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

        /// @brief SaveFile と DataFile に共通のメソッドを登録する
        void RegisterJsonFileMethods(BindingRegistrar& r, const char* type)
        {
            const auto method = [&r, type](const char* declaration, const asSFuncPtr& function) {
                r.Method(type, declaration, function, asCALL_THISCALL);
            };
            r.Behaviour(type, asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptJsonFile, AddRef), asCALL_THISCALL);
            r.Behaviour(type, asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptJsonFile, Release), asCALL_THISCALL);
            method("const string &get_path() const property", asMETHOD(ScriptJsonFile, GetPath));
            method("bool get_exists() const property", asMETHOD(ScriptJsonFile, Exists));
            method("bool Has(const string &in key) const", asMETHOD(ScriptJsonFile, Has));
            method("int GetInt(const string &in key, int fallback = 0) const", asMETHOD(ScriptJsonFile, GetInt));
            method("float GetFloat(const string &in key, float fallback = 0.0f) const", asMETHOD(ScriptJsonFile, GetFloat));
            method("bool GetBool(const string &in key, bool fallback = false) const", asMETHOD(ScriptJsonFile, GetBool));
            method("string GetString(const string &in key, const string &in fallback = \"\") const", asMETHOD(ScriptJsonFile, GetString));
            method("array<int>@ GetIntArray(const string &in key) const", asMETHOD(ScriptJsonFile, GetIntArray));
            method("array<float>@ GetFloatArray(const string &in key) const", asMETHOD(ScriptJsonFile, GetFloatArray));
            method("array<string>@ GetStringArray(const string &in key) const", asMETHOD(ScriptJsonFile, GetStringArray));
            method("array<string>@ GetKeys() const", asMETHOD(ScriptJsonFile, GetKeys));
            method("void SetInt(const string &in key, int value)", asMETHOD(ScriptJsonFile, SetInt));
            method("void SetFloat(const string &in key, float value)", asMETHOD(ScriptJsonFile, SetFloat));
            method("void SetBool(const string &in key, bool value)", asMETHOD(ScriptJsonFile, SetBool));
            method("void SetString(const string &in key, const string &in value)", asMETHOD(ScriptJsonFile, SetString));
            method("void SetIntArray(const string &in key, const array<int> &in values)", asMETHOD(ScriptJsonFile, SetIntArray));
            method("void SetFloatArray(const string &in key, const array<float> &in values)", asMETHOD(ScriptJsonFile, SetFloatArray));
            method("void SetStringArray(const string &in key, const array<string> &in values)", asMETHOD(ScriptJsonFile, SetStringArray));
            method("void Remove(const string &in key)", asMETHOD(ScriptJsonFile, Remove));
            method("bool Save() const", asMETHOD(ScriptJsonFile, Save));
        }

        void RegisterSaveFile(BindingRegistrar& r)
        {
            r.ReferenceType("SaveFile", asOBJ_REF);
            r.Behaviour("SaveFile", asBEHAVE_FACTORY, "SaveFile@ f(const string &in relativePath)", asFUNCTION(CreateSaveFile), asCALL_CDECL);
            RegisterJsonFileMethods(r, "SaveFile");

            r.ReferenceType("DataFile", asOBJ_REF);
            r.Behaviour("DataFile", asBEHAVE_FACTORY, "DataFile@ f(const string &in path)", asFUNCTION(CreateDataFile), asCALL_CDECL);
            RegisterJsonFileMethods(r, "DataFile");
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
