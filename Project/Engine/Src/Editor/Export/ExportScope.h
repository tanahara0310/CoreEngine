#pragma once

#ifdef CORE_EDITOR

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace CoreEngine
{
    class ThreadPool;
}

namespace CoreEngine::Editor
{
    /// @brief ゲームの書き出しに入れないファイル
    /// @details 外したシーンのフォルダと、外したシーンからだけ指されるモデル・テクスチャ・音（と、その .meta）。
    ///          書き出すもの（残るシーン・プレハブ・マテリアル・データ・設定・スクリプトの文字列）から
    ///          指されるものは、外したシーンが指していても書き出す。どのシーンも指さないアセットは、
    ///          スクリプトが組み立てたパスで読むかもしれないので書き出す。Engine/Assets は全部書き出す。
    class ExportScope
    {
    public:
        /// @brief 外すシーンから、書き出さないファイルを決める（別のスレッドで呼んでよい）
        /// @param excludedScenes 外すシーンの名前
        /// @param pool モデルのマテリアルを並べて読むスレッドプール
        static ExportScope Build(const std::vector<std::string>& excludedScenes, ThreadPool& pool);

        /// @brief そのファイルを書き出すか
        /// @param file 絶対パス
        bool Includes(const std::filesystem::path& file) const;

        /// @brief 書き出さないモデルの数
        int GetExcludedModelCount() const { return excludedModels_; }

        /// @brief 書き出さないテクスチャの数
        int GetExcludedTextureCount() const { return excludedTextures_; }

        /// @brief 書き出さない音の数
        int GetExcludedAudioCount() const { return excludedAudio_; }

        /// @brief スクリプト（エディタ用を除く）に書いてある文字列を全部集める
        static std::set<std::string> CollectScriptStrings();

    private:
        std::set<std::wstring> excludedFiles_; ///< 書き出さないファイルのキー
        int excludedModels_ = 0;
        int excludedTextures_ = 0;
        int excludedAudio_ = 0;
    };
}

#endif // CORE_EDITOR
