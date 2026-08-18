#include "pch.h"
#include "ShaderCacheKey.h"

#include <Windows.h>
#include <bcrypt.h>

#include <fstream>
#include <mutex>

#pragma comment(lib, "bcrypt.lib")

namespace CoreEngine
{
    namespace
    {
        constexpr size_t kSha256DigestSize = 32;

        // アルゴリズムプロバイダは 1 回開いてプロセス寿命で使い回す。
        // シェーダ 100 本超 × include 数だけ Open/Close するとそれ自体が重い。
        // 明示的に閉じないのは意図的（プロセス終了時に OS が回収する。
        // D3D12 オブジェクトではないので LeakChecker の報告対象にもならない）。
        BCRYPT_ALG_HANDLE GetSha256Provider()
        {
            static BCRYPT_ALG_HANDLE provider = nullptr;
            static std::once_flag onceFlag;
            std::call_once(onceFlag, []() {
                if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
                        &provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
                    provider = nullptr;
                }
                });
            return provider;
        }
    }

    Sha256Hasher::Sha256Hasher()
    {
        BCRYPT_ALG_HANDLE provider = GetSha256Provider();
        if (!provider) {
            return;
        }

        // ハッシュオブジェクトの作業領域サイズを問い合わせて自前で確保する
        DWORD objectSize = 0;
        DWORD written = 0;
        if (!BCRYPT_SUCCESS(BCryptGetProperty(
                provider, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &written, 0))) {
            return;
        }

        hashObject_.resize(objectSize);

        BCRYPT_HASH_HANDLE handle = nullptr;
        if (!BCRYPT_SUCCESS(BCryptCreateHash(
                provider, &handle, hashObject_.data(), objectSize, nullptr, 0, 0))) {
            return;
        }

        hashHandle_ = handle;
        valid_ = true;
    }

    Sha256Hasher::~Sha256Hasher()
    {
        if (hashHandle_) {
            BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(hashHandle_));
            hashHandle_ = nullptr;
        }
    }

    void Sha256Hasher::Update(const void* data, size_t size)
    {
        if (!valid_ || !data || size == 0) {
            return;
        }

        // BCryptHashData の長さは ULONG。シェーダソースで 4GB を超えることは無いが、
        // 念のため分割して食わせる
        const uint8_t* cursor = static_cast<const uint8_t*>(data);
        size_t remaining = size;
        while (remaining > 0) {
            const ULONG chunk = static_cast<ULONG>(
                (remaining > 0x40000000u) ? 0x40000000u : remaining);
            if (!BCRYPT_SUCCESS(BCryptHashData(
                    static_cast<BCRYPT_HASH_HANDLE>(hashHandle_),
                    const_cast<PUCHAR>(cursor), chunk, 0))) {
                valid_ = false;
                return;
            }
            cursor += chunk;
            remaining -= chunk;
        }
    }

    void Sha256Hasher::Update(const std::string& text)
    {
        Update(text.data(), text.size());
    }

    void Sha256Hasher::Update(const std::wstring& text)
    {
        Update(text.data(), text.size() * sizeof(wchar_t));
    }

    std::string Sha256Hasher::Finish()
    {
        if (!valid_) {
            return {};
        }

        uint8_t digest[kSha256DigestSize]{};
        if (!BCRYPT_SUCCESS(BCryptFinishHash(
                static_cast<BCRYPT_HASH_HANDLE>(hashHandle_), digest, kSha256DigestSize, 0))) {
            valid_ = false;
            return {};
        }

        static constexpr char kHexDigits[] = "0123456789abcdef";
        std::string hex;
        hex.resize(kSha256DigestSize * 2);
        for (size_t i = 0; i < kSha256DigestSize; ++i) {
            hex[i * 2 + 0] = kHexDigits[(digest[i] >> 4) & 0x0F];
            hex[i * 2 + 1] = kHexDigits[digest[i] & 0x0F];
        }
        return hex;
    }

    std::string ShaderCacheKey::HashFile(const std::filesystem::path& path)
    {
        std::error_code errorCode;
        if (!std::filesystem::exists(path, errorCode) || errorCode) {
            return {};
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }

        Sha256Hasher hasher;
        char buffer[64 * 1024];
        while (file) {
            file.read(buffer, sizeof(buffer));
            const std::streamsize readCount = file.gcount();
            if (readCount > 0) {
                hasher.Update(buffer, static_cast<size_t>(readCount));
            }
        }
        return hasher.Finish();
    }

    std::string ShaderCacheKey::ComputePrimaryKey(
        const void* sourceData,
        size_t sourceSize,
        const std::vector<std::wstring>& arguments,
        const std::string& compilerVersion)
    {
        Sha256Hasher hasher;

        // 区切り文字を挟む。挟まないと ("ab","c") と ("a","bc") が同じキーになる
        static const std::string kSeparator = "\x1f";

        hasher.Update(sourceData, sourceSize);
        hasher.Update(kSeparator);

        // 引数一式。プロファイル(-T)・エントリ(-E)・最適化指定(-Od/-O3)・
        // インクルード検索パス(-I 群)が全部ここに含まれる。
        // -I の並びまで入れるのは、検索順が変わると同じ #include が
        // 別のファイルに解決されうるため
        for (const std::wstring& argument : arguments) {
            hasher.Update(argument);
            hasher.Update(kSeparator);
        }

        // DXC を更新するとコード生成が変わるので、バージョンもキーに含める
        hasher.Update(compilerVersion);

        return hasher.Finish();
    }
}
