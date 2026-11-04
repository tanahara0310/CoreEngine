#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraphCodeGen.h"

#include <string>
#include <vector>

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief 全ノードの検査の 1 件（グラフ 1 つから作った HLSL）
    struct SelfTestCase
    {
        std::string name;
        GenerateResult generated;
    };

    /// @brief 全ノードの検査に使うグラフを作って HLSL にする
    /// @details 置けるノードごとに、Dynamic の入力の幅（つながない・1〜4）と出力ごとに、
    ///          その出力を映すグラフと出口へつないだグラフを作る。プロパティの種類ごとのグラフと空のグラフも足す。
    std::vector<SelfTestCase> BuildSelfTestCases();
}

#endif // CORE_EDITOR
