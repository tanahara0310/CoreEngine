#pragma once

#include <cstdint>
#include <filesystem>

#include "Graphics/Model/Import/MeshAsset.h"

namespace CoreEngine
{
    /// @brief モデルの派生データ .cmesh の読み書き
    /// @details リトルエンディアン。32 バイトの見出し（"CMSH"・版・旗・頂点の大きさ・LOD0 のインデックス数・チャンク数）の後に
    ///          チャンク（4 文字の名前・大きさ・中身。16 バイト境界に揃える）が続く。知らないチャンクは読み飛ばす。
    ///          VERT（頂点）・INDX（インデックス）・SUBM（サブメッシュ）・MATL（マテリアル）・NODE（ノード階層）・
    ///          SKIN（スキン）・ANIM（アニメーション）。
    namespace CmeshFile
    {
        /// @brief 形式の版（中身を変えたら上げる）
        inline constexpr uint32_t kVersion = 1;

        /// @brief .cmesh を書く
        bool Write(const std::filesystem::path& path, const MeshAsset& asset);

        /// @brief .cmesh を読む
        /// @return 読めないとき・壊れているとき・版か頂点の大きさが違うときは false
        bool Read(const std::filesystem::path& path, MeshAsset& asset);
    }
}
