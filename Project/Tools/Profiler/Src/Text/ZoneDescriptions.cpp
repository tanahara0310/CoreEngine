#include "Text/ZoneDescriptions.h"

#include "Text/TextFormat.h"

#include <nlohmann/json.hpp>
#include <Windows.h>

#include <algorithm>

namespace CoreEngine::Profiler
{
    namespace
    {
        /// @brief .rc で辞書を埋め込んだ名前
        constexpr const wchar_t* kResourceName = L"ZONE_DESCRIPTIONS";

        /// @brief exe の中の辞書の中身（無ければ空）
        std::string_view EmbeddedJson()
        {
            const HMODULE module = ::GetModuleHandleW(nullptr);
            const HRSRC resource = ::FindResourceW(module, kResourceName, MAKEINTRESOURCEW(10));
            if (!resource) {
                return {};
            }
            const HGLOBAL handle = ::LoadResource(module, resource);
            const DWORD size = ::SizeofResource(module, resource);
            const void* data = handle ? ::LockResource(handle) : nullptr;
            if (!data || size == 0) {
                return {};
            }
            return std::string_view(static_cast<const char*>(data), size);
        }

        /// @brief pattern の * を何文字にも当てて text と比べる。当たった中身を captures へ順に入れる（前の * ほど長く取る）
        bool MatchPattern(std::string_view pattern, std::string_view text, std::vector<std::string_view>& captures)
        {
            while (!pattern.empty() && pattern.front() != '*') {
                if (text.empty() || text.front() != pattern.front()) {
                    return false;
                }
                pattern.remove_prefix(1);
                text.remove_prefix(1);
            }
            if (pattern.empty()) {
                return text.empty();
            }
            pattern.remove_prefix(1);
            for (size_t take = text.size() + 1; take-- > 0;) {
                captures.push_back(text.substr(0, take));
                if (MatchPattern(pattern, text.substr(take), captures)) {
                    return true;
                }
                captures.pop_back();
            }
            return false;
        }

        /// @brief text の {1}・{2}… を当たった中身に置き換える
        std::string FillCaptures(std::string_view text, const std::vector<std::string_view>& captures)
        {
            std::string result;
            result.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '{') {
                    const size_t close = text.find('}', i + 1);
                    if (close != std::string_view::npos && close > i + 1) {
                        size_t index = 0;
                        bool digits = true;
                        for (size_t j = i + 1; j < close; ++j) {
                            if (text[j] < '0' || text[j] > '9') {
                                digits = false;
                                break;
                            }
                            index = index * 10 + static_cast<size_t>(text[j] - '0');
                        }
                        if (digits && index >= 1 && index <= captures.size()) {
                            result += captures[index - 1];
                            i = close;
                            continue;
                        }
                    }
                }
                result += text[i];
            }
            return result;
        }

        std::string StringOf(const nlohmann::json& value)
        {
            return value.is_string() ? value.get<std::string>() : std::string();
        }
    }

    const ZoneDescriptions& ZoneDescriptions::Instance()
    {
        static const ZoneDescriptions instance;
        return instance;
    }

    ZoneDescriptions::ZoneDescriptions()
    {
        Load(EmbeddedJson());
        if (waits_.empty()) {
            waits_ = { "Wait For Frame Slot", "Present" };
        }
    }

    void ZoneDescriptions::Load(std::string_view json)
    {
        if (json.empty()) {
            return;
        }
        const nlohmann::json root = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
        if (!root.is_object()) {
            return;
        }
        if (const auto it = root.find("names"); it != root.end() && it->is_object()) {
            for (const auto& [name, text] : it->items()) {
                names_[name] = StringOf(text);
            }
        }
        if (const auto it = root.find("patterns"); it != root.end() && it->is_array()) {
            for (const nlohmann::json& entry : *it) {
                if (!entry.is_object()) {
                    continue;
                }
                Pattern pattern;
                pattern.match = StringOf(entry.value("match", nlohmann::json()));
                pattern.file = StringOf(entry.value("file", nlohmann::json()));
                pattern.text = StringOf(entry.value("text", nlohmann::json()));
                if (!pattern.match.empty() && !pattern.text.empty()) {
                    patterns_.push_back(std::move(pattern));
                }
            }
        }
        if (const auto it = root.find("self"); it != root.end() && it->is_object()) {
            for (const auto& [name, text] : it->items()) {
                self_[name] = StringOf(text);
            }
        }
        if (const auto it = root.find("selfDefault"); it != root.end()) {
            selfDefault_ = StringOf(*it);
        }
        if (const auto it = root.find("waits"); it != root.end() && it->is_array()) {
            for (const nlohmann::json& entry : *it) {
                if (entry.is_string()) {
                    waits_.push_back(entry.get<std::string>());
                }
            }
        }
        loaded_ = true;
    }

    std::string ZoneDescriptions::Describe(std::string_view name, std::string_view file) const
    {
        if (const auto it = names_.find(std::string(name)); it != names_.end()) {
            return it->second;
        }
        const std::string_view fileName = Text::FileName(file);
        std::vector<std::string_view> captures;
        for (const Pattern& pattern : patterns_) {
            if (!pattern.file.empty() && fileName != pattern.file) {
                continue;
            }
            captures.clear();
            if (MatchPattern(pattern.match, name, captures)) {
                return FillCaptures(pattern.text, captures);
            }
        }
        return {};
    }

    std::string ZoneDescriptions::DescribeSelf(std::string_view name) const
    {
        if (const auto it = self_.find(std::string(name)); it != self_.end()) {
            return it->second;
        }
        return selfDefault_;
    }

    bool ZoneDescriptions::IsWait(std::string_view name) const
    {
        return std::find(waits_.begin(), waits_.end(), name) != waits_.end();
    }
}
