#include "pch.h"
#include "StageProject.h"

#ifdef USE_IMGUI

#include "Utility/JsonManager/JsonManager.h"
#include "Utility/Logger/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

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

        /// @brief パス区切りを "/" に揃える
        /// @details CSVのパスはコードへ貼る文字列にもなるので、Windowsの "\" は残さない。
        std::string ToPortablePath(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        /// @brief Windowsのパス比較用に区切り文字とASCII大文字を揃える
        std::string NormalizePathForCompare(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            std::transform(path.begin(), path.end(), path.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return path;
        }

        bool HasPath(const StageAreaDefinition& area, const std::string& candidate)
        {
            const std::string normalizedCandidate = NormalizePathForCompare(candidate);
            return std::any_of(area.paths.begin(), area.paths.end(),
                [&](const std::string& path) {
                    return NormalizePathForCompare(path) == normalizedCandidate;
                });
        }
    }

    bool StageProjectIO::Load(const std::string& path, StageProject& out)
    {
        const json root = JsonManager::GetInstance().LoadJson(path);
        if (root.empty() || !root.is_object()) {
            return false;
        }

        StageProject loaded;
        loaded.chunkSizeX = JsonManager::SafeGet<std::size_t>(root, "chunkSizeX", loaded.chunkSizeX);
        loaded.mapSizeZ = JsonManager::SafeGet<std::size_t>(root, "mapSizeZ", loaded.mapSizeZ);
        loaded.fixedMapSizeX = JsonManager::SafeGet<std::size_t>(
            root, "fixedMapSizeX", loaded.fixedMapSizeX);
        loaded.fixedMapSizeZ = JsonManager::SafeGet<std::size_t>(
            root, "fixedMapSizeZ", loaded.fixedMapSizeZ);
        loaded.initialAreaName = JsonManager::SafeGet<std::string>(root, "initialArea", loaded.initialAreaName);
        loaded.fixedCsvPath = JsonManager::SafeGet<std::string>(root, "fixedCsvPath", loaded.fixedCsvPath);

        if (root.contains("areas") && root["areas"].is_array()) {
            for (const auto& element : root["areas"]) {
                if (!element.is_object()) {
                    continue;
                }
                StageAreaDefinition area;
                area.name = JsonManager::SafeGet<std::string>(element, "name", std::string{});
                if (area.name.empty()) {
                    continue;
                }
                if (element.contains("paths") && element["paths"].is_array()) {
                    for (const auto& pathElement : element["paths"]) {
                        if (pathElement.is_string()) {
                            area.paths.push_back(pathElement.get<std::string>());
                        }
                    }
                }
                loaded.areas.push_back(std::move(area));
            }
        }

        // 区画幅0はゲーム側で1へ補正される。編集画面が0マスにならないよう、ここで揃えておく。
        loaded.chunkSizeX = std::max<std::size_t>(1, loaded.chunkSizeX);
        loaded.mapSizeZ = std::max<std::size_t>(1, loaded.mapSizeZ);
        loaded.fixedMapSizeX = std::max<std::size_t>(1, loaded.fixedMapSizeX);
        loaded.fixedMapSizeZ = std::max<std::size_t>(1, loaded.fixedMapSizeZ);

        out = std::move(loaded);
        return true;
    }

    bool StageProjectIO::Save(const std::string& path, const StageProject& project)
    {
        json root = json::object();
        root["chunkSizeX"] = project.chunkSizeX;
        root["mapSizeZ"] = project.mapSizeZ;
        root["fixedMapSizeX"] = project.fixedMapSizeX;
        root["fixedMapSizeZ"] = project.fixedMapSizeZ;
        root["initialArea"] = project.initialAreaName;
        root["fixedCsvPath"] = project.fixedCsvPath;

        json areas = json::array();
        for (const auto& area : project.areas) {
            json element = json::object();
            element["name"] = area.name;
            element["paths"] = area.paths;
            areas.push_back(std::move(element));
        }
        root["areas"] = std::move(areas);

        if (!JsonManager::GetInstance().SaveJson(path, root)) {
            Logger::GetInstance().Errorf(LogCategory::Game,
                "StageEditor: ステージ構成を保存できません: {}", path);
            return false;
        }
        Logger::GetInstance().Infof(LogCategory::Game,
            "StageEditor: ステージ構成を保存しました（エリア {} 件）: {}",
            project.areas.size(), path);
        return true;
    }

    StageProject StageProjectIO::ScanFromDisk(const std::string& areasRoot)
    {
        StageProject project;

        std::error_code ec;
        const std::filesystem::path root(areasRoot);
        if (!std::filesystem::is_directory(root, ec)) {
            Logger::GetInstance().Warnf(LogCategory::Game,
                "StageEditor: エリアフォルダーが見つかりません: {}", areasRoot);
            return project;
        }

        // フォルダー名・ファイル名の昇順で並べる。走査順はOS任せなので、ここで決め打つ。
        std::vector<std::filesystem::path> areaDirs;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (entry.is_directory()) {
                areaDirs.push_back(entry.path());
            }
        }
        std::sort(areaDirs.begin(), areaDirs.end());

        for (const auto& areaDir : areaDirs) {
            StageAreaDefinition area;
            area.name = ToPortablePath(areaDir.filename());

            std::vector<std::filesystem::path> csvFiles;
            for (const auto& entry : std::filesystem::directory_iterator(areaDir, ec)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (extension == ".csv") {
                    csvFiles.push_back(entry.path());
                }
            }
            std::sort(csvFiles.begin(), csvFiles.end());

            for (const auto& csvFile : csvFiles) {
                area.paths.push_back(ToPortablePath(csvFile));
            }
            project.areas.push_back(std::move(area));
        }

        if (!project.areas.empty()) {
            project.initialAreaName = project.areas.front().name;
        }
        return project;
    }

    bool StageProjectIO::MergeCsvFilesFromDisk(
        StageProject& project, const std::string& areasRoot)
    {
        bool changed = false;
        const std::filesystem::path root = PathFromUtf8(areasRoot);

        for (auto& area : project.areas) {
            const std::filesystem::path areaDir = root / PathFromUtf8(area.name);
            std::vector<std::filesystem::path> csvFiles;
            std::error_code iteratorError;
            for (const auto& entry : std::filesystem::directory_iterator(areaDir, iteratorError)) {
                std::error_code fileError;
                if (!entry.is_regular_file(fileError) || fileError) {
                    continue;
                }
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (extension == ".csv") {
                    csvFiles.push_back(entry.path());
                }
            }
            if (iteratorError) {
                continue;
            }

            std::sort(csvFiles.begin(), csvFiles.end());
            for (const auto& csvFile : csvFiles) {
                const std::string path = ToPortablePath(csvFile);
                if (!HasPath(area, path)) {
                    area.paths.push_back(path);
                    changed = true;
                }
            }
        }

        return changed;
    }

    std::string StageProjectIO::BuildGameSceneSnippet(const StageProject& project)
    {
        std::ostringstream out;
        out << "GameComponents::MapGenerationSettings mapSettings;\n";
        out << "mapSettings.mode = GameComponents::MapGenerationMode::FixedThenRandomCsvPool;\n";
        out << "mapSettings.csvChunkSizeX = " << project.chunkSizeX << ";\n";
        out << "mapSettings.csvPools = {\n";
        for (const auto& area : project.areas) {
            out << "    { \"" << area.name << "\", {\n";
            for (const auto& path : area.paths) {
                out << "        \"" << path << "\",\n";
            }
            out << "    } },\n";
        }
        out << "};\n";
        out << "mapSettings.initialCsvPoolName = \"" << project.initialAreaName << "\";\n";
        out << "mapSettings.fixedCsvPath = \"" << project.fixedCsvPath << "\";\n";
        return out.str();
    }

    const StageAreaDefinition* StageProjectIO::FindArea(
        const StageProject& project, const std::string& name)
    {
        for (const auto& area : project.areas) {
            if (area.name == name) {
                return &area;
            }
        }
        return nullptr;
    }
}

#endif // USE_IMGUI
