#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"

#include <set>
#include <string>

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief サブグラフの拡張子
    inline constexpr const char* kSubGraphExtension = ".shadersubgraph";

    /// @brief サブグラフの形の写し（使う側のノードの設定に書くもの）
    struct SubGraphSnapshot
    {
        std::string inputs;            ///< 入力の並び（`種類 鍵 表示名;…`）
        std::string outputs;           ///< 出力の並び
        uint8_t targets = kAllTargets; ///< 使える目標（出口からたどったノードの重なり。段は中のノードが書くときに確かめる）
    };

    /// @brief サブグラフの今の形の写しを作る（使える目標は、入れ子のサブグラフの中もたどる）
    SubGraphSnapshot MakeSubGraphSnapshot(const Graph& subGraph, const SubGraphResolver& resolve);

    /// @brief サブグラフのノードを作る（指す先・写し・数の入力の既定値を入れる）
    Node MakeSubGraphNode(int id, const std::string& guid, const std::string& path, const Graph& subGraph,
        const SubGraphSnapshot& snapshot);

    /// @brief 使う側のサブグラフのノードを写しに合わせる（消えたピンへの線を外し、新しい数の入力に既定値を入れる）
    /// @return 何か変えたら true
    bool ApplySubGraphSnapshot(Graph& graph, int node, const Graph& subGraph, const SubGraphSnapshot& snapshot);

    /// @brief グラフの中のサブグラフのノードを全部、今のサブグラフの形へ合わせる（読めないものはそのまま）
    /// @return 何か変えたら true
    bool SyncSubGraphNodes(Graph& graph, const SubGraphResolver& resolve);

    /// @brief サブグラフを、プレビューの目標の普通のグラフにする（サブグラフの窓のプレビューと検査に使う）
    /// @details ノードの番号はそのまま。出口の最初の数の出力を Base Color へつなぐ Fragment の出口（3D は Vertex の出口も）を足し、
    ///          サンプラーの入力は線形・繰り返しの Sampler State ノードにする
    Graph MakeSubGraphPreviewGraph(const Graph& subGraph);

    /// @brief グラフがこのサブグラフ（GUID かパスで指す）を使っているか（入れ子のサブグラフの中もたどる）
    bool UsesSubGraph(const Graph& graph, const std::string& guid, const std::string& path, const SubGraphResolver& resolve);

    /// @brief 選んだノードをサブグラフへ切り出し、グラフの選んだノードをサブグラフのノード 1 つに置き換える
    /// @details 選んだ中の出口と Property は外に残す（Property は入力になる）。付箋は中へ移す。
    ///          入力は外から入る線の元ごと、出力は外へ出る線の元ごとに 1 つ
    /// @param guid, path 作るサブグラフの指す先（ノードの設定に書く）
    /// @param subGraph 作ったサブグラフ
    /// @return 置いたノードの番号（切り出せなければ 0 で、error に訳）
    int ExtractSubGraph(Graph& graph, const std::set<int>& selection, const std::string& guid, const std::string& path,
        const SubGraphResolver& resolve, Graph& subGraph, std::string& error);
}

#endif // CORE_EDITOR
