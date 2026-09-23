#pragma once

namespace CoreEngine
{
    class EngineSystem;
    struct EngineConfig;
    struct FrameContext;
    struct RenderContext;

    /// @brief エンジンサブシステムの共通インターフェース
    /// @details EngineSystem に登録されるサブシステムが実装する基底インターフェース。
    ///          Initialize / Finalize / BeginFrame / EndFrame のライフサイクルと、
    ///          描画の前後（BeginRender / EndRender / AfterPresent）を統一し、
    ///          EngineSystem からまとめて呼び出せるようにする。
    ///          使わない処理は既定の空の実装のままにする。
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

        /// @brief ビューを確定した後、描画を記録する前に呼ばれる
        /// @param context このフレームの描画の文脈（計測器などを入れられる）
        /// @param frame 記録先のコマンドリストとフレームの添字
        virtual void BeginRender(RenderContext& /*context*/, const FrameContext& /*frame*/) {}

        /// @brief 全ビューの描画を記録した後、コマンドリストを閉じる前に呼ばれる（登録の逆順）
        /// @param frame 記録先のコマンドリストとフレームの添字
        virtual void EndRender(const FrameContext& /*frame*/) {}

        /// @brief フレームを提示した後に呼ばれる
        virtual void AfterPresent() {}
    };
}
