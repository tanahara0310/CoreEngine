#pragma once

#ifdef CORE_EDITOR

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Editor/Export/CookRecord.h"

namespace CoreEngine::Editor
{
    /// @brief 書き出しで、モデルのファイルを .cmesh（クック済みのモデル）へ変換する
    /// @details 開発中の派生データ（ModelDerivedData）を新しくしてから写す
    ///          （元のファイルと設定が変わっていなければ変換せずに写すだけ）。
    namespace MeshCooker
    {
        /// @brief 変換するモデルのファイル（`Engine/Assets`・`Application/Assets` の下の全部）の絶対パスを集める
        std::vector<std::filesystem::path> PlanTargets();

        /// @brief 1 つを .cmesh へ変換し、destination の下へ書く
        /// @param destination 書き出す先の根（`Application/Cooked/…` などをこの下へ書く）
        /// @param writtenFiles 書いたファイルの数を足す
        /// @param writtenBytes 書いた大きさ（バイト）を足す
        /// @param records 書いた派生データの記録を足す
        /// @return 失敗した訳（成功なら空）
        std::string Cook(const std::filesystem::path& source, const std::filesystem::path& destination,
            int& writtenFiles, std::uintmax_t& writtenBytes, std::vector<CookRecord>& records);
    }
}

#endif // CORE_EDITOR
