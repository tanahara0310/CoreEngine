#pragma once
#include <string>
#include <vector>

struct _EXCEPTION_POINTERS;

namespace CoreEngine
{
    /// @brief 落ちたときの理由と呼び出しの列を、人が読める文にまとめる
    class CrashReport {
    public:
        /// @brief 落ちた理由（例外の種類。C++ の例外なら what() の文も付ける）
        static std::string DescribeException(const _EXCEPTION_POINTERS& exception);

        /// @brief 落ちた場所から呼び出し元へさかのぼる列（1 行に 1 段。上が落ちた場所）
        /// @details 落ちたときのレジスタからさかのぼる。落ちたスレッドとは別のスレッドから呼んでもよい
        static std::vector<std::string> DescribeCallStack(const _EXCEPTION_POINTERS& exception);
    };
}
