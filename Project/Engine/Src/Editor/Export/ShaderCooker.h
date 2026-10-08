#pragma once

#ifdef CORE_EDITOR

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Graphics/Shader/Cache/ShaderManifest.h"

namespace CoreEngine::Editor
{
    /// @brief 書き出しで、シェーダをコンパイルして DXIL（クック済みシェーダ）にする
    /// @details 対象は ShaderManifest の一覧（エディタやゲームで一度でもコンパイルしたもの）。
    ///          置き場と名前は CookedShader が決める。出力からデバッグ情報を外す（生成されるコードは同じ）。
    namespace ShaderCooker
    {
        /// @brief コンパイルするシェーダの一覧を集める
        std::vector<ShaderManifest::Entry> PlanTargets();

        /// @brief 1 本をコンパイルして destination の下へ書く（スレッドごとに 1 つのコンパイラを使う）
        /// @param destination 書き出す先の根（`Engine/Cooked/…` などをこの下へ書く）
        /// @param writtenFiles 書いたファイルの数を足す
        /// @param writtenBytes 書いた大きさ（バイト）を足す
        /// @return 失敗した訳（成功なら空）
        /// @note 一覧のパスが `Engine/Assets`・`Application/Assets` の外なら書かずに成功を返す
        std::string Cook(const ShaderManifest::Entry& target, const std::filesystem::path& destination,
            int& writtenFiles, std::uintmax_t& writtenBytes);
    }
}

#endif // CORE_EDITOR
