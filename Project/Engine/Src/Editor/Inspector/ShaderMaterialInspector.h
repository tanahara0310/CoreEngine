#pragma once

#ifdef CORE_EDITOR

#include <filesystem>
#include <string>

namespace CoreEngine
{
    class ShaderMaterial;
}

/// @brief マテリアル（.mat）の中身のインスペクタ
/// @details シェーダのパスと、シェーダの定数バッファ（b0）の項目・テクスチャを欄にして出す。
///          項目はシェーダのリフレクションから作るので、シェーダに足した値はそのまま欄になる。
///          編集を終えたら（ドラッグを離したら）.mat のファイルへ書く。Undo は積まない。
namespace CoreEngine::Editor::ShaderMaterialInspector
{
    /// @brief マテリアルの中身を描く
    /// @return 値を変えたら true
    bool Draw(ShaderMaterial& material);

    /// @brief Project で選んだ .mat を描く
    /// @return マテリアルとして描いたら true（.mat でなければ何も描かずに false）
    bool DrawFile(const std::filesystem::path& file);

    /// @brief 新しいマテリアルを作る（シェーダも作るなら雛形から書く）
    /// @param folder 置き先のフォルダ
    /// @param name ファイル名（拡張子なし）
    /// @param withShader ピクセルシェーダの雛形も作るか
    /// @param created 作った .mat の場所
    /// @param error 作れなかった訳
    /// @return 作れたら true
    bool Create(const std::filesystem::path& folder, const std::string& name, bool withShader,
        std::filesystem::path* created, std::string* error);
}

#endif // CORE_EDITOR
