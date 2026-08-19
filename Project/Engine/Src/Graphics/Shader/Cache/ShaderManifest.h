#pragma once

#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief 「このエンジンが実際にコンパイルするシェーダ」の一覧をディスクに残す仕組み
    ///
    /// @details 事前コンパイル（ShaderPrewarm）は「何を並列にコンパイルすればよいか」を
    ///          知る必要があるが、その一覧はエンジンのどこにも存在しない。
    ///          シェーダは 20 箇所以上の PSO 生成コードがそれぞれ
    ///          `ShaderCompiler` をローカルに作って個別に要求しており、
    ///          静的に集める手段が無い。
    ///
    /// @details **手書きのリストにはしない。** `RenderingTechniqueSettingsSection` の
    ///          `kTechniqueNames` が手動リストで、追記漏れによって設定が保存されない
    ///          という事故を起こしている。同じ構造を持ち込まないため、
    ///          実行時に「実際に要求されたもの」を記録して次回に使う方式にした。
    ///          初回だけ直列（記録が無いので事前コンパイルできない）で、
    ///          2 回目以降のコールド起動が並列になる。
    ///
    /// @note 保存するのは**呼び出し側が渡した解決前のパス**。絶対パスを保存すると
    ///       プロジェクトを移動した瞬間に全滅する。解決前の名前なら
    ///       AssetDatabase が次回もその環境で解決してくれる。
    ///
    /// @note 保存先は DXIL キャッシュとは別ディレクトリに置く。同じ場所に置くと
    ///       「シェーダキャッシュを消して再コンパイルさせる」という通常の操作で
    ///       一覧も消え、事前コンパイルが効かなくなる（一番効いてほしい場面で効かない）。
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
