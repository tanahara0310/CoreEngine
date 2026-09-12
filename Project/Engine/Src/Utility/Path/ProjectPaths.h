#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief データファイルの置き場を 1 か所で決める
    /// @details 実行時のカレントディレクトリに依らず常に同じ場所を指す。
    ///          これが無いと、Visual Studio からの実行（カレント＝Project/）と
    ///          exe の直接実行（カレント＝出力ディレクトリ）で保存先が分かれる。
    ///
    ///          根の決め方:
    ///            開発ビルド … カレントと exe の位置から上へ辿って `CoreEngine.vcxproj`
    ///                         を探し、見つかった `Project/` を根にする（ソースツリー）
    ///            リリース   … exe の隣
    class ProjectPaths
    {
    public:
        /// @brief 根を先に決めておく（省略可。初回参照時に自動で決まる）
        static void Prime();

        /// @brief データの根（`Project/` または exe のディレクトリ）
        static const std::filesystem::path& Root();

        /// @brief 根からの相対パスを絶対パスにする
        /// @param relative `Application/Assets/Models/x.obj` のような綴り
        /// @note 既に絶対パスなら素通しする。UTF-8 の日本語を含んでいてもよい。
        static std::filesystem::path Resolve(std::string_view relative);

        /// @brief 作り直せるもの（キャッシュ・ログ）の置き場
        /// @note 消してもビルドと実行に影響しないものだけをここへ置くこと。
        static std::filesystem::path Intermediate(std::string_view relative = {});

        /// @brief 根がどう決まったかの説明（起動ログ用）
        static const std::string& ResolutionNote();

    private:
        ProjectPaths() = delete;
    };
}
