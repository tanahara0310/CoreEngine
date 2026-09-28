#pragma once

#ifdef CORE_EDITOR

#include <filesystem>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief ランチャーの一覧に出すプロジェクト 1 つ
    struct ProjectEntry
    {
        std::filesystem::path folder; ///< プロジェクトのフォルダ（絶対パス）
        std::string name;             ///< Project.json の name（無ければフォルダ名）
        std::string initialScene;     ///< Project.json の initialScene
        int sceneCount = 0;           ///< シーンの数
        int scriptCount = 0;          ///< スクリプトの数（基底クラスを除く）
        std::string lastOpened;       ///< 最後に開いた日時（ローカル時刻の "2026-09-26T12:00:00"。開いたことが無ければ空）
        bool bundled = false;         ///< 同梱プロジェクトのフォルダの中にある
        bool missing = false;         ///< フォルダが無いか、プロジェクトではない
    };

    /// @brief ランチャーが覚えるプロジェクトの一覧
    /// @details 置き場はエンジンの `Saved/RecentProjects.json`（個人のファイル）。
    ///          同梱プロジェクトのフォルダの中は毎回数え直すので、覚えていなくても一覧に出る。
    class ProjectList
    {
    public:
        /// @brief ファイルから読む（無ければ空）
        void Load();

        /// @brief ファイルへ書く
        bool Save() const;

        /// @brief 同梱プロジェクトと覚えているものを合わせた一覧（最近開いた順、次に名前順）
        std::vector<ProjectEntry> Collect() const;

        /// @brief 開いたことを記録する（覚えていなければ足す）
        void MarkOpened(const std::filesystem::path& folder);

        /// @brief 覚えている一覧に足す
        /// @return プロジェクトのフォルダでなければ false
        bool Add(const std::filesystem::path& folder);

        /// @brief 覚えている一覧から外す（ファイルは消さない）
        void Remove(const std::filesystem::path& folder);

        /// @brief 起動したときに前回のプロジェクトを自動で開くか
        bool GetOpenLastOnStartup() const { return openLastOnStartup_; }

        /// @brief 起動したときに前回のプロジェクトを自動で開くかを決める
        void SetOpenLastOnStartup(bool enabled) { openLastOnStartup_ = enabled; }

        /// @brief 最後に開いたプロジェクト（覚えていなければ空）
        std::filesystem::path LastOpened() const;

        /// @brief フォルダを調べて一覧の 1 行を作る（開いた日時は入れない）
        static ProjectEntry Inspect(const std::filesystem::path& folder);

        /// @brief 一覧のファイルの置き場
        static std::filesystem::path FilePath();

        /// @brief 同じフォルダか（区切りの向き・大文字小文字・末尾の区切りを区別しない）
        static bool IsSameFolder(const std::filesystem::path& a, const std::filesystem::path& b);

    private:
        /// @brief 覚えているプロジェクト 1 つ
        struct Record
        {
            std::filesystem::path folder;
            std::string lastOpened;
        };

        /// @brief 覚えている中からフォルダを探す（無ければ nullptr）
        Record* Find(const std::filesystem::path& folder);
        const Record* Find(const std::filesystem::path& folder) const;

        std::vector<Record> records_;
        bool openLastOnStartup_ = false;
    };
}

#endif // CORE_EDITOR
