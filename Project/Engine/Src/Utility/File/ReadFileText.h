#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace CoreEngine
{
    /// @brief ファイルの中身を、改行も変えずに全部読む（開けなければ空）
    inline std::string ReadFileText(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        std::ostringstream text;
        text << file.rdbuf();
        return text.str();
    }
}
