#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

namespace CoreEngine
{
    struct AudioData;

    /// @brief 音声ファイルを PCM へ変換するデコーダの共通インターフェース
    /// @details 対応形式を増やすときは実装を 1 つ足して AudioSystem のデコーダ一覧へ
    ///          登録するだけで済むようにしてある。
    class IAudioDecoder {
    public:
        virtual ~IAudioDecoder() = default;

        /// @brief デコーダ名（ログ用）
        virtual const char* GetName() const = 0;

        /// @brief この拡張子を扱えるか
        /// @param extension 小文字に正規化済みの拡張子（例: ".wav"）
        virtual bool CanDecode(std::string_view extension) const = 0;

        /// @brief ファイルをデコードする
        /// @return デコード結果。失敗時は nullptr（理由はデコーダ側でログに出す）
        virtual std::unique_ptr<AudioData> Decode(const std::filesystem::path& path) const = 0;
    };
}
