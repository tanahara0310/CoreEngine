#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CoreEngine
{
/// @brief 衝突判定レイヤー
/// @details 値は添字。名前はプロジェクト設定（`Application/Config/EngineSettings/Layers.json`）が
///          持つので、レイヤーを増やすのにエンジンの再ビルドは要らない。
/// @note エンジンが名指しするのは `Default` だけ。それ以外はゲームの語彙なので、
///       C++ の列挙には並べない（`static_cast<CollisionLayer>(添字)` で作る）。
enum class CollisionLayer : std::uint8_t {
    Default = 0,  ///< 何も決めていないときのレイヤー。必ず添字 0
};

/// @brief 持てるレイヤーの上限
/// @note 当たり判定の表は常にこの大きさで持つ（名前の数を変えても表の形が変わらない）。
inline constexpr std::size_t kMaxCollisionLayers = 32;

/// @brief レイヤーの名前の表
/// @details 初回の参照で `Layers.json` を読む。無ければ既定の並びを使う。
namespace CollisionLayers
{
    /// @brief 使っているレイヤーの数（1 以上 kMaxCollisionLayers 以下）
    std::size_t Count();

    /// @brief 名前の一覧（添字がそのままレイヤーの値）
    const std::vector<std::string>& Names();

    /// @brief 名前を決めて保存する
    /// @param names 添字 0 は必ず "Default"。空文字・重複・上限超えは断る
    /// @param outError 断った訳（省略可）
    /// @return 保存できたら true
    bool SetNames(std::vector<std::string> names, std::string* outError = nullptr);

    /// @brief ファイルから読み直す
    void Reload();
}

/// @brief レイヤーの名前（範囲外なら "Default"）
const std::string& ToString(CollisionLayer layer);

/// @brief 名前からレイヤーを引く
/// @return 名前が無ければ false（out は変えない）
bool TryParseCollisionLayer(std::string_view name, CollisionLayer& out);
}
