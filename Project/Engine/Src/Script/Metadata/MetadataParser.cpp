#include "pch.h"
#include "Script/Metadata/MetadataParser.h"

#include <charconv>
#include <cstddef>
#include <system_error>
#include <utility>

namespace CoreEngine::Script
{
    namespace
    {
        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        bool IsIdentifierHead(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        }

        bool IsIdentifierBody(char c)
        {
            return IsIdentifierHead(c) || (c >= '0' && c <= '9');
        }

        /// @brief 読む位置を持って文字列を先頭から読む
        class Reader
        {
        public:
            explicit Reader(std::string_view text) : text_(text) {}

            bool AtEnd() const { return pos_ >= text_.size(); }
            char Peek() const { return AtEnd() ? '\0' : text_[pos_]; }
            void Advance() { ++pos_; }

            void SkipSpaces()
            {
                while (!AtEnd() && IsSpace(text_[pos_])) {
                    ++pos_;
                }
            }

            /// @brief 識別子を読む（識別子でなければ空）
            std::string ReadIdentifier()
            {
                const std::size_t start = pos_;
                if (!AtEnd() && IsIdentifierHead(text_[pos_])) {
                    ++pos_;
                    while (!AtEnd() && IsIdentifierBody(text_[pos_])) {
                        ++pos_;
                    }
                }
                return std::string(text_.substr(start, pos_ - start));
            }

            /// @brief `"` で囲んだ文字列を読む（今の位置が開きの `"`）
            /// @return 閉じの `"` まで読めたら true
            bool ReadQuoted(std::string& out)
            {
                ++pos_;
                while (!AtEnd()) {
                    const char c = text_[pos_++];
                    if (c == '"') {
                        return true;
                    }
                    if (c == '\\' && !AtEnd()) {
                        const char escaped = text_[pos_++];
                        switch (escaped) {
                        case 'n': out.push_back('\n'); break;
                        case 't': out.push_back('\t'); break;
                        default:  out.push_back(escaped); break;
                        }
                        continue;
                    }
                    out.push_back(c);
                }
                return false;
            }

            /// @brief 引用符で囲まない引数を `,` か `)` の手前まで読む（前後の空白は含めない）
            std::string ReadBare()
            {
                const std::size_t start = pos_;
                while (!AtEnd() && text_[pos_] != ',' && text_[pos_] != ')') {
                    ++pos_;
                }
                std::size_t end = pos_;
                while (end > start && IsSpace(text_[end - 1])) {
                    --end;
                }
                return std::string(text_.substr(start, end - start));
            }

        private:
            std::string_view text_;
            std::size_t pos_ = 0;
        };

        /// @brief `(` の次から `)` までの引数を読む
        bool ReadArguments(Reader& reader, MetadataAttribute& attribute, std::string& error)
        {
            reader.SkipSpaces();
            if (reader.Peek() == ')') {
                reader.Advance();
                return true;
            }

            for (;;) {
                reader.SkipSpaces();
                std::string argument;
                if (reader.Peek() == '"') {
                    if (!reader.ReadQuoted(argument)) {
                        error = "属性「" + attribute.name + "」の文字列が閉じていません";
                        return false;
                    }
                } else {
                    argument = reader.ReadBare();
                    if (argument.empty()) {
                        error = "属性「" + attribute.name + "」に空の引数があります";
                        return false;
                    }
                }
                attribute.arguments.push_back(std::move(argument));

                reader.SkipSpaces();
                if (reader.Peek() == ',') {
                    reader.Advance();
                    continue;
                }
                if (reader.Peek() == ')') {
                    reader.Advance();
                    return true;
                }
                error = "属性「" + attribute.name + "」の引数の後に「,」か「)」がありません";
                return false;
            }
        }
    }

    bool ParseMetadata(std::string_view text, std::vector<MetadataAttribute>& out, std::string& error)
    {
        out.clear();
        error.clear();

        Reader reader(text);
        reader.SkipSpaces();
        while (!reader.AtEnd()) {
            MetadataAttribute attribute;
            attribute.name = reader.ReadIdentifier();
            if (attribute.name.empty()) {
                error = "属性の名前がありません";
                return false;
            }

            reader.SkipSpaces();
            if (reader.Peek() == '(') {
                reader.Advance();
                if (!ReadArguments(reader, attribute, error)) {
                    return false;
                }
                reader.SkipSpaces();
            }
            out.push_back(std::move(attribute));

            if (reader.Peek() == ',') {
                reader.Advance();
                reader.SkipSpaces();
                continue;
            }
            if (!reader.AtEnd()) {
                error = "属性「" + out.back().name + "」の後に「,」がありません";
                return false;
            }
        }
        return true;
    }

    std::optional<float> ParseFloatArgument(const std::string& argument)
    {
        std::string_view text = argument;
        if (!text.empty() && (text.back() == 'f' || text.back() == 'F')) {
            text.remove_suffix(1);
        }
        if (!text.empty() && text.front() == '+') {
            text.remove_prefix(1);
        }
        if (text.empty()) {
            return std::nullopt;
        }

        float value = 0.0f;
        const char* const last = text.data() + text.size();
        const auto [end, result] = std::from_chars(text.data(), last, value);
        if (result != std::errc{} || end != last) {
            return std::nullopt;
        }
        return value;
    }
}
