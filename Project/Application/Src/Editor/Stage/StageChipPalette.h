#pragma once

#ifdef USE_IMGUI

#include <cstddef>
#include <cstdint>

#include "Components/Building/MapChipData.h"

namespace GameEditors
{
    /// @brief パレット1種類ぶんの定義
    struct StageChipInfo {
        GameComponents::MapChipType type;
        int csvId;              ///< CSVへ書き出す数字
        const char* label;      ///< パレットボタンに出す名前
        const char* mark;       ///< マス目へ重ねる短い記号（空文字なら塗りつぶしだけ）
        const char* hint;       ///< ホバーで出す説明（ゲーム側の意味）
        uint32_t color;         ///< マス目の色。IM_COL32 と同じ 0xAABBGGRR 並び
    };

    /// @brief 0xAABBGGRR へ詰める
    /// @details IM_COL32 と同じ並びだが、この見出しを imgui.h に依存させないため自前で作る。
    inline constexpr uint32_t PackChipColor(uint32_t r, uint32_t g, uint32_t b)
    {
        return 0xFF000000u | (b << 16) | (g << 8) | r;
    }

    /// @brief チップ一覧。並び順はCSVの数字と一致させる（数字キーの割り当てもこの順）。
    /// @note 数字と名前の対応は MapGeneratorComponent の ParseChip が正。増やすときは両方へ追記すること。
    inline constexpr StageChipInfo kStageChipPalette[] = {
        { GameComponents::MapChipType::Void,     0, "空白 (Void)",     "",   "レール設置不可。マップ外と同じ扱い",     PackChipColor( 38,  38,  44) },
        { GameComponents::MapChipType::Water,    1, "水場 (Water)",    "水", "レール設置コスト2",                      PackChipColor( 38, 110, 190) },
        { GameComponents::MapChipType::Ground,   2, "地面 (Ground)",   "",   "レール設置コスト1",                      PackChipColor( 96, 150,  72) },
        { GameComponents::MapChipType::Station,  3, "駅 (Station)",    "駅", "建物には敷設不可。正面（Z-1）の常設レールは曲げて接続でき、列車到着で減速・サル追加", PackChipColor(230, 170,  50) },
        { GameComponents::MapChipType::Resource, 4, "岩 (Resource)",   "岩", "スタミナを消費して破壊しながらレールを設置",   PackChipColor(170, 130, 205) },
        { GameComponents::MapChipType::BananaTree, 5, "バナナの木 (Banana Tree)", "バ", "レール設置不可。隣接マス通過時にスタミナ回復", PackChipColor(220, 190,  45) },
        { GameComponents::MapChipType::Grass,    6, "草 (Grass)",     "草", "地面の上に置く見た目のみの装飾。レール設置などは地面と同じ", PackChipColor( 48, 190, 100) },
    };

    /// @brief パレットの要素数
    inline constexpr std::size_t kStageChipCount =
        sizeof(kStageChipPalette) / sizeof(kStageChipPalette[0]);

    /// @brief チップ種別から定義を引く
    /// @param type 種別
    /// @return 見つからなければ Void の定義
    inline const StageChipInfo& GetChipInfo(GameComponents::MapChipType type)
    {
        for (const auto& info : kStageChipPalette) {
            if (info.type == type) {
                return info;
            }
        }
        return kStageChipPalette[0];
    }

    /// @brief チップ種別をCSVの数字へ
    inline int ChipToCsvId(GameComponents::MapChipType type)
    {
        return GetChipInfo(type).csvId;
    }

    /// @brief CSVの数字をチップ種別へ
    /// @param csvId CSVのセルに書かれていた数字
    /// @param out 変換結果
    /// @return 対応する種別があれば true
    inline bool CsvIdToChip(int csvId, GameComponents::MapChipType& out)
    {
        for (const auto& info : kStageChipPalette) {
            if (info.csvId == csvId) {
                out = info.type;
                return true;
            }
        }
        return false;
    }
}

#endif // USE_IMGUI
