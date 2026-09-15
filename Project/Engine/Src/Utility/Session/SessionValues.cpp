#include "pch.h"
#include "Utility/Session/SessionValues.h"

#include <unordered_map>
#include <utility>
#include <variant>

namespace CoreEngine
{
    namespace
    {
        using Value = std::variant<std::int64_t, float, bool, std::string>;

        std::unordered_map<std::string, Value>& Values()
        {
            static std::unordered_map<std::string, Value> values;
            return values;
        }

        const Value* Find(const std::string& key)
        {
            const auto& values = Values();
            const auto it = values.find(key);
            return it != values.end() ? &it->second : nullptr;
        }
    }

    void SessionValues::SetInt(const std::string& key, std::int64_t value)
    {
        Values()[key].emplace<std::int64_t>(value);
    }

    std::int64_t SessionValues::GetInt(const std::string& key, std::int64_t fallback)
    {
        const Value* const value = Find(key);
        const std::int64_t* const stored = value ? std::get_if<std::int64_t>(value) : nullptr;
        return stored ? *stored : fallback;
    }

    void SessionValues::SetFloat(const std::string& key, float value)
    {
        Values()[key].emplace<float>(value);
    }

    float SessionValues::GetFloat(const std::string& key, float fallback)
    {
        const Value* const value = Find(key);
        if (!value) {
            return fallback;
        }
        if (const float* const stored = std::get_if<float>(value)) {
            return *stored;
        }
        if (const std::int64_t* const stored = std::get_if<std::int64_t>(value)) {
            return static_cast<float>(*stored);
        }
        return fallback;
    }

    void SessionValues::SetBool(const std::string& key, bool value)
    {
        Values()[key].emplace<bool>(value);
    }

    bool SessionValues::GetBool(const std::string& key, bool fallback)
    {
        const Value* const value = Find(key);
        const bool* const stored = value ? std::get_if<bool>(value) : nullptr;
        return stored ? *stored : fallback;
    }

    void SessionValues::SetString(const std::string& key, std::string value)
    {
        Values()[key].emplace<std::string>(std::move(value));
    }

    std::string SessionValues::GetString(const std::string& key, const std::string& fallback)
    {
        const Value* const value = Find(key);
        const std::string* const stored = value ? std::get_if<std::string>(value) : nullptr;
        return stored ? *stored : fallback;
    }

    bool SessionValues::Has(const std::string& key)
    {
        return Find(key) != nullptr;
    }

    void SessionValues::Remove(const std::string& key)
    {
        Values().erase(key);
    }

    void SessionValues::Clear()
    {
        Values().clear();
    }
}
