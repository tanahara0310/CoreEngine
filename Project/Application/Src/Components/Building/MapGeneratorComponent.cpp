#include "pch.h"
#include "MapGeneratorComponent.h"

#include "Utility/Logger/Logger.h"

#ifdef USE_IMGUI
#include "Editor/ImGui/ImGuiAll.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <utility>

using namespace CoreEngine;

namespace {
    bool UsesCsvPool(GameComponents::MapGenerationMode mode) {
        return mode == GameComponents::MapGenerationMode::RandomCsvPool
            || mode == GameComponents::MapGenerationMode::FixedThenRandomCsvPool;
    }

    bool HasFixedPrefix(GameComponents::MapGenerationMode mode) {
        return mode == GameComponents::MapGenerationMode::FixedCsv
            || mode == GameComponents::MapGenerationMode::FixedThenRandomCsvPool;
    }

    const char* ModeToString(GameComponents::MapGenerationMode mode) {
        using GameComponents::MapGenerationMode;
        switch (mode) {
        case MapGenerationMode::RandomCsvPool: return "RandomCsvPool";
        case MapGenerationMode::FixedCsv: return "FixedCsv";
        case MapGenerationMode::FixedThenRandomCsvPool: return "FixedThenRandomCsvPool";
        case MapGenerationMode::Procedural:
        default: return "Procedural";
        }
    }

    GameComponents::MapGenerationMode ModeFromString(
        const std::string& value, GameComponents::MapGenerationMode fallback) {
        using GameComponents::MapGenerationMode;
        if (value == "RandomCsvPool") return MapGenerationMode::RandomCsvPool;
        if (value == "FixedCsv") return MapGenerationMode::FixedCsv;
        if (value == "FixedThenRandomCsvPool") {
            return MapGenerationMode::FixedThenRandomCsvPool;
        }
        if (value == "Procedural") return MapGenerationMode::Procedural;
        return fallback;
    }

    // 地形セルは単一行。引用符で囲まれたセル・エスケープされた引用符も扱う。
    std::vector<std::string> SplitCsvRow(const std::string& line) {
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
        // 閉じていない引用符は不正セルとしてVoidにする。
        cells.push_back(quoted ? "<invalid>" : std::move(cell));
        return cells;
    }

    GameComponents::MapChipType ParseChip(std::string cell, std::size_t& invalidCount) {
        using GameComponents::MapChipType;
        const auto first = cell.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return MapChipType::Void;
        }
        cell = cell.substr(first, cell.find_last_not_of(" \t\r\n") - first + 1);
        std::transform(cell.begin(), cell.end(), cell.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        // 数値IDはCSVの仕様として明示し、enumの並び順には依存させない。
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

GameComponents::MapGeneratorComponent::MapGeneratorComponent(
    uint32_t mapSizeZ, uint32_t startGenerateX, MapGenerationSettings settings)
    : mapSizeZ_(mapSizeZ), initialGenerateSizeX_(startGenerateX),
      settings_(std::move(settings)) {
    if (HasFixedPrefix(settings_.mode)) {
        fixedCsv_ = LoadCsv(settings_.fixedCsvPath);
    }
    if (UsesCsvPool(settings_.mode)) {
        settings_.csvChunkSizeX = std::max<std::size_t>(1, settings_.csvChunkSizeX);
        csvRandom_.seed(settings_.randomSeed ? *settings_.randomSeed : std::random_device{}());
        LoadCsvPools();
    }
    // 初期マップを生成する
    AddMapChips(startGenerateX);
}

json GameComponents::MapGeneratorComponent::OnSerialize() const {
    return {
        { "mapSizeZ", mapSizeZ_ },
        { "initialGenerateSizeX", initialGenerateSizeX_ },
        { "mode", ModeToString(settings_.mode) },
        { "csvChunkSizeX", settings_.csvChunkSizeX },
        { "fixedCsvPath", settings_.fixedCsvPath },
        { "stationBuildInterval", stationBuildInterval_ },
        { "selectedCsvPool", GetSelectedCsvPoolName() }
    };
}

void GameComponents::MapGeneratorComponent::OnDeserialize(const json& j) {
    // GameScene がコードから渡したステージ構成を、シーン保存値で上書きしない。
    // 逆に、設定なしでプレハブを復元した場合はJSONの値を使えるようにする。
    const bool hasExplicitMapSource = settings_.mode != MapGenerationMode::Procedural
        || !settings_.fixedCsvPath.empty()
        || !settings_.csvPoolPaths.empty()
        || !settings_.csvPools.empty();
    mapSizeZ_ = std::max<uint32_t>(1, JsonManager::SafeGet<uint32_t>(j, "mapSizeZ", mapSizeZ_));
    initialGenerateSizeX_ = std::max<uint32_t>(1,
        JsonManager::SafeGet<uint32_t>(j, "initialGenerateSizeX", initialGenerateSizeX_));
    if (!hasExplicitMapSource) {
        settings_.csvChunkSizeX = std::max<std::size_t>(1,
            JsonManager::SafeGet<std::size_t>(j, "csvChunkSizeX", settings_.csvChunkSizeX));
    }
    const std::string serializedMode = JsonManager::SafeGet<std::string>(
        j, "mode", std::string{});
    if (!serializedMode.empty() && !hasExplicitMapSource) {
        settings_.mode = ModeFromString(serializedMode, settings_.mode);
    }
    if (settings_.fixedCsvPath.empty()) {
        settings_.fixedCsvPath = JsonManager::SafeGet<std::string>(
            j, "fixedCsvPath", settings_.fixedCsvPath);
    }
    stationBuildInterval_ = std::max<uint32_t>(1,
        JsonManager::SafeGet<uint32_t>(j, "stationBuildInterval", stationBuildInterval_));
    const std::string selectedPool = JsonManager::SafeGet<std::string>(
        j, "selectedCsvPool", std::string{});
    const std::string initialPoolName = settings_.initialCsvPoolName.empty()
        ? selectedPool : settings_.initialCsvPoolName;

    mapChips_.clear();
    fixedCsv_.clear();
    csvPools_.clear();
    selectedCsvPoolIndex_.reset();
    activeCsvPoolIndex_.reset();
    activeCsvIndex_ = 0;
    activeCsvColumn_ = 0;
    if (HasFixedPrefix(settings_.mode)) {
        fixedCsv_ = LoadCsv(settings_.fixedCsvPath);
    }
    if (UsesCsvPool(settings_.mode)) {
        csvRandom_.seed(settings_.randomSeed ? *settings_.randomSeed : std::random_device{}());
        settings_.initialCsvPoolName = initialPoolName;
        LoadCsvPools();
    }
    AddMapChips(initialGenerateSizeX_);
}

void GameComponents::MapGeneratorComponent::Start() {

}

void GameComponents::MapGeneratorComponent::Update() {
    
}

void GameComponents::MapGeneratorComponent::CreateToX(std::size_t xCount) {
    if (xCount <= mapChips_.size()) {
        return;
    }
    AddMapChips(xCount - mapChips_.size());
}

void GameComponents::MapGeneratorComponent::AddMapChips(std::size_t count) {
    if (settings_.mode == MapGenerationMode::Procedural) {
        AddProceduralMapChips(count);
    } else {
        AddCsvMapChips(count);
    }
    Logger::GetInstance().Infof(
        LogCategory::Game,
        "MapGeneratorComponent: AddMapChips: {} 行追加しました。現在の行数: {}", count, mapChips_.size());
}

void GameComponents::MapGeneratorComponent::LoadCsvPools() {
    auto definitions = settings_.csvPools;
    if (definitions.empty() && !settings_.csvPoolPaths.empty()) {
        definitions.push_back({ "Default", settings_.csvPoolPaths });
    }
    for (const auto& definition : definitions) {
        if (definition.name.empty() || std::any_of(csvPools_.begin(), csvPools_.end(),
            [&](const LoadedCsvPool& pool) { return pool.name == definition.name; })) {
            Logger::GetInstance().Warnf(LogCategory::Game,
                "MapGenerator: 空または重複したプール名を無視しました: {}", definition.name);
            continue;
        }
        LoadedCsvPool pool{ definition.name, {} };
        for (const auto& path : definition.paths) {
            pool.maps.push_back(LoadCsv(path, settings_.csvChunkSizeX));
        }
        csvPools_.push_back(std::move(pool));
    }

    if (!settings_.initialCsvPoolName.empty()) {
        SelectCsvPool(settings_.initialCsvPoolName);
    } else if (!csvPools_.empty()) {
        selectedCsvPoolIndex_ = 0;
    }
    if (!selectedCsvPoolIndex_) {
        Logger::GetInstance().Warnf(LogCategory::Game,
            "MapGenerator: 有効なCSVプールが未選択です。Voidで生成します");
    }
}

bool GameComponents::MapGeneratorComponent::SelectCsvPool(const std::string& name) {
    if (!UsesCsvPool(settings_.mode)) {
        return false;
    }
    for (std::size_t i = 0; i < csvPools_.size(); ++i) {
        if (csvPools_[i].name == name) {
            if (selectedCsvPoolIndex_ != i) {
                // エリア切替後は、そのエリアの全チャンクを対象に新しい周回を始める。
                csvPools_[i].shuffledIndices.clear();
                csvPools_[i].nextShuffledIndex = 0;
            }
            selectedCsvPoolIndex_ = i;
            // activeCsvPoolIndex_とactiveCsvColumn_は維持し、区画の途中では混ぜない。
            return true;
        }
    }
    Logger::GetInstance().Warnf(LogCategory::Game,
        "MapGenerator: CSVプールが見つかりません: {}", name);
    return false;
}

void GameComponents::MapGeneratorComponent::RefillCsvShuffleBag(LoadedCsvPool& pool) {
    pool.shuffledIndices.resize(pool.maps.size());
    std::iota(pool.shuffledIndices.begin(), pool.shuffledIndices.end(), std::size_t{ 0 });
    std::shuffle(pool.shuffledIndices.begin(), pool.shuffledIndices.end(), csvRandom_);
    pool.nextShuffledIndex = 0;
}

std::string GameComponents::MapGeneratorComponent::GetSelectedCsvPoolName() const {
    return selectedCsvPoolIndex_ ? csvPools_[*selectedCsvPoolIndex_].name : "";
}

std::string GameComponents::MapGeneratorComponent::GetActiveCsvPoolName() const {
    return activeCsvPoolIndex_ ? csvPools_[*activeCsvPoolIndex_].name : "";
}

std::vector<std::string> GameComponents::MapGeneratorComponent::GetCsvPoolNames() const {
    std::vector<std::string> names;
    for (const auto& pool : csvPools_) {
        names.push_back(pool.name);
    }
    return names;
}

std::size_t GameComponents::MapGeneratorComponent::GetFixedMapSizeX() const {
    return HasFixedPrefix(settings_.mode) ? fixedCsv_.size() : 0;
}

#ifdef USE_IMGUI
bool GameComponents::MapGeneratorComponent::DrawInspector() {
    bool changed = false;
    int mapSize = static_cast<int>(mapSizeZ_);
    int initialSize = static_cast<int>(initialGenerateSizeX_);
    int chunkSize = static_cast<int>(settings_.csvChunkSizeX);
    int stationInterval = static_cast<int>(stationBuildInterval_);
    ImGui::Text("生成方式: %s", ModeToString(settings_.mode));
    if (ImGui::DragInt("Z方向マップサイズ", &mapSize, 1.0f, 1, 100)) { mapSizeZ_ = static_cast<uint32_t>(std::max(mapSize, 1)); changed = true; }
    if (ImGui::DragInt("初期生成Xサイズ", &initialSize, 1.0f, 1, 500)) { initialGenerateSizeX_ = static_cast<uint32_t>(std::max(initialSize, 1)); changed = true; }
    if (ImGui::DragInt("CSV区画幅", &chunkSize, 1.0f, 1, 200)) { settings_.csvChunkSizeX = static_cast<std::size_t>(std::max(chunkSize, 1)); changed = true; }
    if (ImGui::DragInt("駅生成間隔", &stationInterval, 1.0f, 1, 200)) { stationBuildInterval_ = static_cast<uint32_t>(std::max(stationInterval, 1)); changed = true; }

    if (UsesCsvPool(settings_.mode)) {
        const std::string selectedName = GetSelectedCsvPoolName();
        if (ImGui::BeginCombo("エリアプール", selectedName.empty() ? "未選択" : selectedName.c_str())) {
            for (const auto& pool : csvPools_) {
                const bool selected = pool.name == selectedName;
                if (ImGui::Selectable(pool.name.c_str(), selected) && !selected) {
                    changed = SelectCsvPool(pool.name) || changed;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const std::size_t fixedSize = GetFixedMapSizeX();
        const std::size_t nextBoundary = mapChips_.size() < fixedSize
            ? fixedSize
            : mapChips_.size()
                + (activeCsvColumn_ == 0 ? 0 : settings_.csvChunkSizeX - activeCsvColumn_);
        if (fixedSize > 0) {
            ImGui::Text("先頭固定マップ: X = 0 〜 %zu", fixedSize - 1);
        }
        ImGui::Text("次の区画開始X: %zu", nextBoundary);
    } else {
        ImGui::TextUnformatted("プール切替はCSVプール方式で使用できます。");
    }
    ImGui::TextWrapped("数値設定は保存後、ゲームシーンの再読み込み時に反映されます。");
    return changed;
}
#endif

void GameComponents::MapGeneratorComponent::AddProceduralMapChips(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        std::vector<MapChipType> newRow(mapSizeZ_, MapChipType::Ground);
        mapChips_.push_back(newRow);
        // ランダムにvoidを配置する（10%の確率）
        for (std::size_t z = 0; z < mapSizeZ_; ++z) {
            if (rand() % 10 == 0) { // 10%の確率
                mapChips_.back()[z] = MapChipType::Void;
            }
        }
        // ランダムに水場を配置する（10%の確率）
        for (std::size_t z = 0; z < mapSizeZ_; ++z) {
            if (rand() % 10 == 0) { // 10%の確率
                mapChips_.back()[z] = MapChipType::Water;
            }
        }
        // ランダムに資源を配置する（5%の確率で資源を配置）
        for (std::size_t z = 0; z < mapSizeZ_; ++z) {
            if (rand() % 20 == 0) { // 5%の確率
                mapChips_.back()[z] = MapChipType::Resource;
            }
        }
        // 駅を建設する間隔で駅チップを配置する
        if (mapSizeZ_ > 1 && (mapChips_.size() - 1) % stationBuildInterval_ == 0) {
            // 正面の常設レールがマップ内に収まる位置に駅を配置する。
            std::size_t stationZ = 1 + rand() % (mapSizeZ_ - 1);
            mapChips_.back()[stationZ] = MapChipType::Station;
        }
    }
}

GameComponents::MapGeneratorComponent::MapData
GameComponents::MapGeneratorComponent::LoadCsv(const std::string& path, std::size_t width) const {
    // ファイルが無い場合も、ランダム生成の区画幅は失わない。
    MapData result(width, std::vector<MapChipType>(mapSizeZ_, MapChipType::Void));
    const std::filesystem::path csvPath(std::u8string(path.begin(), path.end()));
    std::ifstream input(csvPath, std::ios::binary);
    if (!input) {
        Logger::GetInstance().Warnf(LogCategory::Game,
            "MapGenerator: CSVを読み込めません。Voidで生成します: {}", path);
        return result;
    }

    std::size_t invalidCount = 0;
    std::string line;
    for (std::size_t z = 0; z < mapSizeZ_ && std::getline(input, line); ++z) {
        // UTF-8 BOM付きCSVにも対応する。空行は飛ばさずVoidの行として数える。
        if (z == 0 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }
        const auto cells = SplitCsvRow(line);
        if (width == 0 && result.size() < cells.size()) {
            result.resize(cells.size(), std::vector<MapChipType>(mapSizeZ_, MapChipType::Void));
        }
        for (std::size_t x = 0; x < std::min(cells.size(), result.size()); ++x) {
            result[x][z] = ParseChip(cells[x], invalidCount);
        }
    }
    if (invalidCount > 0) {
        Logger::GetInstance().Warnf(LogCategory::Game,
            "MapGenerator: CSV内の不明なチップ {} 個をVoidにしました: {}", invalidCount, path);
    }
    return result;
}

void GameComponents::MapGeneratorComponent::AddCsvMapChips(std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        if (HasFixedPrefix(settings_.mode) && mapChips_.size() < fixedCsv_.size()) {
            mapChips_.push_back(fixedCsv_[mapChips_.size()]);
        } else if (UsesCsvPool(settings_.mode)) {
            // 描画側とレール側から小刻みに延長されても、区画の途中で再抽選しない。
            if (activeCsvColumn_ == 0) {
                activeCsvPoolIndex_ = selectedCsvPoolIndex_;
                if (activeCsvPoolIndex_ && !csvPools_[*activeCsvPoolIndex_].maps.empty()) {
                    auto& pool = csvPools_[*activeCsvPoolIndex_];
                    if (pool.nextShuffledIndex >= pool.shuffledIndices.size()) {
                        RefillCsvShuffleBag(pool);
                    }
                    activeCsvIndex_ = pool.shuffledIndices[pool.nextShuffledIndex++];
                }
            }
            if (activeCsvPoolIndex_ && !csvPools_[*activeCsvPoolIndex_].maps.empty()) {
                mapChips_.push_back(csvPools_[*activeCsvPoolIndex_].maps[activeCsvIndex_][activeCsvColumn_]);
            } else {
                // 空プールでも区画幅を維持し、次の切替は区画境界で行う。
                mapChips_.emplace_back(mapSizeZ_, MapChipType::Void);
            }
            activeCsvColumn_ = (activeCsvColumn_ + 1) % settings_.csvChunkSizeX;
        } else {
            // 固定CSVの終端以降や空のプールは、従来のランダム生成へ戻さずVoidにする。
            mapChips_.emplace_back(mapSizeZ_, MapChipType::Void);
        }
    }
}

GameComponents::MapChipType GameComponents::MapGeneratorComponent::GetMapChip(
    std::size_t x, std::size_t z) const {
    if (x >= mapChips_.size() || z >= mapSizeZ_) {
        return MapChipType::Void;
    }
    // CSVの元データは保持し、駅の正面だけをレール用の地面として扱う。
    // 別の駅の建物がある場合は上書きしない。
    if (IsStationRailCell(x, z)) {
        return MapChipType::Ground;
    }
    return mapChips_[x][z];
}

bool GameComponents::MapGeneratorComponent::IsStationRailCell(
    std::size_t x, std::size_t z) const {
    return x < mapChips_.size() && mapSizeZ_ > 1 && z < mapSizeZ_ - 1 &&
        mapChips_[x][z] != MapChipType::Station &&
        mapChips_[x][z + 1] == MapChipType::Station;
}

bool GameComponents::MapGeneratorComponent::CanConnectRail(
    int32_t fromX, int32_t fromZ, int32_t toX, int32_t toZ) const {
    if (fromX < 0 || fromZ < 0 || toX < 0 || toZ < 0 ||
        std::abs(toX - fromX) + std::abs(toZ - fromZ) != 1) {
        return false;
    }
    const MapChipType destination = GetMapChip(
        static_cast<std::size_t>(toX), static_cast<std::size_t>(toZ));
    if (destination == MapChipType::Void || destination == MapChipType::BananaTree ||
        destination == MapChipType::Station) {
        return false;
    }
    return true;
}

bool GameComponents::MapGeneratorComponent::SetMapChip(
    std::size_t x, std::size_t z, MapChipType type) {
    if (z >= mapSizeZ_) {
        return false;
    }
    CreateToX(x + 1);
    mapChips_[x][z] = type;
    return true;
}

const std::vector<std::vector<GameComponents::MapChipType>>& GameComponents::MapGeneratorComponent::GetMapChips() const {
    return mapChips_;
}
