#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace CoreEngine
{
    /// @brief 同一プロセス内でコンパイル結果 DXIL を持ち回すメモリキャッシュ
    /// @details 事前コンパイル後に PSO 生成側が同じシェーダを再要求するため、
    ///          ディスクキャッシュのヒット判定（ハッシュ照合＋読み込み）を 2 回払わずに済ませる。
    /// @warning ホットリロードを入れるなら、リロード時に Clear() すること。
    class ShaderBlobCache {
    public:
        /// @brief プロセス共有のインスタンスを取得
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
