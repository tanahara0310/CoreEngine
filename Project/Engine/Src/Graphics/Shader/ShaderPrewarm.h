#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief 起動時にシェーダを並列にコンパイルして温めておく仕組み
    /// @details DXC の呼び出しだけを先に並列で済ませて DXIL を用意しておけば、
    ///          20 箇所以上ある PSO 生成コードは 1 行も変えずにキャッシュヒットで済む。
    /// @note 対象は ShaderManifest（前回の実行で記録された一覧）から得るため、初回起動は直列。
    ///       結果は ShaderBlobCache（メモリ）と ShaderCacheStore（ディスク）の両方に載る。
    namespace ShaderPrewarm
    {
        /// @brief 事前コンパイルの結果（ログと回帰検知のため）
        struct Result
        {
            bool     executed = false;    ///< 実行したか（一覧が空なら false）
            uint32_t total = 0;           ///< 一覧の件数
            uint32_t succeeded = 0;       ///< バイナリが得られた件数
            uint32_t failed = 0;          ///< 失敗した件数（本来の経路で再試行される）
            uint32_t workerCount = 0;     ///< 使ったワーカー数
            double   wallMs = 0.0;        ///< 壁時計
            double   workerBusyMs = 0.0;  ///< 全ワーカーの実行時間合計
            double   speedup = 0.0;       ///< workerBusyMs / wallMs（直列比）
        };

        /// @brief 一覧を読み込み、並列にコンパイルする（**メインスレッドから呼ぶ**）
        /// @details パス解決だけはメインスレッドで済ませ、ワーカーへは
        ///          解決済みの要求を渡す（AssetDatabase がスレッドセーフでないため）。
        Result Run();
    }
}
