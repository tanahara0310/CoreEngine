#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class asIScriptContext;
class asIScriptEngine;
class asIScriptFunction;
class asIScriptModule;
class asIScriptObject;

namespace CoreEngine
{
    class InputManager;
    class ScriptComponent;
    class ScriptComponentType;

    /// @brief スクリプトの束縛が使うエンジンのサービス
    struct ScriptServices
    {
        InputManager* input = nullptr;
    };

    /// @brief AngelScript の実行環境（エンジン・モジュール・コンポーネントの型）
    /// @details スクリプトのフォルダの `.as` をすべて 1 つのモジュール `Game` にコンパイルする。
    ///          メインスレッドからだけ使う。
    class ScriptHost
    {
    public:
        ScriptHost();
        ~ScriptHost();

        ScriptHost(const ScriptHost&) = delete;
        ScriptHost& operator=(const ScriptHost&) = delete;

        /// @brief エンジンを作り、スクリプトから使える型と関数を登録する
        /// @param services 束縛が使うエンジンのサービス
        /// @return 失敗したら false（ログに理由を出す）
        bool Initialize(const ScriptServices& services);

        /// @brief スクリプトのフォルダの `.as` をコンパイルし、ScriptComponent を継いだクラスを型として集める
        /// @param root スクリプトのフォルダ（下のフォルダも読む。エディタの無いビルドは直下の `Editor` を読まない）
        /// @return コンパイルできたら true。失敗したらモジュールも型も持たない
        /// @note スクリプトのコンポーネントが残っている間は何もせずに false を返す。
        bool Build(const std::filesystem::path& root);

        /// @brief エンジンを終える
        /// @note 生きているコンポーネントには先にスクリプトのオブジェクトを手放させる。
        void Shutdown();

        /// @brief GC を 1 段だけ進める
        void CollectGarbageStep();

        /// @brief 集めたコンポーネントの型
        const std::vector<std::unique_ptr<ScriptComponentType>>& GetTypes() const { return types_; }

        /// @brief 型のクラスのオブジェクトを作る
        /// @return 参照を 1 つ持ったオブジェクト。作れなければ nullptr
        asIScriptObject* CreateObject(const ScriptComponentType& type);

        /// @brief オブジェクトのメソッドを呼ぶ
        /// @param describeCaller 止まったときのログに出す呼び出し元の名前を作る（止まったときだけ呼ぶ）
        /// @return 最後まで実行できたら true。例外・中断のときは場所と呼び出し履歴をログへ出して false
        /// @note エディタのあるビルドは 1 回の呼び出しで実行できる行数に上限を持ち、超えたら中断する。
        bool CallMethod(asIScriptFunction* function, asIScriptObject* object,
                        const std::function<std::string()>& describeCaller);

        /// @brief string 型の型 ID（エンジンを作る前は 0）
        int GetStringTypeId() const { return stringTypeId_; }

        /// @brief Vector2 / Vector3 / Vector4 型の型 ID（エンジンを作る前は 0）
        int GetVector2TypeId() const { return vector2TypeId_; }
        int GetVector3TypeId() const { return vector3TypeId_; }
        int GetVector4TypeId() const { return vector4TypeId_; }

        /// @brief GameObject のハンドル（`GameObject@`）の型 ID（エンジンを作る前は 0）
        int GetGameObjectHandleTypeId() const { return gameObjectHandleTypeId_; }

        /// @brief 型 ID の宣言の綴り（ログ用。引けなければ nullptr）
        const char* GetTypeDeclaration(int typeId) const;

        /// @brief 生きているスクリプトのコンポーネントとして控える
        void RegisterComponent(ScriptComponent* component);

        /// @brief 生きているスクリプトのコンポーネントの控えから外す
        void UnregisterComponent(ScriptComponent* component);

    private:
        static asIScriptContext* RequestContext(asIScriptEngine* engine, void* userData);
        static void ReturnContext(asIScriptEngine* engine, asIScriptContext* context, void* userData);

        /// @brief 型を捨ててからモジュールを捨てる
        void DiscardModule();

        asIScriptEngine* engine_ = nullptr;
        asIScriptModule* module_ = nullptr;
        int stringTypeId_ = 0;
        int vector2TypeId_ = 0;
        int vector3TypeId_ = 0;
        int vector4TypeId_ = 0;
        int gameObjectHandleTypeId_ = 0;
        std::vector<std::unique_ptr<ScriptComponentType>> types_;

        /// 使い回すコンテキスト
        std::vector<asIScriptContext*> contextPool_;

        /// 生きているスクリプトのコンポーネント
        std::unordered_set<ScriptComponent*> components_;
    };
}
