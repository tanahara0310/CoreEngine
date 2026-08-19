#include "pch.h"
#include "TileMap.h"
#include <algorithm>
#include <cmath>

namespace CoreEngine
{
    // ─────────────────────────────────────────────────────────────────────
    // 初期化
    // ─────────────────────────────────────────────────────────────────────
    void TileMap::Initialize(int cols, int rows, float tileSize, Vector2 origin)
    {
        cols_     = cols;
        rows_     = rows;
        tileSize_ = tileSize;
        origin_   = origin;
        layers_.clear();
    }

    // ─────────────────────────────────────────────────────────────────────
    // 動的サイズ変更（既存データ保持）
    // ─────────────────────────────────────────────────────────────────────
    void TileMap::Resize(int newCols, int newRows)
    {
        for (auto& layer : layers_) {
            // 行数を調整
            layer.tiles.resize(newRows);

            // 各行の列数を調整（既存データ保持・新規は 0 埋め）
            for (int r = 0; r < newRows; ++r) {
                if (r < static_cast<int>(layer.tiles.size())) {
                    layer.tiles[r].resize(newCols, 0);
                } else {
                    layer.tiles[r].assign(newCols, 0);
                }
            }
        }

        cols_ = newCols;
        rows_ = newRows;
    }

    // ─────────────────────────────────────────────────────────────────────
    // レイヤー操作
    // ─────────────────────────────────────────────────────────────────────
    int TileMap::AddLayer(const std::string& layerName)
    {
        int existing = GetLayerIndex(layerName);
        if (existing >= 0) {
            return existing;
        }

        TileLayer layer;
        layer.name = layerName;
        // rows_ × cols_ の 0 埋め配列を生成
        layer.tiles.assign(rows_, std::vector<uint8_t>(cols_, 0));
        layers_.push_back(std::move(layer));
        return static_cast<int>(layers_.size()) - 1;
    }

    int TileMap::GetLayerIndex(const std::string& layerName) const
    {
        for (int i = 0; i < static_cast<int>(layers_.size()); ++i) {
            if (layers_[i].name == layerName) {
                return i;
            }
        }
        return -1;
    }

    // ─────────────────────────────────────────────────────────────────────
    // タイルの読み書き
    // ─────────────────────────────────────────────────────────────────────
    void TileMap::SetTile(int layerIndex, int col, int row, uint8_t tileId)
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) { return; }
        if (!IsInRange(col, row)) { return; }
        layers_[layerIndex].tiles[row][col] = tileId;
    }

    uint8_t TileMap::GetTile(int layerIndex, int col, int row) const
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) { return 0; }
        if (!IsInRange(col, row)) { return 0; }
        return layers_[layerIndex].tiles[row][col];
    }

    const TileLayer* TileMap::GetLayer(int layerIndex) const
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) { return nullptr; }
        return &layers_[layerIndex];
    }

    TileLayer* TileMap::GetLayer(int layerIndex)
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(layers_.size())) { return nullptr; }
        return &layers_[layerIndex];
    }

    // 座標変換（Camera2D 準拠: 中央原点・Y 軸上正）
    //   タイル行 0 がマップ上端（Y 最大）、行 (rows_-1) が下端（Y 最小）
    //   world.x = origin_.x + col * tileSize_ ／ world.y = origin_.y - row * tileSize_
    std::optional<std::pair<int,int>> TileMap::WorldToTile(Vector2 worldPos) const
    {
        if (tileSize_ <= 0.0f) { return std::nullopt; }

        float localX = worldPos.x - origin_.x;
        float localY = origin_.y - worldPos.y;  // Y 軸反転

        int col = static_cast<int>(std::floor(localX / tileSize_));
        int row = static_cast<int>(std::floor(localY / tileSize_));

        if (!IsInRange(col, row)) { return std::nullopt; }
        return std::make_pair(col, row);
    }

    Vector2 TileMap::TileToWorld(int col, int row) const
    {
        // タイル中央
        return {
            origin_.x + (col + 0.5f) * tileSize_,
            origin_.y - (row + 0.5f) * tileSize_
        };
    }

    Vector2 TileMap::TileToWorldTopLeft(int col, int row) const
    {
        return {
            origin_.x + col * tileSize_,
            origin_.y - row * tileSize_
        };
    }

    // ─────────────────────────────────────────────────────────────────────
    // Solid 判定
    // ─────────────────────────────────────────────────────────────────────
    bool TileMap::IsSolid(int layerIndex, int col, int row) const
    {
        uint8_t id = GetTile(layerIndex, col, row);
        if (solidChecker_) {
            return solidChecker_(id);
        }
        return IsDefaultSolid(id);
    }

    bool TileMap::IsInRange(int col, int row) const
    {
        return col >= 0 && col < cols_ && row >= 0 && row < rows_;
    }

    // ─────────────────────────────────────────────────────────────────────
    // JSON 入出力
    // ─────────────────────────────────────────────────────────────────────
    json TileMap::ToJson() const
    {
        json j;
        j["tileSize"] = tileSize_;
        j["cols"]     = cols_;
        j["rows"]     = rows_;
        j["originX"]  = origin_.x;
        j["originY"]  = origin_.y;

        json layersJson = json::array();
        for (const auto& layer : layers_) {
            json layerJson;
            layerJson["name"] = layer.name;

            json tilesJson = json::array();
            for (const auto& row : layer.tiles) {
                json rowJson = json::array();
                for (uint8_t t : row) {
                    rowJson.push_back(t);
                }
                tilesJson.push_back(rowJson);
            }
            layerJson["tiles"] = tilesJson;
            layersJson.push_back(layerJson);
        }
        j["layers"] = layersJson;

        return j;
    }

    void TileMap::FromJson(const json& j)
    {
        tileSize_ = j.value("tileSize", kDefaultTileSize);
        cols_     = j.value("cols",     0);
        rows_     = j.value("rows",     0);
        origin_.x = j.value("originX",  0.0f);
        origin_.y = j.value("originY",  0.0f);

        layers_.clear();
        if (j.contains("layers") && j["layers"].is_array()) {
            for (const auto& layerJson : j["layers"]) {
                TileLayer layer;
                layer.name = layerJson.value("name", "");

                if (layerJson.contains("tiles") && layerJson["tiles"].is_array()) {
                    for (const auto& rowJson : layerJson["tiles"]) {
                        std::vector<uint8_t> row;
                        for (const auto& cell : rowJson) {
                            row.push_back(static_cast<uint8_t>(cell.get<int>()));
                        }
                        layer.tiles.push_back(std::move(row));
                    }
                }
                layers_.push_back(std::move(layer));
            }
        }
    }

    bool TileMap::SaveJson(const std::string& filePath) const
    {
        return JsonManager::GetInstance().SaveJson(filePath, ToJson());
    }

    bool TileMap::LoadJson(const std::string& filePath)
    {
        auto& mgr = JsonManager::GetInstance();
        if (!mgr.FileExists(filePath)) { return false; }
        FromJson(mgr.LoadJson(filePath));
        return true;
    }

} // namespace CoreEngine
