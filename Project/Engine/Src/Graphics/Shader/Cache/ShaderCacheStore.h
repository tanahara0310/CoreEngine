#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    /// @brief コンパイル済み DXIL をディスクへ保存して再利用するキャッシュ
    /// @details キーは「.hlsl 本体＋引数＋DXC バージョン」の一次キーと、
    ///          `.deps` に記録した include 群の中身ハッシュの 2 段構え。
    class ShaderCacheStore {
    public:
        /// @brief プロセス共有のインスタンスを取得
        static ShaderCacheStore& GetInstance();

        /// @brief キャッシュを有効化してディレクトリを用意する
        /// @param cacheDirectory 保存先（例: <cwd>/Cache/ShaderCache）
        /// @param enabled        false なら常にミスを返し、書き込みもしない
        /// @note 最初のシェーダコンパイルより前に呼ぶこと
        void Initialize(const std::filesystem::path& cacheDirectory, bool enabled);

        bool IsEnabled() const { return enabled_; }

        /// @brief 1 エントリを識別する情報（ファイル名は `<シェーダ名>.<プロファイル>-<一次キー>.dxil`）
        struct EntryInfo {
            std::string primaryKey;       ///< SHA-256（鍵）
            std::string sourcePathUtf8;   ///< 元 .hlsl のフルパス（.deps のコメント用）
            std::string profile;          ///< "ps_6_6" など
        };

        /// @brief キャッシュから DXIL を取り出す（依存ファイルの検証込み）
        /// @param info    エントリ識別情報
        /// @param blobOut ヒット時に DXIL のバイト列が入る
        /// @return ヒットしたら true
        bool TryLoad(const EntryInfo& info, std::vector<uint8_t>& blobOut);

        /// @brief DXIL と依存一覧を書き出す
        /// @param info         エントリ識別情報
        /// @param data         DXIL コンテナの先頭
        /// @param size         その長さ
        /// @param dependencies コンパイル中に実際に開いた include ファイル
        void Save(const EntryInfo& info,
                  const void* data,
                  size_t size,
                  const std::vector<std::filesystem::path>& dependencies);

        /// @brief ヒット / ミスの集計をログへ出す（起動完了時に 1 回）
        void LogSummary();

        uint32_t GetHitCount() const { return hitCount_; }
        uint32_t GetMissCount() const { return missCount_; }

    private:
        ShaderCacheStore() = default;

        /// @brief `<シェーダ名>.<プロファイル>-<一次キー>` を組み立てる
        /// @details ファイル名に使えない文字は '_' へ落とす。
        ///          可読部が無い／作れない場合は一次キーだけにフォールバックする。
        static std::string MakeFileStem(const EntryInfo& info);

        std::filesystem::path GetBlobPath(const EntryInfo& info) const;
        std::filesystem::path GetDepsPath(const EntryInfo& info) const;

        /// @brief ファイル中身のハッシュを返す（同一プロセス内はキャッシュを使い回す）
        /// @warning ホットリロードを入れるなら fileHashCache_ の無効化 API が必須
        std::string GetFileHashCached(const std::filesystem::path& path);

        std::filesystem::path cacheDirectory_;
        bool enabled_ = false;

        std::mutex mutex_;
        std::unordered_map<std::string, std::string> fileHashCache_;   // UTF-8 パス -> SHA-256

        uint32_t hitCount_ = 0;
        uint32_t missCount_ = 0;
        uint64_t savedBytes_ = 0;
    };
}
