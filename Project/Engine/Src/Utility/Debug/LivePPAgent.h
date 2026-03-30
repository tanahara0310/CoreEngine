#pragma once

#ifdef _DEBUG
#include "LivePP/API/x64/LPP_API_x64_CPP.h"

namespace CoreEngine
{
    /// @brief Live++ ホットリロードエージェント管理クラス（デバッグビルド専用）
    /// @details Broker プロセスの管理と Hot-Reload / Hot-Restart ポーリングを担当する
    class LivePPAgent
    {
    public:
        LivePPAgent() = default;
        ~LivePPAgent();

        LivePPAgent(const LivePPAgent&) = delete;
        LivePPAgent& operator=(const LivePPAgent&) = delete;

        /// @brief 残留 Broker を終了 → Agent 初期化 → モジュール有効化
        void Initialize();

        /// @brief フレーム先頭で呼び出す（ホットリロード / ホットリスタートの処理）
        void Update();

        /// @brief エージェントが有効かどうかを返す
        bool IsValid() const;

    private:
        /// @brief 残留 Broker プロセスを強制終了する（ゾンビプロセス対策）
        void KillBrokerProcess();

        lpp::LppSynchronizedAgent agent_ = {};
    };
}

#endif // _DEBUG
