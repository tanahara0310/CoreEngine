#pragma once

#include <filesystem>
#include <string_view>

namespace CoreEngine
{
    /// @brief 書き出し時にコンパイルしておく DXIL（クック済みシェーダ）の名前
    /// @details `Engine/Assets/Shaders/a/b.PS.hlsl` を ps_6_0 でコンパイルしたものは
    ///          `Engine/Cooked/Shaders/a/b.PS.hlsl.ps_6_0.dxil` に置く（置き場は CookedPath）。
    ///          エントリが main でも空（ライブラリ）でもなければ、プロファイルの後ろに `.<エントリ>` を足す。
    ///          書き出したゲームはコンパイルせずにこの DXIL を使う。
    namespace CookedShader
    {
        /// @brief 元のシェーダの綴りから、クック済みの DXIL の綴りを作る
        /// @param sourceRelative `Application/Assets/…` か `Engine/Assets/…` の綴り
        /// @return どちらの下でもなければ空
        std::filesystem::path ToCookedPath(const std::filesystem::path& sourceRelative,
            std::wstring_view profile, std::wstring_view entryPoint);

        /// @brief 元のシェーダに対応するクック済みの DXIL を探す
        /// @param sourcePath 元のシェーダの絶対パス
        /// @return 見つかった DXIL の絶対パス。無ければ空
        std::filesystem::path Find(const std::filesystem::path& sourcePath,
            std::wstring_view profile, std::wstring_view entryPoint);
    }
}
