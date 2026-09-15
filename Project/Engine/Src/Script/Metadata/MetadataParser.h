#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine::Script
{
    /// @brief スクリプトの属性 1 つ
    /// @details `Range(0, 10)` なら名前が `Range`、引数が `0` と `10`。
    struct MetadataAttribute
    {
        std::string name;

        /// @brief 引数。引用符で囲んだ引数は、引用符を外してエスケープを戻した値
        std::vector<std::string> arguments;
    };

    /// @brief scriptbuilder が切り出した `[` `]` の中身を属性の並びに分ける
    /// @param text `Range(0, 10)` や `ReadOnly, DisplayName("速さ")` のような中身
    /// @param out 読んだ属性（先頭から順に）
    /// @param error 読めなかったときの理由
    /// @return 最後まで読めたら true
    bool ParseMetadata(std::string_view text, std::vector<MetadataAttribute>& out, std::string& error);

    /// @brief 数値の引数を float として読む
    /// @return 数値として読めなければ空（末尾の `f` は読み飛ばす）
    std::optional<float> ParseFloatArgument(const std::string& argument);
}
