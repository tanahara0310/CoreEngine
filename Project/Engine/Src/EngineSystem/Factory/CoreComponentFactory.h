#pragma once

namespace CoreEngine
{
    class EngineSystem;
    struct EngineConfig;

    /// @brief コアコンポーネント（Input / Audio / Light / FrameRate）の生成・初期化を担当するファクトリー
    /// @details EngineSystem::CreateInput/Audio/Light/FrameRateController の実装を分離し、
    ///          EngineSystem.h から入力・オーディオ・ライト系のインクルードを排除する。
    ///          EngineSystem の friend クラスとして RegisterComponent へアクセスする。
    class CoreComponentFactory
    {
    public:
        /// @brief フレームレートコントローラーを生成・登録する
        static void SetupFrameRate(EngineSystem& engine);

        /// @brief 入力コンポーネントを生成・登録する
        static void SetupInput(EngineSystem& engine);

        /// @brief オーディオコンポーネントを生成・登録する
        static void SetupAudio(EngineSystem& engine);

        /// @brief ライトコンポーネントを生成・登録する
        static void SetupLight(EngineSystem& engine);
    };
}
