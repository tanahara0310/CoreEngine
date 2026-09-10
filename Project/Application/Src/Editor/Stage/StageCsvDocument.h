#pragma once

#ifdef USE_IMGUI

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "Components/Building/MapChipData.h"

namespace GameEditors
{
    /// @brief 区画CSV 1枚ぶんの編集データ
    /// @details CSVと同じ並び（行=Z・列=X）で持つ。MapGeneratorComponent が内部で使う
    ///          map[x][z] とは転置の関係なので、書き出すときの入れ替えは不要。
    class StageCsvDocument {
    public:
        using Row = std::vector<GameComponents::MapChipType>;
        using Grid = std::vector<Row>;

        /// @brief 指定サイズで作り直す（パスと編集履歴も捨てる）
        /// @param sizeX X方向のマス数（CSVの列数）
        /// @param sizeZ Z方向のマス数（CSVの行数）
        /// @param fill 全マスの初期値
        void Reset(std::size_t sizeX, std::size_t sizeZ, GameComponents::MapChipType fill);

        /// @brief CSVを読み込む
        /// @param path 読み込むCSVのパス（実行時のカレントディレクトリ基準）
        /// @return 読み込めた場合 true。false のとき中身は変更しない
        /// @note 行数・列数はファイルの実際の大きさをそのまま使う。
        ///       ゲーム側の切り捨て（mapSizeZ 行・csvChunkSizeX 列）はここでは行わない。
        bool Load(const std::string& path);

        /// @brief CSVへ書き出す
        /// @param path 書き出すCSVのパス
        /// @return 書き出せた場合 true
        bool Save(const std::string& path);

        /// @brief マス数を変える（増えた分は Void、減った分は切り捨て）
        void Resize(std::size_t sizeX, std::size_t sizeZ);

        /// @brief 1マスの種類を取得する（範囲外は Void）
        GameComponents::MapChipType Get(std::size_t x, std::size_t z) const;

        /// @brief 1マスの種類を変える
        /// @return 実際に値が変わった場合 true
        bool Set(std::size_t x, std::size_t z, GameComponents::MapChipType type);

        /// @brief 全マスを同じ種類で埋める
        void Fill(GameComponents::MapChipType type);

        /// @brief 中身をまるごと差し替える（履歴は1件だけ積む）
        /// @param grid 行=Z・列=X の並び。行ごとの長さが違う場合は最長へ揃える
        void SetGrid(const Grid& grid);

        std::size_t GetSizeX() const { return sizeX_; }
        std::size_t GetSizeZ() const { return cells_.size(); }

        const Grid& GetGrid() const { return cells_; }

        /// @brief 直近に読み書きしたパス（未保存の新規なら空）
        const std::string& GetPath() const { return path_; }

        /// @brief 保存していない編集があるか
        bool IsDirty() const { return dirty_; }

        /// @brief 読み込み時に数字にも名前にも当てはまらなかったセル数
        std::size_t GetInvalidCellCount() const { return invalidCellCount_; }

        /// @brief Void を空欄として書き出すか（既定は数字の 0 で書き出す）
        void SetWriteVoidAsEmpty(bool value) { writeVoidAsEmpty_ = value; }
        bool GetWriteVoidAsEmpty() const { return writeVoidAsEmpty_; }

        /// @brief ひと続きの編集の開始を記録する（ドラッグ1回 = 履歴1件にするため）
        void BeginStroke();

        /// @brief 直前の BeginStroke 時点へ戻す
        /// @return 戻せた場合 true
        bool Undo();

        bool CanUndo() const { return !undoStack_.empty(); }

    private:
        /// @brief 履歴が伸び続けないよう上限を決めておく
        static constexpr std::size_t kUndoLimit = 64;

        Grid cells_;
        std::size_t sizeX_ = 0;
        std::string path_;
        bool dirty_ = false;
        bool writeVoidAsEmpty_ = false;
        std::size_t invalidCellCount_ = 0;

        std::deque<Grid> undoStack_;
    };
}

#endif // USE_IMGUI
