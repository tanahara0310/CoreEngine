#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace CoreEngine
{
    /// @brief SHA-256 の逐次ハッシュ計算器（Windows CNG / bcrypt を使用）
    /// @details キャッシュキーは「1 ビットでも違えば出力 DXIL が変わりうるもの」を
    ///          全部連結して取る。衝突すると**無言で別のシェーダが読まれる**という
    ///          最悪の壊れ方をするので、64bit ハッシュではなく SHA-256 を使う。
    class Sha256Hasher {
    public:
        Sha256Hasher();
        ~Sha256Hasher();

        Sha256Hasher(const Sha256Hasher&) = delete;
        Sha256Hasher& operator=(const Sha256Hasher&) = delete;

        /// @brief バイト列を追加する
        void Update(const void* data, size_t size);

        /// @brief UTF-8 文字列を追加する
        void Update(const std::string& text);

        /// @brief ワイド文字列を追加する（バイト表現をそのまま食わせる）
        void Update(const std::wstring& text);

        /// @brief 確定して 64 桁の小文字 16 進文字列を返す（失敗時は空文字）
        std::string Finish();

    private:
        void* hashHandle_ = nullptr;   // BCRYPT_HASH_HANDLE
        std::vector<uint8_t> hashObject_;
        bool valid_ = false;
    };

    /// @brief シェーダキャッシュのキー計算
    /// @details ランタイムキャッシュとビルド時プリコンパイルで**同じキー**を計算できるよう、
    ///          ShaderCompiler から切り離してある。片方だけ式を変えるとキャッシュが
    ///          永久にヒットしなくなるので、変更時は必ず両方を見ること。
    class ShaderCacheKey {
    public:
        /// @brief ファイル全体の SHA-256（読めなければ空文字）
        static std::string HashFile(const std::filesystem::path& path);

        /// @brief 一次キーを計算する
        /// @param sourceData      .hlsl 本体のバイト列（DXC へ渡すのと同じもの）
        /// @param sourceSize      その長さ
        /// @param arguments       DXC へ渡すコンパイル引数一式（-I 群を含む）
        /// @param compilerVersion DXC のバージョン識別子
        /// @return 64 桁の 16 進文字列（失敗時は空文字＝キャッシュを使わない）
        /// @note **`#include` したファイルの中身はここには入らない。**
        ///       include 一覧はコンパイルしてみるまで分からないため、
        ///       別途「依存マニフェスト（.deps）」で二段階に検証する。
        ///       ShaderCacheStore の説明を参照。
        static std::string ComputePrimaryKey(
            const void* sourceData,
            size_t sourceSize,
            const std::vector<std::wstring>& arguments,
            const std::string& compilerVersion);
    };
}
