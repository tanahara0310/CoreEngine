#include "pch.h"
#include "Editor/Scene/SceneNameList.h"

#ifdef CORE_EDITOR

#include <algorithm>
#include <unordered_map>

namespace CoreEngine::Editor::SceneNameList
{
    namespace
    {
        /// @brief UTF-8 の 1 字（バイトの位置と長さ）
        struct Glyph
        {
            std::size_t offset = 0;
            std::size_t length = 0;
        };

        /// @brief UTF-8 の文字列を 1 字ずつに分ける
        std::vector<Glyph> SplitGlyphs(std::string_view text)
        {
            std::vector<Glyph> glyphs;
            glyphs.reserve(text.size());
            for (std::size_t i = 0; i < text.size();) {
                const auto lead = static_cast<unsigned char>(text[i]);
                std::size_t length = 1;
                if (lead >= 0xF0) {
                    length = 4;
                } else if (lead >= 0xE0) {
                    length = 3;
                } else if (lead >= 0xC0) {
                    length = 2;
                }
                length = std::min(length, text.size() - i);
                glyphs.push_back({ i, length });
                i += length;
            }
            return glyphs;
        }

        char LowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        /// @brief 英字の大文字と小文字を区別せずに 1 字を比べる
        bool SameGlyph(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size()) {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (LowerAscii(a[i]) != LowerAscii(b[i])) {
                    return false;
                }
            }
            return true;
        }

        std::string_view GlyphText(std::string_view text, const Glyph& glyph)
        {
            return text.substr(glyph.offset, glyph.length);
        }

        /// @brief 空白（半角と全角）か
        bool IsSpace(std::string_view glyph)
        {
            return glyph == " " || glyph == "\t" || glyph == "\xE3\x80\x80";
        }

        /// @brief 語の頭の字か（先頭・`_` や空白の直後・英大文字）
        bool IsWordStart(std::string_view name, const std::vector<Glyph>& glyphs, std::size_t index)
        {
            if (index == 0) {
                return true;
            }
            const char current = name[glyphs[index].offset];
            const char previous = name[glyphs[index - 1].offset];
            return previous == '_' || previous == ' ' || (current >= 'A' && current <= 'Z');
        }

        /// @brief 英字の大文字と小文字を区別せずに比べる（同じなら 0）
        int CompareNoCase(std::string_view a, std::string_view b)
        {
            const std::size_t count = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < count; ++i) {
                const auto x = static_cast<unsigned char>(LowerAscii(a[i]));
                const auto y = static_cast<unsigned char>(LowerAscii(b[i]));
                if (x != y) {
                    return x < y ? -1 : 1;
                }
            }
            if (a.size() == b.size()) {
                return 0;
            }
            return a.size() < b.size() ? -1 : 1;
        }

        /// @brief 大文字と小文字を区別しない名前順（同じなら文字コード順）
        bool NameLess(std::string_view a, std::string_view b)
        {
            const int order = CompareNoCase(a, b);
            return order != 0 ? order < 0 : a < b;
        }

        void PushRange(std::vector<std::pair<std::size_t, std::size_t>>& ranges, const Glyph& glyph)
        {
            if (!ranges.empty() && ranges.back().first + ranges.back().second == glyph.offset) {
                ranges.back().second += glyph.length;
            } else {
                ranges.emplace_back(glyph.offset, glyph.length);
            }
        }
    }

    std::string_view Prefix(std::string_view sceneName)
    {
        const std::size_t at = sceneName.find('_');
        if (at == std::string_view::npos || at == 0) {
            return {};
        }
        return sceneName.substr(0, at);
    }

    std::string_view ShortName(std::string_view sceneName, std::string_view group)
    {
        if (group.empty() || sceneName.size() <= group.size() + 1
            || sceneName.substr(0, group.size()) != group || sceneName[group.size()] != '_') {
            return sceneName;
        }
        return sceneName.substr(group.size() + 1);
    }

    std::vector<Entry> Group(const std::vector<std::string>& sceneNames)
    {
        std::unordered_map<std::string_view, std::size_t> counts;
        for (const std::string& name : sceneNames) {
            if (const std::string_view prefix = Prefix(name); !prefix.empty()) {
                ++counts[prefix];
            }
        }

        std::vector<Entry> entries;
        std::unordered_map<std::string_view, std::size_t> groupIndex;
        for (const std::string& name : sceneNames) {
            const std::string_view prefix = Prefix(name);
            if (prefix.empty() || counts[prefix] < 2) {
                entries.push_back({ {}, { name } });
                continue;
            }
            const auto [it, added] = groupIndex.try_emplace(prefix, entries.size());
            if (added) {
                entries.push_back({ std::string(prefix), {} });
            }
            entries[it->second].scenes.push_back(name);
        }

        for (Entry& entry : entries) {
            std::sort(entry.scenes.begin(), entry.scenes.end(), NameLess);
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            const std::string_view keyA = a.IsGroup() ? std::string_view(a.group) : std::string_view(a.scenes.front());
            const std::string_view keyB = b.IsGroup() ? std::string_view(b.group) : std::string_view(b.scenes.front());
            return NameLess(keyA, keyB);
        });
        return entries;
    }

    bool IsBlank(std::string_view query)
    {
        const std::vector<Glyph> glyphs = SplitGlyphs(query);
        return std::all_of(glyphs.begin(), glyphs.end(),
            [query](const Glyph& glyph) { return IsSpace(GlyphText(query, glyph)); });
    }

    std::optional<Match> Find(std::string_view query, std::string_view sceneName)
    {
        // 空白を除いた絞り込みの字
        std::vector<std::string_view> wanted;
        for (const Glyph& glyph : SplitGlyphs(query)) {
            if (const std::string_view text = GlyphText(query, glyph); !IsSpace(text)) {
                wanted.push_back(text);
            }
        }
        if (wanted.empty()) {
            return Match{};
        }

        const std::vector<Glyph> glyphs = SplitGlyphs(sceneName);
        const float lengthPenalty = static_cast<float>(glyphs.size()) * 0.01f;

        // 続けて当たる所があれば、一番前のものを使う
        for (std::size_t start = 0; start + wanted.size() <= glyphs.size(); ++start) {
            bool all = true;
            for (std::size_t k = 0; k < wanted.size() && all; ++k) {
                all = SameGlyph(GlyphText(sceneName, glyphs[start + k]), wanted[k]);
            }
            if (!all) {
                continue;
            }
            Match match;
            for (std::size_t k = 0; k < wanted.size(); ++k) {
                PushRange(match.ranges, glyphs[start + k]);
            }
            const float wordPenalty = IsWordStart(sceneName, glyphs, start) ? 0.0f : 50.0f;
            match.score = 1000.0f - wordPenalty - static_cast<float>(start) - lengthPenalty;
            return match;
        }

        // 字を順に拾う
        std::vector<std::size_t> picked;
        picked.reserve(wanted.size());
        for (std::size_t i = 0; i < glyphs.size() && picked.size() < wanted.size(); ++i) {
            if (SameGlyph(GlyphText(sceneName, glyphs[i]), wanted[picked.size()])) {
                picked.push_back(i);
            }
        }
        if (picked.size() < wanted.size()) {
            return std::nullopt;
        }

        Match match;
        float gaps = 0.0f;
        float wordStarts = 0.0f;
        for (std::size_t k = 0; k < picked.size(); ++k) {
            if (k > 0) {
                gaps += static_cast<float>(picked[k] - picked[k - 1] - 1);
            }
            if (IsWordStart(sceneName, glyphs, picked[k])) {
                wordStarts += 1.0f;
            }
            PushRange(match.ranges, glyphs[picked[k]]);
        }
        match.score = 500.0f + wordStarts * 20.0f - gaps * 2.0f - static_cast<float>(picked.front()) * 0.5f - lengthPenalty;
        return match;
    }

    std::vector<Hit> Search(std::string_view query, const std::vector<std::string>& sceneNames)
    {
        std::vector<Hit> hits;
        for (const std::string& name : sceneNames) {
            if (std::optional<Match> match = Find(query, name)) {
                hits.push_back({ name, std::move(*match) });
            }
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
            if (a.match.score != b.match.score) {
                return a.match.score > b.match.score;
            }
            return NameLess(a.name, b.name);
        });
        return hits;
    }
}

#endif // CORE_EDITOR
