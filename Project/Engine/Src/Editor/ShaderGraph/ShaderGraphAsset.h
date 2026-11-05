#pragma once

#ifdef CORE_EDITOR

#include "Editor/ShaderGraph/ShaderGraph.h"
#include "Editor/ShaderGraph/ShaderGraphCodeGen.h"

#include <filesystem>
#include <string>
#include <vector>

namespace CoreEngine
{
    class ShaderCompiler;
}

namespace CoreEngine::Editor::ShaderGraph
{
    /// @brief グラフの拡張子
    inline constexpr const char* kGraphExtension = ".shadergraph";

    /// @brief グラフのファイルか（.meta を含む）
    bool IsGraphFile(const std::filesystem::path& path);

    /// @brief グラフが作るシェーダのパス（グラフの隣の `名前.Graph.PS.hlsl`。ポストエフェクトは `名前.Graph.CS.hlsl`、
    ///        3D は `名前.Graph.Surface.hlsl`）
    std::filesystem::path GeneratedShaderPath(const std::filesystem::path& graphFile, Target target);

    /// @brief シェーダーグラフが作ったシェーダのパスか
    bool IsGeneratedShaderPath(const std::filesystem::path& path);

    /// @brief コンピュートシェーダの名前か（`名前.CS.hlsl`。プロジェクトのポストエフェクトの決まり）
    bool IsComputeShaderPath(const std::filesystem::path& path);

    /// @brief 目標のシェーダのプロファイル（ps_6_0 ／ cs_6_0。3D は Main Preview のピクセルシェーダの ps_6_0）
    const wchar_t* ShaderProfile(Target target);

    /// @brief Custom Function のファイルを、ファイル名だけの include にする（同じ名前のシェーダが 2 つあれば失敗）
    IncludeResolver MakeIncludeResolver();

    /// @brief フルパスを `Application/Assets/…` の綴りにする
    std::string ToAssetText(const std::filesystem::path& fullPath);

    /// @brief グラフのファイルを読む
    /// @return 読めなければ false（error に訳）
    bool LoadGraphFile(const std::filesystem::path& file, Graph& graph, std::string& error,
        std::vector<std::string>& warnings);

    /// @brief 一時ファイルへ書いてから置き換える
    bool WriteFileReplacing(const std::filesystem::path& file, const std::string& text);

    /// @brief 検査コンパイルの結果
    struct CompileCheck
    {
        bool ok = false;
        std::string message; ///< 失敗の訳（エラーか警告）
    };

    /// @brief HLSL を一時ファイルに書いてコンパイルできるか確かめる（警告も失敗として扱う）
    /// @param scratchFile 書く一時ファイル（中間フォルダの中）
    /// @param allEntries 3D のとき、Main Preview の 2 つに加えて描画とレイトレの全部の入口もコンパイルする
    CompileCheck CheckCompile(ShaderCompiler& compiler, const std::string& hlsl, const std::filesystem::path& scratchFile,
        Target target, bool allEntries = false);

    /// @brief 検査コンパイルの一時ファイルの置き場（`key.PS.hlsl` ／ `key.CS.hlsl` ／ `key.Surface.hlsl`）
    std::filesystem::path ScratchShaderPath(const std::string& key, Target target);

    /// @brief エラーの文から、そのファイルの行番号を拾う（拾えなければ空）
    std::vector<int> FindErrorLines(const std::string& message, const std::filesystem::path& file);

    /// @brief 保存の結果
    struct SaveResult
    {
        bool graphSaved = false;   ///< .shadergraph を書けたか
        bool shaderWritten = false; ///< 生成したシェーダを書いたか
        std::string error;         ///< 書けなかった訳・シェーダを作れなかった訳
        GenerateResult generated;
        int updatedMaterials = 0;  ///< 既定値を足した .mat の数
    };

    /// @brief グラフを保存し、検査を通ればシェーダを書き、.mat へ足りない値を足す
    SaveResult SaveGraph(const std::filesystem::path& graphFile, const Graph& graph, ShaderCompiler& compiler);

    /// @brief 新しいグラフを作る（グラフ・シェーダ・同じ名前の .mat）
    /// @param created 作ったグラフの場所
    /// @return 作れたら true
    bool CreateGraph(const std::filesystem::path& folder, const std::string& name, Target target,
        std::filesystem::path* created, std::string* error);

    /// @brief グラフのシェーダを指す .mat のフルパス（パスの順）
    std::vector<std::filesystem::path> FindGraphMaterials(const std::filesystem::path& graphFile, Target target);

    /// @brief グラフのシェーダを指す .mat に、足りないプロパティの既定値を足す
    /// @return 書き換えた .mat の数
    int SyncMaterials(const std::filesystem::path& graphFile, const Graph& graph);

    /// @brief 生成したシェーダの先頭から、元のグラフの綴りを読む（生成物でなければ空）
    std::string ReadSourceGraph(const std::filesystem::path& shaderFile);

    /// @brief 生成したシェーダの先頭に書いたプロパティの印（インスペクタの欄の出し方）
    struct PropertyHint
    {
        std::string name;
        bool color = false; ///< Color のプロパティ
        bool hasRange = false;
        float rangeMin = 0.0f;
        float rangeMax = 1.0f;
    };

    /// @brief 生成したシェーダの先頭から、プロパティの印を読む
    std::vector<PropertyHint> ReadPropertyHints(const std::filesystem::path& shaderFile);
}

#endif // CORE_EDITOR
