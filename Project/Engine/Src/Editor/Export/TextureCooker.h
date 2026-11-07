#pragma once

#ifdef CORE_EDITOR

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Export/CookRecord.h"

namespace CoreEngine::Editor
{
    /// @brief 書き出しで DDS へ変換する画像 1 枚
    struct CookTarget
    {
        std::filesystem::path source;   ///< 元の画像の絶対パス
        std::filesystem::path relative; ///< 元の画像の綴り（`Application/Assets/…` か `Engine/Assets/…`）
        bool srgb = false;              ///< sRGB 版を作るか
        bool linear = false;            ///< リニア版を作るか
        bool normal = false;            ///< 法線マップ版を作るか
    };

    /// @brief 書き出しで、画像のテクスチャを DDS（クック済みテクスチャ）へ変換する
    /// @details 置き場と名前は CookedTexture が決める。開発中の派生データ（TextureDerivedData）を新しくしてから写す
    ///          （元の画像と設定が変わっていなければ変換せずに写すだけ）。
    namespace TextureCooker
    {
        /// @brief 変換する画像と、作る色空間の版を決める
        /// @param sources 書き出す画像の絶対パス
        /// @details モデルのマテリアルで法線に使う画像は法線マップ版、メタリック/ラフネス・遮蔽に使う画像はリニア版を作る。
        ///          それ以外の使い道がある画像と、モデルから使われていない画像は sRGB 版を作る。
        ///          取り込み設定で色空間を決めた画像は、実行時に読む版（設定を当てた後の色空間）を作る。
        std::vector<CookTarget> PlanTargets(const std::vector<std::filesystem::path>& sources);

        /// @brief 1 枚を DDS へ変換し、destination の下へ書く
        /// @param destination 書き出す先の根（`Application/Cooked/…` などをこの下へ書く）
        /// @param writtenFiles 書いたファイルの数を足す
        /// @param writtenBytes 書いた大きさ（バイト）を足す
        /// @param records 書いた派生データの記録を足す
        /// @return 失敗した訳（成功なら空）
        std::string Cook(const CookTarget& target, const std::filesystem::path& destination,
            int& writtenFiles, std::uintmax_t& writtenBytes, std::vector<CookRecord>& records);

        /// @brief 中身が同じ DDS（派生キーが同じ物）を 1 つだけ残し、消した物を別名の一覧（CookedTexture::kAliasFileName）に書く
        /// @param destination 書き出す先の根
        /// @param records Cook が書いた記録。消した DDS の記録は、残した DDS の綴りに書き換える
        /// @param removedFiles 消したファイルの数を足す
        /// @param removedBytes 消した大きさ（バイト）を足す
        /// @return 失敗した訳（成功なら空）
        std::string MergeDuplicates(const std::filesystem::path& destination, std::vector<CookRecord>& records,
            int& removedFiles, std::uintmax_t& removedBytes);
    }
}

#endif // CORE_EDITOR
