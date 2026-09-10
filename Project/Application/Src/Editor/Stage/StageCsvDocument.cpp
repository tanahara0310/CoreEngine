#include "pch.h"
#include "StageCsvDocument.h"

#ifdef USE_IMGUI

#include "StageChipPalette.h"

#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

using namespace CoreEngine;

namespace GameEditors
{
    namespace
    {
        /// @brief UTF-8文字列をWindowsでも正しくfilesystemのパスへ変換する
        std::filesystem::path PathFromUtf8(const std::string& path)
        {
            const std::u8string utf8Path(path.begin(), path.end());
            return std::filesystem::path(utf8Path);
        }

        /// @brief CSVの1行をセルへ割る
        /// @note 読み方は MapGeneratorComponent::LoadCsv と同じにしてある。
        ///       エディタで見えている絵とゲームが読む地形がずれると意味がないので、
        ///       あちらの規則を変えたときはここも合わせること。
        std::vector<std::string> SplitCsvRow(const std::string& line)
        {
            std::vector<std::string> cells;
            std::string cell;
            bool quoted = false;
            for (std::size_t i = 0; i < line.size(); ++i) {
                const char ch = line[i];
                if (ch == '"') {
                    if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                        cell += '"';
                        ++i;
                    } else {
                        quoted = !quoted;
                    }
                } else if (ch == ',' && !quoted) {
                    cells.push_back(std::move(cell));
                    cell.clear();
                } else {
                    cell += ch;
                }
            }
            cells.push_back(quoted ? "<invalid>" : std::move(cell));
            return cells;
        }

        /// @brief セル1つを種類へ変換する（数字・英語名どちらも受ける）
        /// @param invalidCount 変換できなかった回数の加算先
        GameComponents::MapChipType ParseChip(std::string cell, std::size_t& invalidCount)
        {
            using GameComponents::MapChipType;
            const auto first = cell.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                return MapChipType::Void;
            }
            cell = cell.substr(first, cell.find_last_not_of(" \t\r\n") - first + 1);
            std::transform(cell.begin(), cell.end(), cell.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            if (cell == "0" || cell == "void") return MapChipType::Void;
            if (cell == "1" || cell == "water") return MapChipType::Water;
            if (cell == "2" || cell == "ground") return MapChipType::Ground;
            if (cell == "3" || cell == "station") return MapChipType::Station;
            if (cell == "4" || cell == "resource") return MapChipType::Resource;
            if (cell == "5" || cell == "banana" || cell == "banana_tree" || cell == "bananatree") {
                return MapChipType::BananaTree;
            }
            if (cell == "6" || cell == "grass") return MapChipType::Grass;
            ++invalidCount;
            return MapChipType::Void;
        }
    }

    void StageCsvDocument::Reset(std::size_t sizeX, std::size_t sizeZ,
        GameComponents::MapChipType fill)
    {
        cells_.assign(sizeZ, Row(sizeX, fill));
        sizeX_ = sizeX;
        path_.clear();
        dirty_ = false;
        invalidCellCount_ = 0;
        undoStack_.clear();
    }

    bool StageCsvDocument::Load(const std::string& path)
    {
        // エディタのパスはUTF-8で保持している。Windowsではstd::stringを
        // そのままpathへ渡すと、日本語を含むファイル名が既定コードページで
        // 解釈されるため、char8_tの文字列へ変換してから渡す。
        const std::filesystem::path csvPath = PathFromUtf8(path);
        std::ifstream input(csvPath, std::ios::binary);
        if (!input) {
            Logger::GetInstance().Warnf(LogCategory::Game,
                "StageEditor: CSVを読み込めません: {}", path);
            return false;
        }

        Grid loaded;
        std::size_t width = 0;
        std::size_t invalidCount = 0;
        std::string line;
        for (std::size_t z = 0; std::getline(input, line); ++z) {
            // UTF-8 BOM付きのCSVも受ける。空行は飛ばさずVoidの行として数える。
            if (z == 0 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                line.erase(0, 3);
            }
            const auto rawCells = SplitCsvRow(line);
            Row row;
            row.reserve(rawCells.size());
            for (const auto& rawCell : rawCells) {
                row.push_back(ParseChip(rawCell, invalidCount));
            }
            width = std::max(width, row.size());
            loaded.push_back(std::move(row));
        }

        // 短い行はVoidで埋め、どの行も同じ列数にしてから編集させる。
        for (auto& row : loaded) {
            row.resize(width, GameComponents::MapChipType::Void);
        }

        cells_ = std::move(loaded);
        sizeX_ = width;
        path_ = path;
        dirty_ = false;
        invalidCellCount_ = invalidCount;
        undoStack_.clear();

        if (invalidCount > 0) {
            Logger::GetInstance().Warnf(LogCategory::Game,
                "StageEditor: CSV内の不明なチップ {} 個をVoidとして読みました: {}",
                invalidCount, path);
        }
        return true;
    }

    bool StageCsvDocument::Save(const std::string& path)
    {
        // Loadと同じく、UTF-8のパスをfilesystemへ正しく渡す。
        const std::filesystem::path csvPath = PathFromUtf8(path);
        std::error_code ec;
        if (csvPath.has_parent_path()) {
            std::filesystem::create_directories(csvPath.parent_path(), ec);
            if (ec) {
                Logger::GetInstance().Errorf(LogCategory::Game,
                    "StageEditor: CSVの保存先フォルダーを作れません: {} ({})",
                    path, ec.message());
                return false;
            }
        }

        std::ostringstream out;
        for (const auto& row : cells_) {
            for (std::size_t x = 0; x < row.size(); ++x) {
                if (x > 0) {
                    out << ',';
                }
                const bool asEmpty = writeVoidAsEmpty_
                    && row[x] == GameComponents::MapChipType::Void;
                if (!asEmpty) {
                    out << ChipToCsvId(row[x]);
                }
            }
            out << '\n';
        }

        // BOMなしUTF-8・LF改行で書く。読み手はどちらの改行も受けるので揃えておく。
        std::ofstream file(csvPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            Logger::GetInstance().Errorf(LogCategory::Game,
                "StageEditor: CSVを書き出せません: {}", path);
            return false;
        }
        const std::string text = out.str();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file) {
            Logger::GetInstance().Errorf(LogCategory::Game,
                "StageEditor: CSVの書き出しに失敗しました: {}", path);
            return false;
        }

        path_ = path;
        dirty_ = false;
        Logger::GetInstance().Infof(LogCategory::Game,
            "StageEditor: CSVを保存しました ({} x {}): {}", sizeX_, cells_.size(), path);
        return true;
    }

    void StageCsvDocument::Resize(std::size_t sizeX, std::size_t sizeZ)
    {
        if (sizeX == sizeX_ && sizeZ == cells_.size()) {
            return;
        }
        BeginStroke();
        cells_.resize(sizeZ, Row(sizeX, GameComponents::MapChipType::Void));
        for (auto& row : cells_) {
            row.resize(sizeX, GameComponents::MapChipType::Void);
        }
        sizeX_ = sizeX;
        dirty_ = true;
    }

    GameComponents::MapChipType StageCsvDocument::Get(std::size_t x, std::size_t z) const
    {
        if (z >= cells_.size() || x >= cells_[z].size()) {
            return GameComponents::MapChipType::Void;
        }
        return cells_[z][x];
    }

    bool StageCsvDocument::Set(std::size_t x, std::size_t z, GameComponents::MapChipType type)
    {
        if (z >= cells_.size() || x >= cells_[z].size()) {
            return false;
        }
        if (cells_[z][x] == type) {
            return false;
        }
        cells_[z][x] = type;
        dirty_ = true;
        return true;
    }

    void StageCsvDocument::Fill(GameComponents::MapChipType type)
    {
        BeginStroke();
        for (auto& row : cells_) {
            std::fill(row.begin(), row.end(), type);
        }
        dirty_ = true;
    }

    void StageCsvDocument::SetGrid(const Grid& grid)
    {
        BeginStroke();
        std::size_t width = 0;
        for (const auto& row : grid) {
            width = std::max(width, row.size());
        }
        cells_ = grid;
        for (auto& row : cells_) {
            row.resize(width, GameComponents::MapChipType::Void);
        }
        sizeX_ = width;
        dirty_ = true;
    }

    void StageCsvDocument::BeginStroke()
    {
        undoStack_.push_back(cells_);
        if (undoStack_.size() > kUndoLimit) {
            undoStack_.pop_front();
        }
    }

    bool StageCsvDocument::Undo()
    {
        if (undoStack_.empty()) {
            return false;
        }
        cells_ = std::move(undoStack_.back());
        undoStack_.pop_back();
        sizeX_ = cells_.empty() ? 0 : cells_.front().size();
        dirty_ = true;
        return true;
    }
}

#endif // USE_IMGUI
