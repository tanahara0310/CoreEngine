#pragma once

#include "TileAttribute.h"
#include "Math/Vector/Vector2.h"
#include "Utility/JsonManager/JsonManager.h"
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <cstdint>

namespace CoreEngine
{
    /// @brief 1レイヤー分のタイルデータ
    struct TileLayer
    {
        std::string name;   ///< レイヤー名（"collision", "visual" など）
        std::vector<std::vector<uint8_t>> tiles; ///< tiles[row][col]
    };

    /// @brief タイルマップデータの管理クラス
    /// @details
    ///  - 座標系: Camera2D 準拠（中央原点・Y軸上正）
    ///  - マップ左上ワールド座標を origin_ で指定
    ///  - タイルサイズはデフォルト 32px、実行時に変更可能
    ///  - レイヤーを複数持てる（当たり判定 / 描画 を分離）
    ///  - JSON 入出力対応
    class TileMap
    {
    public:
        static constexpr float kDefaultTileSize = 32.0f;

        // ===== 初期化 =====

        /// @brief 指定サイズで空のタイルマップを生成
        /// @param cols      列数
        /// @param rows      行数
        /// @param tileSize  タイル1辺のピクセルサイズ（デフォルト 32）
        /// @param origin    マップ左上のワールド座標（Camera2D 中央原点系）
        void Initialize(int cols, int rows,
            float tileSize = kDefaultTileSize,
            Vector2 origin = { 0.0f, 0.0f });

        // ===== サイズ変更 =====

        /// @brief 動的にマップサイズを変更する（既存データは範囲内を保持）
        /// @param newCols 新しい列数
        /// @param newRows 新しい行数
        void Resize(int newCols, int newRows);

        /// @brief タイルサイズを変更する
        void SetTileSize(float tileSize) { tileSize_ = tileSize; }

        // ===== レイヤー操作 =====

        /// @brief レイヤーを追加（同名が既にある場合は既存を返す）
        /// @param layerName レイヤー名
        /// @return レイヤーインデックス
        int  AddLayer(const std::string& layerName);

        /// @brief レイヤーインデックスを名前で取得（存在しない場合は -1）
        int  GetLayerIndex(const std::string& layerName) const;

        /// @brief レイヤー数を取得
        int  GetLayerCount() const { return static_cast<int>(layers_.size()); }

        // ===== タイルの読み書き =====

        /// @brief タイル値を設定
        /// @param layerIndex レイヤーインデックス
        /// @param col        列
        /// @param row        行
        /// @param tileId     タイルID（TileAttribute の値 or ユーザー定義）
        void SetTile(int layerIndex, int col, int row, uint8_t tileId);

        /// @brief タイル値を取得（範囲外は 0 を返す）
        uint8_t GetTile(int layerIndex, int col, int row) const;

        /// @brief 指定レイヤーのタイルデータ全体を取得
        const TileLayer* GetLayer(int layerIndex) const;
        TileLayer* GetLayer(int layerIndex);

        // ===== 座標変換（Camera2D 準拠） =====

        /// @brief ワールド座標 → タイル座標（col, row）に変換
        /// @return タイル座標。マップ範囲外の場合 std::nullopt
        std::optional<std::pair<int, int>> WorldToTile(Vector2 worldPos) const;

        /// @brief タイル座標 → タイル中央のワールド座標に変換
        Vector2 TileToWorld(int col, int row) const;

        /// @brief タイル座標 → タイル左上のワールド座標に変換
        Vector2 TileToWorldTopLeft(int col, int row) const;

        // ===== Solid 判定 =====

        /// @brief 指定レイヤーの (col, row) が Solid かどうかを返す
        /// @note SolidChecker が未設定の場合は TileAttribute::Solid (1) をデフォルトとする
        bool IsSolid(int layerIndex, int col, int row) const;

        /// @brief Solid 判定カスタム関数を設定（ゲーム側で属性ルールを拡張できる）
        /// @param checker tileId を受け取り true なら Solid と判定する関数
        void SetSolidChecker(std::function<bool(uint8_t)> checker) { solidChecker_ = std::move(checker); }

        // ===== 範囲チェック =====

        bool IsInRange(int col, int row) const;

        // ===== アクセッサ =====

        int    GetCols()     const { return cols_; }
        int    GetRows()     const { return rows_; }
        float  GetTileSize() const { return tileSize_; }
        Vector2 GetOrigin()  const { return origin_; }

        /// @brief マップ左上ワールド座標を設定（Camera2D 中央原点系）
        void SetOrigin(Vector2 origin) { origin_ = origin; }

        // ===== JSON 入出力 =====

        /// @brief タイルマップを JSON に保存
        /// @param filePath 保存先パス
        bool SaveJson(const std::string& filePath) const;

        /// @brief JSON からタイルマップを読み込む
        /// @param filePath 読み込み元パス
        bool LoadJson(const std::string& filePath);

        /// @brief 生の JSON オブジェクトに変換（シーンへの組み込み用）
        json ToJson() const;

        /// @brief 生の JSON オブジェクトから復元（シーンへの組み込み用）
        void FromJson(const json& j);

    private:
        int     cols_ = 0;
        int     rows_ = 0;
        float   tileSize_ = kDefaultTileSize;
        Vector2 origin_ = { 0.0f, 0.0f };

        std::vector<TileLayer> layers_;
        std::function<bool(uint8_t)> solidChecker_;
    };

} // namespace CoreEngine
