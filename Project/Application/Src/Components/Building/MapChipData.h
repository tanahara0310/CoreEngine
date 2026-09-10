#pragma once
namespace GameComponents
{
    enum class MapChipType{
        Void,
        Water,
        Ground,
        Station,
        Resource,
        BananaTree,
        Grass, // 地面上の装飾。ゲーム上は通常の地面と同じ扱い。
    };
}
