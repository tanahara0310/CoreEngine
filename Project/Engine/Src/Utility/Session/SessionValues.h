#pragma once

#include <cstdint>
#include <string>

namespace CoreEngine
{
    /// @brief シーンをまたいで値を持つ置き場（名前と値）
    /// @details アプリを動かしている間だけ持ち、ファイルには書かない。シーンを切り替えても消えない。
    ///          1 つの名前には 1 つの値だけを持ち、別の種類の値を書くと置き換わる。メインスレッドからだけ使う。
    class SessionValues
    {
    public:
        static void SetInt(const std::string& key, std::int64_t value);

        /// @return 無いか整数でなければ fallback
        static std::int64_t GetInt(const std::string& key, std::int64_t fallback = 0);

        static void SetFloat(const std::string& key, float value);

        /// @return 小数ならその値、整数なら小数にした値、どちらでもなければ fallback
        static float GetFloat(const std::string& key, float fallback = 0.0f);

        static void SetBool(const std::string& key, bool value);

        /// @return 無いか真偽値でなければ fallback
        static bool GetBool(const std::string& key, bool fallback = false);

        static void SetString(const std::string& key, std::string value);

        /// @return 無いか文字列でなければ fallback
        static std::string GetString(const std::string& key, const std::string& fallback = {});

        /// @brief 値があるか（種類は問わない）
        static bool Has(const std::string& key);

        /// @brief 値を消す
        static void Remove(const std::string& key);

        /// @brief すべての値を消す
        static void Clear();
    };
}
