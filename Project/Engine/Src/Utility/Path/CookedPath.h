#pragma once

#include <filesystem>

namespace CoreEngine
{
    /// @brief 書き出し時に元のファイルから作るデータ（クック済みのデータ）の置き場
    /// @details `Application/Assets/a/b.png` から作ったものは `Application/Cooked/a/b.png…` に置く。
    ///          `Engine/Assets` も同じ。名前の後ろに何を付けるかは、データの種類ごとに決める。
    namespace CookedPath
    {
        /// @brief クック済みのデータを置くフォルダの綴り
        inline constexpr const char* kFolders[] = { "Application/Cooked", "Engine/Cooked" };

        /// @brief 元のファイルの綴り（`<根>/Assets/…`）を、クック済みの置き場の綴り（`<根>/Cooked/…`）にする
        /// @param sourceRelative `Application/Assets/…` か `Engine/Assets/…` の綴り
        /// @return どちらの下でもなければ空
        std::filesystem::path FromSource(const std::filesystem::path& sourceRelative);

        /// @brief クック済みの置き場の綴りを、元のファイルの綴りに戻す（FromSource の逆）
        /// @return `Application/Cooked/…` か `Engine/Cooked/…` でなければ空
        std::filesystem::path ToSource(const std::filesystem::path& cookedRelative);

        /// @brief `Application/…` はプロジェクトの根、`Engine/…` はエンジンの根の下の絶対パスにする
        /// @return どちらでもなければ空
        std::filesystem::path Resolve(const std::filesystem::path& relative);
    }
}
