#pragma once

#include <cstdint>

namespace CoreEngine
{
    /// @brief 起動時にシェーダを並列にコンパイルして温めておく仕組み
    ///
    /// @details **なぜ「事前に温める」形なのか。**
    ///          シェーダのコンパイルは 20 箇所以上の PSO 生成コードが
    ///          それぞれ `ShaderCompiler` をローカルに作って個別に要求している。
    ///          その 20 箇所を並列化するとレンダラーの構築順とリソース生成が
    ///          並列になり、D3D12 のオブジェクト生成・ディスクリプタ確保・
    ///          ルートシグネチャ生成まで巻き込むことになる。
    ///          対して、DXC の呼び出しだけを先に並列で済ませて DXIL を
    ///          用意しておけば、20 箇所は 1 行も変えずにキャッシュヒットで済む。
    ///          **コールド起動の 4 秒はほぼ DXC の時間**なので、
    ///          この方式でリスクを最小にしたまま効果のほとんどが取れる。
    ///
    /// @details 何をコンパイルすべきかは ShaderManifest（前回の実行で記録された一覧）
    ///          から得る。したがって**初回起動は従来どおり直列**で、
    ///          2 回目以降のコールド起動が並列になる。
    ///
    /// @note 結果は ShaderBlobCache（メモリ）と ShaderCacheStore（ディスク）の
    ///       両方に載る。前者が無いとウォーム起動で検証コストを 2 回払って遅くなる。
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
