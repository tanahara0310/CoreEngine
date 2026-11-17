#pragma once

#ifdef CORE_EDITOR

#include <string>

namespace CoreEngine::Editor::TagField
{
    /// @brief タグを一覧（プロジェクト設定の Tags）から選ぶ欄を、残りの幅いっぱいに描く
    /// @param tag 今のタグ。選び直したら選んだ名前を書く
    /// @return 別のタグを選んだら true
    /// @note 一覧に無いタグは、名前に「（一覧に無い）」を付けて注意の色で出す。
    bool Draw(std::string& tag);
}

#endif // CORE_EDITOR
