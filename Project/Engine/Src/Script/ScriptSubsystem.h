#pragma once

#include "EngineSystem/Subsystem/IEngineSubsystem.h"

#ifdef USE_IMGUI
#include "Script/ScriptFileWatcher.h"
#endif

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

    private:
        /// @brief コンポーネントの型をファクトリへ登録する（前の登録を外してから呼ぶ）
        void RegisterComponentTypes();

#ifdef USE_IMGUI
        /// @brief スクリプトを読み直し、ファクトリの登録を入れ替える
        void ReloadScripts();
#endif

        std::unique_ptr<ScriptHost> host_;
        std::filesystem::path scriptRoot_;
#ifdef USE_IMGUI
        ScriptFileWatcher watcher_;
#endif
    };
}
