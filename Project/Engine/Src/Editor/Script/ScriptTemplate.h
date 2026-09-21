#pragma once

#ifdef CORE_EDITOR

#include <filesystem>
#include <string>
#include <vector>

namespace CoreEngine::Editor
{
    /// @brief エディタから新しいスクリプトを作る
    /// @details 雛形は `Application/Config/ScriptTemplates/*.as.txt`。
    ///          拡張子が `.as` でないのでスクリプト本体と一緒にコンパイルされない。
    ///          雛形の中の `{CLASS}` がクラス名に置き換わる。
    namespace ScriptTemplate
    {
        /// @brief 雛形 1 つ
        struct Entry
        {
            std::string id;    ///< ファイル名から拡張子を除いたもの（Basic など）
            std::string label; ///< 選択肢に出す名前
        };

        /// @brief スクリプトを置くフォルダ（プロジェクトの根からの相対パス）
        std::filesystem::path GetScriptRoot();

        /// @brief 使える雛形を並べる（ファイルが無ければ空）
        /// @note 毎回フォルダを読むので、雛形を足したらエディタを開き直さなくても出る。
        std::vector<Entry> List();

        /// @brief クラス名として使えるか
        /// @param name 調べる名前
        /// @param outError 使えないときの訳（省略可）
        /// @return 使えるなら true
        bool IsValidClassName(const std::string& name, std::string* outError = nullptr);

        /// @brief スクリプトを作る
        /// @param folder 置き先（プロジェクトの根からの相対パス）。スクリプトのフォルダの外は断る
        /// @param className クラス名。ファイル名は `<className>.as` になる
        /// @param templateId 雛形の id（空なら中身の無いクラス）
        /// @param outPath 作ったファイルのフルパス（省略可）
        /// @param outError 作れなかったときの訳（省略可）
        /// @return 作れたら true
        bool Create(const std::filesystem::path& folder, const std::string& className,
                    const std::string& templateId, std::filesystem::path* outPath = nullptr,
                    std::string* outError = nullptr);
    }
}

#endif // CORE_EDITOR
