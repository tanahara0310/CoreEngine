#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    /// @brief コンパイル済み DXIL のディスクキャッシュ
    ///
    /// @details HLSL → GPU 機械語は 2 段階で、
    ///          ① DXC が HLSL を **DXIL（ベンダー非依存の中間表現）** へ変換し、
    ///          ② ドライバが PSO 生成時に DXIL を GPU の機械語へ変換する。
    ///          ①はベンダーに依存しないのでファイルに保存して使い回せる。
    ///          **②は消せない**（`ID3D12PipelineLibrary` の領分）。
    ///
    /// @details キャッシュキーは 2 段構え。include 一覧はコンパイルしてみるまで
    ///          分からない（鶏と卵）ので、こうする:
    /// @code
    ///   ① 一次キー = SHA-256(.hlsl本体 + 引数一式 + DXCバージョン)
    ///   ② <一次キー>.deps を読む（include のパスと中身ハッシュの一覧）
    ///        - 無い                 → ミス
    ///        - 全部の中身ハッシュ一致 → ヒット。<一次キー>.dxil を読む
    ///        - どれか不一致          → ミス
    ///   ③ ミスならコンパイルし、.dxil と .deps の両方を書く
    /// @endcode
    ///
    /// @warning **キーの取りこぼしは「無言で古いシェーダが使われる」形で現れる。**
    ///          共通 `.hlsli` で cbuffer レイアウトを変えたときに片方のシェーダだけ
    ///          再コンパイルされると、2 つのシェーダが違う解釈で同じ定数バッファを読む。
    ///          `CB_VERIFY_LAYOUT` は C++ と HLSL の食い違いを見る仕組みなので、
    ///          この HLSL 同士の食い違いは検出できない。
    ///          怪しいときは `Engine/Config/config_*.json` の
    ///          `shader.enableCache` を false にして切り分けること。
    ///
    /// @note ShaderCompiler が 18 箇所以上でスタックローカルとして生成されるため、
    ///       設定を持ち回せない。プロセス共有のシングルトンにしてある。
    class ShaderCacheStore {
    public:
        static ShaderCacheStore& GetInstance();

        /// @brief キャッシュを有効化してディレクトリを用意する
        /// @param cacheDirectory 保存先（例: <cwd>/Cache/ShaderCache）
        /// @param enabled        false ならこのクラスは常にミスを返し、書き込みもしない
        /// @note 最初のシェーダコンパイルより前に呼ぶこと。
        ///       CVar ではなく EngineConfig から設定するのは、**CVar の保存値が
        ///       復元されるのは DebugSubsystem の初期化時＝シェーダコンパイルより後**で、
        ///       起動時のコンパイルには間に合わないため。
        void Initialize(const std::filesystem::path& cacheDirectory, bool enabled);

        bool IsEnabled() const { return enabled_; }

        /// @brief 1 エントリを識別する情報
        /// @details ファイル名は `<シェーダ名>.<プロファイル>-<一次キー>.dxil` にする。
        ///          ハッシュだけだと `ls` しても何のキャッシュか分からず、
        ///          「このシェーダのキャッシュが効いているか」を確かめるのに
        ///          毎回 .deps を grep する羽目になる。
        /// @note ハッシュ部分は削れない。同じ .hlsl でもプロファイル・エントリ・
        ///       コンパイル引数が違えば別の DXIL になるので、1 本のシェーダが
        ///       複数エントリを持ちうる。ハッシュは名前ではなく鍵。
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

        /// @brief ファイル中身ハッシュ（同一プロセス内は使い回す）
        /// @details 同じ `.hlsli` が何十本ものシェーダから include されるので、
        ///          毎回ディスクを読むと検証だけで無視できない時間になる。
        ///          起動中にシェーダファイルが書き換わることは無い前提。
        /// @warning **シェーダのホットリロードを実装する場合、この前提が崩れる。**
        ///          編集後のファイルに対して古いハッシュを返し、変更済みの `.hlsli` でも
        ///          依存検証が通ってしまう（＝編集が反映されないキャッシュヒット）。
        ///          その際は fileHashCache_ をクリアする無効化 API を必ずセットで入れること。
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
