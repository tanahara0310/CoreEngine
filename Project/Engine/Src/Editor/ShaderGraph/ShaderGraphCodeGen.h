#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"

#include <map>
#include <string>
#include <vector>

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief ノード 1 つの型を決めた結果
    struct ResolvedNode
    {
        int dynamicWidth = 1;
        std::vector<ValueType> inputs;  ///< 入力ピンの種類（Dynamic は幅へ直した後）
        std::vector<ValueType> outputs; ///< 出力ピンの種類
    };

    /// @brief ノードに出す問題
    struct NodeIssue
    {
        int node = 0;
        std::string message;
    };

    /// @brief グラフ全体の型を決めた結果
    struct Resolution
    {
        std::map<int, ResolvedNode> nodes;
        std::vector<NodeIssue> issues;
    };

    /// @brief 全ノードのピンの種類を決める（届かないノードも含む）
    Resolution Resolve(const Graph& graph);

    /// @brief HLSL を作るときの指定
    struct GenerateOptions
    {
        std::string sourcePath;  ///< 先頭の行に書くグラフの場所
        int previewNode = 0;     ///< 0 以外なら、このノードの出力を色として出す
        std::string previewPin;  ///< 映す出力ピン（空なら最初の数の出力）
    };

    /// @brief HLSL を作った結果
    struct GenerateResult
    {
        std::string hlsl;
        std::vector<NodeIssue> issues; ///< ノードごとの問題（あっても HLSL は作る）
        std::string fatal;             ///< HLSL を作れなかった訳（空なら作れた）
        std::map<int, std::pair<int, int>> nodeLines; ///< ノード → HLSL の行（1 始まり・最初と最後）
    };

    /// @brief グラフから HLSL を作る
    GenerateResult Generate(const Graph& graph, const GenerateOptions& options);

    /// @brief 生成したファイルの先頭に書く印（生成物かの判定に使う）
    inline constexpr const char* kGeneratedMarker = "// Generated from ";

    /// @brief 生成したファイルの先頭のプロパティの行（`// property 名前 種類 …`）
    inline constexpr const char* kPropertyLinePrefix = "// property ";
}

#endif // CORE_EDITOR
