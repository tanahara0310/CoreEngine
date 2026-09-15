#pragma once

#include "EngineSystem/Subsystem/IEngineSubsystem.h"

#include <memory>

namespace CoreEngine
{
    class ScriptHost;

    /// @brief スクリプトの実行環境を起動時に作り、コンポーネントの型をファクトリへ登録する
    /// @details `Application/Assets/Scripts` の `.as` をコンパイルし、ScriptComponent を継いだクラスを
    ///          クラス名でコンポーネントとして作れるようにする。フレーム末に GC を 1 段進める。
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
        std::unique_ptr<ScriptHost> host_;
    };
}
