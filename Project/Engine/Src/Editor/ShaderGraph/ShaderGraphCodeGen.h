#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
#include "Editor/ShaderGraph/ShaderGraphNodeLibrary.h"

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
        IncludeResolver resolveInclude; ///< Custom Function のファイルの include の名前を決める（空ならファイル名）
    };

    /// @brief HLSL を作った結果
    struct GenerateResult
    {
        std::string hlsl;
        std::vector<NodeIssue> issues; ///< ノードごとの問題（あっても HLSL は作る）
        std::string fatal;             ///< HLSL を作れなかった訳（空なら作れた）
        std::multimap<int, std::pair<int, int>> nodeLines; ///< ノード → HLSL の行（1 始まり・最初と最後。3D は Vertex と Fragment の 2 つあることがある）
        std::map<int, std::pair<int, int>> globalLines; ///< ノード → そのノードが関数の外に置いた文の行
    };

    /// @brief グラフから HLSL を作る
    GenerateResult Generate(const Graph& graph, const GenerateOptions& options);

    /// @brief ノードのプレビューを並べる絵（1 枚の一辺・横の数・最大の数）
    inline constexpr int kNodePreviewTile = 96;
    inline constexpr int kNodePreviewColumns = 10;
    inline constexpr int kNodePreviewMax = 100;
    inline constexpr int kNodePreviewAtlas = kNodePreviewTile * kNodePreviewColumns;

    /// @brief ノードのプレビュー 1 枚
    struct NodePreviewTile
    {
        int node = 0;
        int tile = 0;        ///< 並べる位置（左上から横へ）
        bool sphere = false; ///< 3D の値を使うので球に描く（3D の目標だけ）
    };

    /// @brief ノードのプレビューの HLSL を作った結果
    struct NodePreviewResult
    {
        std::string hlsl;                       ///< 映すノードが無ければ空
        std::vector<NodePreviewTile> tiles;
        std::map<int, std::string> unavailable; ///< 映せないノードとその訳
        std::vector<int> overflow;              ///< 数の上限を超えて映さないノード
    };

    /// @brief プレビューを開いたノードを全部 1 本のシェーダに書く（画素の位置からタイルを選んで、そのノードの色を出す）
    /// @note スプライトは main（タイルごとに描く）、ポストエフェクトは main（絵全体を 1 回で書く）、3D は Surface（Unlit）
    NodePreviewResult GenerateNodePreviews(const Graph& graph, const GenerateOptions& options);

    /// @brief 生成したファイルの先頭に書く印（生成物かの判定に使う）
    inline constexpr const char* kGeneratedMarker = "// Generated from ";

    /// @brief 生成したファイルの先頭のプロパティの行（`// property 名前 種類 …`）
    inline constexpr const char* kPropertyLinePrefix = "// property ";

    /// @brief 3D の生成物のプロパティの定数バッファ（b）と最初のテクスチャ（t）の番号（`kSurfaceMaterialRegisters` と同じ値）
    inline constexpr int kSurfaceGraphConstantRegister = 10;
    inline constexpr int kSurfaceGraphFirstTexture = 11;
}

#endif // CORE_EDITOR
