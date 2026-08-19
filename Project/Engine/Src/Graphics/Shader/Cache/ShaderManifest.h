#pragma once

#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief 実行時に要求されたシェーダの一覧をディスクへ記録し、次回の事前コンパイルに使う
    /// @details 手書きリストにすると追記漏れで事前コンパイルが効かなくなるため、実測を記録する方式。
    ///          保存するのは解決前のパス（絶対パスだとプロジェクト移動で全滅するため）。
    class ShaderManifest {
    public:
        /// @brief コンパイル要求 1 件を識別する情報
        struct Entry {
            std::wstring filePath;     ///< 呼び出し側が渡したパス（解決前）
            std::wstring profile;      ///< "ps_6_0" など
            std::wstring entryPoint;   ///< 空文字ならライブラリ（-E なし）

            bool operator<(const Entry& other) const
            {
                if (filePath != other.filePath) return filePath < other.filePath;
                if (profile != other.profile) return profile < other.profile;
                return entryPoint < other.entryPoint;
            }
        };

        /// @brief プロセス共有のインスタンスを取得
        static ShaderManifest& GetInstance();

        /// @brief 保存先を設定する
        /// @param manifestPath 一覧ファイルのパス（例: <cwd>/Cache/ShaderManifest.txt）
        /// @param enabled false ならこのクラスは何もしない
        void Initialize(const std::filesystem::path& manifestPath, bool enabled);

        bool IsEnabled() const { return enabled_; }

        /// @brief コンパイル要求を記録する（スレッドセーフ）
        /// @note ShaderCompiler の通常経路から呼ばれる。重複は集合で潰れる。
        void Record(const std::wstring& filePath,
            const wchar_t* profile,
            const wchar_t* entryPoint);

        /// @brief 保存済みの一覧を読み出す（事前コンパイルが使う）
        /// @return 読めなければ空。空なら事前コンパイルは何もしない
        std::vector<Entry> Load() const;

        /// @brief 記録を書き出す（起動完了時に 1 回）
        /// @note 前回と同じ内容なら書き込みを省く。毎起動でファイルの更新時刻が
        ///       動くと、ビルドシステムやバックアップから見て無駄な差分になる。
        void Save();

    private:
        ShaderManifest() = default;

        std::filesystem::path path_;
        bool enabled_ = false;

        mutable std::mutex mutex_;
        std::set<Entry> recorded_;
    };
}
