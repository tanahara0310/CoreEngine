#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class asIScriptContext;
class asIScriptEngine;
class asIScriptFunction;
class asIScriptModule;
class asIScriptObject;

namespace CoreEngine
{
    class AudioSystem;
    class EngineSystem;
    class InputManager;
    class ScriptComponent;
    class ScriptComponentType;

    /// @brief スクリプトの束縛が使うエンジンのサービス
    struct ScriptServices
    {
        InputManager* input = nullptr;
        AudioSystem* audio = nullptr;
        EngineSystem* engine = nullptr;
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

        /// @brief スクリプトを読み直した結果
        struct ReloadReport
        {
            bool compiled = false;    ///< 新しいモジュールを作れたか（false なら前のモジュールのまま動き続ける）
            std::size_t restored = 0; ///< 値を戻せたコンポーネントの数
            std::size_t orphaned = 0; ///< クラスが無くなり、値を持ったまま止めたコンポーネントの数
            double elapsedMs = 0.0;
        };

        /// @brief スクリプトをコンパイルし直し、生きているコンポーネントの値を持ち越して差し替える
        /// @param root スクリプトのフォルダ
        /// @return 結果（コンパイルに失敗したら何も変えない）
        /// @note スクリプトを実行していないところ（フレームの最後）から呼ぶ。
        ReloadReport Reload(const std::filesystem::path& root);

        /// @brief モジュールの世代（コンパイルし直すたびに 1 つ増える）
        std::uint32_t GetModuleGeneration() const { return moduleGeneration_; }

        /// @brief 名前からコンポーネントの型を引く
        /// @return 無ければ nullptr
        const ScriptComponentType* FindType(std::string_view name) const;

        /// @brief エンジンを終える
        /// @note 生きているコンポーネントには先にスクリプトのオブジェクトを手放させる。
        void Shutdown();

        /// @brief GC を 1 段だけ進める
        void CollectGarbageStep();

        /// @brief 直前のフレームの実行の集計
        struct FrameStats
        {
            double updateMs = 0.0;          ///< Update / LateUpdate にかかった時間の合計（ミリ秒）
            std::size_t liveComponents = 0; ///< 生きているスクリプトのコンポーネントの数
            std::size_t pooledContexts = 0; ///< 使い回しを待っているコンテキストの数
            std::size_t gcObjects = 0;      ///< GC が抱えているオブジェクトの数
        };

        /// @brief フレームの集計を締める（型ごとの時間を「直前のフレーム」へ移す）
        /// @note フレームの最後に 1 回呼ぶ。
        void EndFrameStats();

        /// @brief 直前のフレームの実行の集計
        const FrameStats& GetFrameStats() const { return frameStats_; }

        /// @brief 登録済みの型と関数を `as.predefined` の形でファイルへ書く（中身が同じなら書かない）
        /// @return 書き終えたか、既に同じ中身だったら true。エンジンを作る前は false
        bool WritePredefined(const std::filesystem::path& file) const;

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

        /// @brief オブジェクトのメソッドを引数付きで呼ぶ
        /// @param setArguments コンテキストへ引数を積む（負の値を返したら実行しない）
        /// @param describeCaller 止まったときのログに出す呼び出し元の名前を作る（止まったときだけ呼ぶ）
        /// @return 最後まで実行できたら true。例外・中断のときは場所と呼び出し履歴をログへ出して false
        bool CallMethod(asIScriptFunction* function, asIScriptObject* object,
                        const std::function<int(asIScriptContext*)>& setArguments,
                        const std::function<std::string()>& describeCaller);

        /// @brief スクリプトの関数を呼ぶ（デリゲートでもよい）
        /// @param setArguments コンテキストへ引数を積む（負の値を返したら実行しない。nullptr なら引数なし）
        /// @param describeCaller 止まったときのログに出す呼び出し元の名前を作る（止まったときだけ呼ぶ）
        /// @return 最後まで実行できたら true。例外・中断のときは場所と呼び出し履歴をログへ出して false
        bool CallFunction(asIScriptFunction* function, const std::function<int(asIScriptContext*)>& setArguments,
                          const std::function<std::string()>& describeCaller);

        /// @brief エンジンを作った実行環境（無ければ nullptr）
        static ScriptHost* FromEngine(asIScriptEngine* engine);

        /// @brief 実行環境を終えると期限切れになる印（スクリプトの関数を持ち続ける側が、手放す前に確かめる）
        std::weak_ptr<void> GetLifetimeToken() const { return lifetimeToken_; }

        /// @brief string 型の型 ID（エンジンを作る前は 0）
        int GetStringTypeId() const { return stringTypeId_; }

        /// @brief Vector2 / Vector3 / Vector4 型の型 ID（エンジンを作る前は 0）
        int GetVector2TypeId() const { return vector2TypeId_; }
        int GetVector3TypeId() const { return vector3TypeId_; }
        int GetVector4TypeId() const { return vector4TypeId_; }

        /// @brief GameObject のハンドル（`GameObject@`）の型 ID（エンジンを作る前は 0）
        int GetGameObjectHandleTypeId() const { return gameObjectHandleTypeId_; }

        /// @brief 配列（`array<T>`。ハンドルは除く）の型 ID から要素の型 ID を引く
        /// @return 配列でなければ -1
        int GetArrayElementTypeId(int typeId) const;

        /// @brief 型 ID の宣言の綴り（ログ用。引けなければ nullptr）
        const char* GetTypeDeclaration(int typeId) const;

        /// @brief 生きているスクリプトのコンポーネントとして控える
        void RegisterComponent(ScriptComponent* component);

        /// @brief 生きているスクリプトのコンポーネントの控えから外す
        void UnregisterComponent(ScriptComponent* component);

    private:
        static asIScriptContext* RequestContext(asIScriptEngine* engine, void* userData);
        static void ReturnContext(asIScriptEngine* engine, asIScriptContext* context, void* userData);

        /// @brief 用意したコンテキストを実行し、止まったらログへ出して、コンテキストを返す
        bool RunPrepared(asIScriptContext* context, int result, const std::function<std::string()>& describeCaller);

        /// @brief 型を捨ててからモジュールを捨てる
        void DiscardModule();

        /// @brief コンパイルしてコンポーネントの型を集めた 1 モジュール分
        struct CompiledModule
        {
            asIScriptModule* module = nullptr;
            std::vector<std::unique_ptr<ScriptComponentType>> types;
            /// 直る前の版のまま組んだファイル（エラーが残っているもの）
            std::vector<std::string> staleSections;
        };

        /// @brief フォルダの `.as` をコンパイルする
        /// @details 1 回目で失敗したら、★エラーの出たファイルだけ最後に通った版へ戻して
        ///          もう一度組む★。全部が前の状態へ戻るのを避けるため。
        /// @return 失敗したら false（作りかけのモジュールは捨てる。今のモジュールは触らない）
        bool CompileModule(const std::filesystem::path& root, CompiledModule& out);

        /// @brief 1 回分のコンパイル
        /// @param fallbackSections この中のファイルは、今の中身ではなく最後に通った版で組む
        /// @param outSources 組むのに使った中身（成功したときに控えるため）
        /// @return 組めたら true
        /// @note エラーの出たファイルを集めるのは呼び出し側（メッセージの受け先を差し替える）。
        bool TryBuild(const std::filesystem::path& root, CompiledModule& out,
                      const std::set<std::string>& fallbackSections,
                      std::unordered_map<std::string, std::string>& outSources);

        asIScriptEngine* engine_ = nullptr;
        asIScriptModule* module_ = nullptr;
        std::uint32_t moduleGeneration_ = 0;

        // 最後にコンパイルが通ったときの、ファイルごとの中身。
        // 1 つが壊れても他を新しくできるよう、差し戻す先として持っておく
        std::unordered_map<std::string, std::string> lastGoodSources_;
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

        /// `GetLifetimeToken()` の実体（エンジンを捨てる前に切る）
        std::shared_ptr<void> lifetimeToken_;

        /// 直前のフレームの実行の集計
        FrameStats frameStats_;
    };
}
