#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace CoreEngine
{
    /// @brief 元のファイル（PNG など）から作るデータ（派生データ）の置き場と、作り直しの判定
    /// @details 探す順はクック済み（Cooked/）→ 開発中の置き場（Intermediate/DerivedData/）→ いま変換。
    ///          開発中の置き場の派生データは、隣の .ddinfo に控えた「元のファイルの大きさと日時・派生キー」と比べて使う。
    ///          派生キーは元のファイルの中身・種類・変換の設定と版の SHA-256。大きさか日時が違うときだけ中身を読み直す。
    ///          書き出したゲームは変換しない（無ければ Missing）。
    namespace DerivedData
    {
        /// @brief 派生データをどこから得たか
        enum class Origin
        {
            Cooked,       ///< 書き出し時に作った物
            Intermediate, ///< 開発中に作ってあった物
            Imported,     ///< いま元のファイルから作った物
            Missing,      ///< 得られなかった
        };

        /// @brief 派生データ 1 つの頼み
        struct Request
        {
            std::string kind;                     ///< 種類（"Texture" など。キー・置き場・件数に使う）
            std::filesystem::path source;         ///< 元のファイル（絶対パス）
            std::filesystem::path cooked;         ///< クック済みの置き場（絶対パス。無ければ空）
            std::filesystem::path cookedFallback; ///< cooked が無いときに代わりに使うクック済みの置き場（使うと警告を出す）
            std::filesystem::path intermediate;   ///< 開発中の置き場（絶対パス）
            std::string settings;                 ///< 変換の設定（キーに入る）
            uint32_t importerVersion = 1;         ///< 変換の版（キーに入る）
        };

        /// @brief 元のファイルを output へ変換する（書けたら true）
        using Importer = std::function<bool(const std::filesystem::path& source, const std::filesystem::path& output)>;

        /// @brief 探した結果
        struct Result
        {
            std::filesystem::path path;      ///< 読む派生データ（Missing なら空）
            Origin origin = Origin::Missing;
            std::string key;                 ///< 派生キー（クック済みを使ったときは空）
        };

        /// @brief クック済み → 開発中の置き場 → （書き出したゲームでなければ）いま変換、の順に探す
        /// @note ワーカースレッドから呼んでよい。同じ置き場への変換は 1 スレッドずつ行う。
        Result Find(const Request& request, const Importer& importer);

        /// @brief 開発中の置き場の派生データを新しくして返す（書き出しのクックが使う）
        Result EnsureIntermediate(const Request& request, const Importer& importer);

        /// @brief 開発中の置き場のフォルダ（<プロジェクト>/Intermediate/DerivedData/<kind>）
        std::filesystem::path IntermediateDirectory(std::string_view kind);

        /// @brief Find が返した件数を、種類ごとに Origin 別で 1 行ずつ出す
        void LogSummary();
    }
}
