#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief データファイルの置き場を 1 か所で決める
    /// @details 実行時のカレントディレクトリに依らず常に同じ場所を指す。
    ///          根はエンジンとプロジェクトの 2 つあり、綴りの先頭が `Application` なら
    ///          プロジェクトの根、それ以外はエンジンの根から解決する。
    ///
    ///          エンジンの根:
    ///            エディタのあるビルド … カレントと exe の位置から上へ辿って `CoreEngine.vcxproj`
    ///                                  を探し、見つかった `Project/` を根にする（ソースツリー）
    ///            それ以外のビルド     … exe の隣
    ///          プロジェクトの根:
    ///            起動の引数 `--project <フォルダ>` で指定したフォルダ。
    ///            指定が無いか、指定したフォルダがプロジェクトでなければ、エンジンの根がプロジェクトなら
    ///            エンジンの根。そうでなければ（エディタのあるビルドのみ）同梱プロジェクトのフォルダで
    ///            名前が最初のプロジェクト
    class ProjectPaths
    {
    public:
        /// @brief 根を先に決めておく（省略可。初回参照時に自動で決まる）
        static void Prime();

        /// @brief エンジンの根（`Engine/` の綴りの基準）
        static const std::filesystem::path& EngineRoot();

        /// @brief 開いているプロジェクトの根（`Application/` の綴りの基準）
        static const std::filesystem::path& ProjectRoot();

        /// @brief `--project` で指定したプロジェクトを開いているか
        static bool IsProjectSpecified();

        /// @brief 綴りを絶対パスにする
        /// @param relative `Application/Assets/Models/x.obj` のような綴り。
        ///        先頭が `Application` ならプロジェクトの根、それ以外はエンジンの根から辿る
        /// @note 既に絶対パスなら素通しする。UTF-8 の日本語を含んでいてもよい。
        static std::filesystem::path Resolve(std::string_view relative);

        /// @brief 絶対パスを綴りに戻す（`Resolve` の逆）
        /// @return プロジェクトの `Application` の下なら `Application/…`、
        ///         エンジンの `Engine` の下なら `Engine/…`。どちらでもなければ空
        static std::filesystem::path MakeRelative(const std::filesystem::path& absolute);

        /// @brief プロジェクトの作り直せるもの（ログ・テクスチャのキャッシュ）の置き場
        /// @note 消してもビルドと実行に影響しないものだけをここへ置くこと。
        static std::filesystem::path Intermediate(std::string_view relative = {});

        /// @brief エンジンの作り直せるもの（シェーダー・フォントのキャッシュ）の置き場
        /// @note どのプロジェクトを開いても同じ場所を使う。
        static std::filesystem::path EngineIntermediate(std::string_view relative = {});

        /// @brief フォルダがプロジェクトか（`Application/Config/EngineSettings/Project.json` があるか）
        static bool IsProjectFolder(const std::filesystem::path& folder);

        /// @brief 同梱プロジェクトのフォルダ（エンジンの根の隣の `Projects`）
        static std::filesystem::path BundledProjectsDirectory();

        /// @brief 根がどう決まったかの説明（起動ログ用）
        static const std::string& ResolutionNote();

    private:
        ProjectPaths() = delete;
    };
}
