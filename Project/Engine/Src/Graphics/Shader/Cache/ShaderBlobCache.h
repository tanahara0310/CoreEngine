#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    /// @brief 同一プロセス内でコンパイル結果 DXIL を持ち回すメモリキャッシュ
    ///
    /// @details **これが無いと事前コンパイルは「ウォーム起動を遅くする」だけになる。**
    ///          事前コンパイル（ShaderPrewarm）が全シェーダを並列に処理したあと、
    ///          本来の PSO 生成コードが同じシェーダをもう一度要求する。
    ///          ディスクキャッシュ（ShaderCacheStore）はヒットするが、
    ///          ヒット判定そのものに
    ///            ① .hlsl 本体の読み込み ② SHA-256 ③ .deps の全 include ハッシュ照合
    ///            ④ .dxil の読み込み
    ///          が必要で、これを 2 回払うことになる。
    ///          このクラスはファイル I/O を一切せずに (パス, プロファイル, エントリ)
    ///          だけで即答するので、2 回目がタダになる。
    ///
    /// @note キーが「解決済みパス + プロファイル + エントリ点」であってハッシュでないのは、
    ///       ファイルを読む前に引ける必要があるため。ディスクキャッシュの 2 段キーとは
    ///       目的が違う（あちらは**別プロセス間**で正しさを保証する仕組み）。
    ///
    /// @warning **同一プロセス内で .hlsl が書き換わらない前提に依存する。**
    ///          シェーダのホットリロードを実装する場合、リロード時に Clear() すること。
    ///          ShaderCacheStore::GetFileHashCached が同じ前提に立っているので、
    ///          前提が崩れるときはそちらもセットで無効化が必要。
    class ShaderBlobCache {
    public:
        static ShaderBlobCache& GetInstance();

        /// @brief 有効化する（無効なら Store は捨て、TryGet は常に false）
        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool IsEnabled() const { return enabled_; }

        /// @brief 結果を登録する
        void Store(const std::wstring& resolvedPath,
            const std::wstring& profile,
            const std::wstring& entryPoint,
            const void* data,
            size_t size);

        /// @brief 登録済みの DXIL を取り出す
        /// @return ヒットしたら true
        bool TryGet(const std::wstring& resolvedPath,
            const std::wstring& profile,
            const std::wstring& entryPoint,
            std::vector<uint8_t>& blobOut) const;

        /// @brief 保持しているバイト数
        size_t GetTotalBytes() const;

        /// @brief 件数
        size_t GetCount() const;

        /// @brief 全部捨てる
        /// @note 起動完了時に呼ぶ。ゲーム中はもう誰も要求しないので、
        ///       持ち続けても数 MB を無駄に占有するだけ。
        void Clear();

    private:
        ShaderBlobCache() = default;

        static std::wstring MakeKey(const std::wstring& resolvedPath,
            const std::wstring& profile,
            const std::wstring& entryPoint);

        bool enabled_ = false;

        mutable std::mutex mutex_;
        std::unordered_map<std::wstring, std::vector<uint8_t>> blobs_;
    };
}
