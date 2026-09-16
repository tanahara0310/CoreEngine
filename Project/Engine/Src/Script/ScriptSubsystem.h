#pragma once

#include "EngineSystem/Subsystem/IEngineSubsystem.h"

#ifdef USE_IMGUI
#include "Script/ScriptFileWatcher.h"
#endif

#include <cstddef>
#include <filesystem>
#include <memory>

namespace CoreEngine
{
    class ScriptHost;

    /// @brief スクリプトの実行環境を起動時に作り、コンポーネントの型をファクトリへ登録する
    /// @details `Application/Assets/Scripts` の `.as` をコンパイルし、ScriptComponent を継いだクラスを
    ///          クラス名でコンポーネントとして作れるようにする。フレーム末に GC を 1 段進める。
    ///          エディタのあるビルドは、`.as` の変更を見張ってフレーム末に読み直す。
    class ScriptSubsystem final : public IEngineSubsystem
    {
    public:
        ScriptSubsystem();
        ~ScriptSubsystem() override;

        const char* GetName() const noexcept override { return "ScriptSubsystem"; }

        void Initialize(EngineSystem* engine, const EngineConfig& config) override;
        void Finalize() override;
        void EndFrame() override;

        /// @brief 実行環境（エンジンを作れなかったら nullptr）
        ScriptHost* GetHost() const { return host_.get(); }

        /// @brief スクリプトの読み込み状態
        struct Status
        {
            bool ok = false;            ///< 直前のコンパイルに成功したか
            std::size_t typeCount = 0;  ///< 使えるコンポーネントの型の数
            std::size_t restored = 0;   ///< 直前の読み直しで値を戻せた数
            std::size_t orphaned = 0;   ///< 直前の読み直しでクラスが無くなった数
            double elapsedMs = 0.0;     ///< 直前の読み直しにかかった時間
        };

        /// @brief スクリプトの読み込み状態
        const Status& GetStatus() const { return status_; }

        /// @brief スクリプトのフォルダ（コンパイラのメッセージのファイル名はここからの相対パス）
        const std::filesystem::path& GetScriptRoot() const { return scriptRoot_; }

#ifdef USE_IMGUI
        /// @brief ファイルが変わっていなくても、このフレームの終わりにスクリプトを読み直す
        void RequestReload() { reloadRequested_ = true; }
#endif

    private:
        /// @brief コンポーネントの型をファクトリへ登録する（前の登録を外してから呼ぶ）
        void RegisterComponentTypes();

#ifdef USE_IMGUI
        /// @brief スクリプトを読み直し、ファクトリの登録を入れ替える
        void ReloadScripts();
#endif

        std::unique_ptr<ScriptHost> host_;
        std::filesystem::path scriptRoot_;
        Status status_;
#ifdef USE_IMGUI
        bool reloadRequested_ = false;
        ScriptFileWatcher watcher_;
#endif
    };
}
