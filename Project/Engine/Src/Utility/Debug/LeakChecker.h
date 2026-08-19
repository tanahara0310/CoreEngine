#pragma once


namespace CoreEngine
{
/// @brief スコープ終了時に CRT のメモリリーク検出を走らせる RAII
class LeakChecker {
public:
    // スコープ終了時にリークチェックが実行される
    ~LeakChecker();

private: // メンバ変数
};
}
