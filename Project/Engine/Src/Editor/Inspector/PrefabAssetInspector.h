#pragma once

#ifdef CORE_EDITOR

#include <filesystem>

/// @brief Project で選んだプレハブの中身（コンポーネントと値）のインスペクタ
/// @details プレハブの各コンポーネントを、オブジェクトに付けずに作って値だけを読み込む（Awake / OnEnable は呼ばない）。
///          編集を終えたらプレハブのファイルへ書き戻し、シーンでそのプレハブから作ったオブジェクトへも反映する。
///          Undo はプロパティごとではなく、編集 1 回分のプレハブ全体の書き換えを 1 件として積む。
namespace CoreEngine::Editor::PrefabAssetInspector
{
    /// @brief プレハブの中身を描く
    /// @param file 選んでいるファイルの場所
    /// @return プレハブとして描いたら true（プレハブでなければ何も描かずに false）
    bool Draw(const std::filesystem::path& file);

    /// @brief 読み込んだコンポーネントを手放す（選んでいるものがプレハブでなくなったとき・終了時）
    void Release();
}

#endif // CORE_EDITOR
