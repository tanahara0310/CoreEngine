#include "pch.h"
#include "GameObject/ObjectId.h"

#include <charconv>
#include <system_error>

namespace CoreEngine
{
    std::string ObjectId::ToString() const
    {
        static constexpr char kDigits[] = "0123456789abcdef";

        std::string text(16, '0');
        for (std::size_t i = 0; i < text.size(); ++i) {
            const unsigned shift = static_cast<unsigned>((text.size() - 1 - i) * 4);
            text[i] = kDigits[(value >> shift) & 0xF];
        }
        return text;
    }

    ObjectId ObjectId::FromString(std::string_view text)
    {
        if (text.empty() || text.size() > 16) {
            return {};
        }

        std::uint64_t parsed = 0;
        const char* const last = text.data() + text.size();
        const auto [end, error] = std::from_chars(text.data(), last, parsed, 16);
        if (error != std::errc{} || end != last) {
            return {};
        }
        return ObjectId{ parsed };
    }

    ObjectId ObjectId::FromKey(std::string_view key)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const char c : key) {
            hash ^= static_cast<unsigned char>(c);
            hash *= 1099511628211ull;
        }
        return ObjectId{ hash != 0 ? hash : 1 };
    }
}
