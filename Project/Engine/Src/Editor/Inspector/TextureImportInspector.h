#pragma once

#ifdef CORE_EDITOR

#include <filesystem>

/// @brief 画像の取り込み設定（.meta の import）のインスペクタ
/// @details 色空間と圧縮の形式を選ぶと .meta へ書く。読み込み済みのテクスチャは作り直さない。
namespace CoreEngine::Editor::TextureImportInspector
{
    /// @brief Project で選んだ画像の取り込み設定を描く
    /// @return 画像として描いたら true（DDS に変換する画像でなければ何も描かずに false）
    bool DrawFile(const std::filesystem::path& file);
}

#endif // CORE_EDITOR
