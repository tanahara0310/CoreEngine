#pragma once

#ifdef USE_IMGUI

#include <cstddef>
#include <string>
#include <vector>

namespace GameEditors
{
    /// @brief 1エリアぶんの区画CSV集合（MapGenerationSettings の csvPools と同じ形）
    struct StageAreaDefinition {
        std::string name;
        std::vector<std::string> paths;
    };

    /// @brief ステージ全体の構成
    /// @details GameScene::OnInitialize が読む mapSettings と同じ内容を持つ。
    ///          固定マップのサイズ情報はエディタで新規作成・サイズ変更するときに使う。
    struct StageProject {
        std::size_t chunkSizeX = 10;                                    ///< ランダム区画のX幅
        std::size_t mapSizeZ = 11;                                      ///< チャンクのZ方向マス数
        std::size_t fixedMapSizeX = 20;                                 ///< 先頭固定マップのX幅
        std::size_t fixedMapSizeZ = 9;                                  ///< 先頭固定マップのZ幅
        std::string initialAreaName = "Area1";                          ///< 開始エリア
        std::string fixedCsvPath = "Application/Assets/Maps/fixed.csv"; ///< 先頭に置く固定CSV
        std::vector<StageAreaDefinition> areas;
    };

    /// @brief ステージ構成ファイルの読み書き
    namespace StageProjectIO
    {
        /// @brief 構成ファイルの既定パス（実行時のカレントディレクトリ基準）
        inline constexpr const char* kDefaultPath = "Application/Assets/Maps/stage_project.json";

        /// @brief エリアフォルダーを置く場所
        inline constexpr const char* kAreasRoot = "Application/Assets/Maps/Areas";

        /// @brief 構成ファイルを読み込む
        /// @param path 読み込むJSONのパス
        /// @param out 読み込み先。失敗時は変更しない
        /// @return 読み込めた場合 true
        bool Load(const std::string& path, StageProject& out);

        /// @brief 構成ファイルを書き出す
        /// @return 書き出せた場合 true
        bool Save(const std::string& path, const StageProject& project);

        /// @brief エリアフォルダーを走査して構成を組み立てる
        /// @param areasRoot エリアフォルダーの親（kAreasRoot）
        /// @details 構成ファイルがまだ無いときの初期値に使う。
        ///          フォルダー名がエリア名、その直下の .csv が区画になる。
        StageProject ScanFromDisk(const std::string& areasRoot);

        /// @brief 既存の構成へ、エリアフォルダー内の未登録CSVを追加する
        /// @return 1件以上追加された場合 true
        /// @details 構成JSONを先に作った後で、エクスプローラーや旧版エディタから
        ///          追加されたCSVも、次回読み込み時に一覧へ取り込めるようにする。
        bool MergeCsvFilesFromDisk(StageProject& project, const std::string& areasRoot);

        /// @brief GameScene::OnInitialize へ貼り付けられる形の設定コードを作る
        /// @details 手動で構成を埋め込みたい場合の互換用スニペット。
        std::string BuildGameSceneSnippet(const StageProject& project);

        /// @brief エリアを名前で探す
        /// @return 見つからなければ nullptr
        const StageAreaDefinition* FindArea(const StageProject& project, const std::string& name);
    }
}

#endif // USE_IMGUI
