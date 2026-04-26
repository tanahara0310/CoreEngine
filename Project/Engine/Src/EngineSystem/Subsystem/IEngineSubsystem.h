#pragma once

namespace CoreEngine
{
    class EngineSystem;
    struct EngineConfig;

    /// @brief エンジンサブシステムの共通インターフェース
    /// @details EngineSystem に登録されるサブシステムが実装する基底インターフェース。
    ///          Initialize / Finalize / BeginFrame / EndFrame のライフサイクルを統一し、
    ///          EngineSystem からまとめて呼び出せるようにする。
    ///          フレームライフサイクルが不要なサブシステムは BeginFrame / EndFrame を no-op で実装する。
    class IEngineSubsystem
    {
    public:
        virtual ~IEngineSubsystem() = default;

        /// @brief サブシステム名（ログ・デバッグ用）
        virtual const char* GetName() const noexcept = 0;

        /// @brief 初期化（EngineSystem のグラフィックス系コンポーネント生成後に呼ばれる）
        virtual void Initialize(EngineSystem* /*engine*/, const EngineConfig& /*config*/) {}

        /// @brief 終了処理（EngineSystem::Finalize の最初に呼ばれる）
        virtual void Finalize() {}

        /// @brief フレーム開始処理
        virtual void BeginFrame() {}

        /// @brief フレーム終了処理
        virtual void EndFrame() {}
    };
}
