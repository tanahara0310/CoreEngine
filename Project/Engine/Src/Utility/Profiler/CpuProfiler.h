#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief CPU 側の区間計測（壁時計 + そのスレッドの実 CPU 時間）
    /// @details 用途は回帰検知。原因究明には VS プロファイラや WPR を使う。
    /// @warning CPU 時間の分解能は約 15.6ms。数十 ms 未満のスコープで待ち率を読まないこと。
    class CpuProfiler {
    public:
        /// @brief 1 スコープの計測結果
        struct Sample {
            std::string name;
            uint64_t    sequence = 0;   ///< 開始順（レポートの並べ替え用）
            uint32_t    depth = 0;      ///< ネストの深さ
            uint32_t    threadId = 0;
            double      wallMs = 0.0;
            double      cpuMs = 0.0;    ///< そのスレッドのカーネル+ユーザー時間
        };

        /// @brief プロセス共有のインスタンスを取得
        static CpuProfiler& GetInstance();

        /// @brief サンプルの**収集**を有効/無効にする
        /// @note 無効にしても BeginScope / EndScope の計測自体は行われ、
        ///       EndScope は正しい計測値を返す（レポートに載らなくなるだけ）。
        ///       こうしないと、計測値を戻り値として使う呼び出し元
        ///       （StartupSequence::Step）が無効化と同時に壊れる。
        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool IsEnabled() const { return enabled_; }

        /// @brief スコープ開始（通常は CORE_CPU_SCOPE を使う）
        void BeginScope(const char* name);

        /// @brief スコープ終了。**この区間の計測結果を返す**
        /// @return 閉じたスコープの計測値（対応する BeginScope が無ければ全て 0）
        /// @details 戻り値を返すのは、計測を必要とする呼び出し元（StartupSequence）が
        ///          自前のストップウォッチを別途持つ「計測経路の二重化」を防ぐため。
        ///          同じ区間を 2 経路で測ると、片方だけ修正されたときに数字が食い違う。
        Sample EndScope();

        /// @brief 収集済みサンプルを捨てる
        void Reset();

        /// @brief 収集済みサンプルを開始順で取得する
        std::vector<Sample> GetSamples() const;

        /// @brief ツリー整形してログへ出す（壁時計 / CPU / 待ち率）
        /// @param title 見出し
        /// @details 出力は TSV 風。DXC 純時間が実行ごとに 2.2 倍ばらつくため、
        ///          統計は複数回の起動をプロセス外で集計して取ること。
        void LogReport(const char* title) const;

    private:
        CpuProfiler() = default;

        bool enabled_ = true;
        mutable std::mutex mutex_;
        std::vector<Sample> samples_;
        uint64_t nextSequence_ = 0;
    };

    /// @brief 現在のスレッドが消費した CPU 時間（ミリ秒）
    /// @note 分解能は約 15.6ms。短い区間の判定には使えない（CpuProfiler の warning 参照）
    double GetCurrentThreadCpuMilliseconds();

    /// @brief RAII の区間計測。CORE_CPU_SCOPE 経由で使う
    class CpuScopeTimer {
    public:
        explicit CpuScopeTimer(const char* name) { CpuProfiler::GetInstance().BeginScope(name); }
        ~CpuScopeTimer() { CpuProfiler::GetInstance().EndScope(); }

        CpuScopeTimer(const CpuScopeTimer&) = delete;
        CpuScopeTimer& operator=(const CpuScopeTimer&) = delete;
    };
}

#define CORE_CPU_SCOPE_CONCAT_INNER(a, b) a##b
#define CORE_CPU_SCOPE_CONCAT(a, b) CORE_CPU_SCOPE_CONCAT_INNER(a, b)

/// @brief この行から現在のスコープ終端までを計測する
/// @details 例: `CORE_CPU_SCOPE("ModelResource::LoadFromFile");`
///          ネストするとレポートでインデントされる。
#define CORE_CPU_SCOPE(name) \
    CoreEngine::CpuScopeTimer CORE_CPU_SCOPE_CONCAT(coreCpuScope_, __LINE__)(name)
