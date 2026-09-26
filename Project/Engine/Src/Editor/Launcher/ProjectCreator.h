#pragma once

#ifdef CORE_EDITOR

#include <filesystem>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief 新しいプロジェクトのテンプレート 1 つ
    /// @details 置き場は `Engine/Templates/Projects/<id>/`。中の `Application` を丸ごと写し、
    ///          `Template.json` に表示名・説明・起動シーン・並び順を書く。
    struct ProjectTemplate
    {
        std::string id;                  ///< フォルダ名
        std::string name;                ///< 表示名
        std::string description;         ///< 説明
        std::string initialScene;        ///< 起動シーン
        std::vector<std::string> scenes; ///< 入っているシーン
        std::vector<std::string> scripts;///< 入っているスクリプトのファイル名
        int order = 0;                   ///< 並び順（小さいほど先）
        std::filesystem::path folder;    ///< テンプレートのフォルダ
    };

    /// @brief テンプレートから新しいプロジェクトを作る
    namespace ProjectCreator
    {
        /// @brief 使えるテンプレートを並び順に返す
        std::vector<ProjectTemplate> ListTemplates();

        /// @brief 作れるかを調べる
        /// @param name プロジェクトの名前（そのままフォルダ名になる。英数字と _ - だけ）
        /// @param location 作る場所（このフォルダの下に name のフォルダを作る）
        /// @param outWarning 作れるが気をつけること（日本語を含む保存先など。無ければ空）
        /// @return 作れないときの訳（作れるなら空）
        std::string Check(const std::string& name, const std::filesystem::path& location, std::string* outWarning);

        /// @brief テンプレートからプロジェクトを作る
        /// @details `Application` を写し、`Project.json`・`.gitignore`・`<name>.code-workspace` を書く。
        ///          途中で失敗したら、作ったフォルダを消す。
        /// @param outFolder 作ったプロジェクトのフォルダ
        /// @param outError 作れなかった訳
        bool Create(const ProjectTemplate& projectTemplate, const std::string& name,
                    const std::filesystem::path& location, std::filesystem::path* outFolder, std::string* outError);
    }
}

#endif // CORE_EDITOR
