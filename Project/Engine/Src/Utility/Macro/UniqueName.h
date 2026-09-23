#pragma once

/// 翻訳単位の中で一意な識別子を作るためのマクロ。
/// 自己登録のように「名前は使わないが変数宣言が要る」場所で使う。

/// @brief トークンを連結する（この段では引数を展開しない）
#define CORE_DETAIL_CAT2(a, b) a##b

/// @brief 引数を展開してから連結する
/// @note 1 段はさまないと `__COUNTER__` が展開されず、そのまま識別子に貼られる
#define CORE_DETAIL_CAT(a, b) CORE_DETAIL_CAT2(a, b)

/// @brief 翻訳単位の中で一意な識別子を作る
/// @param Prefix 識別子の先頭。末尾に通し番号が付く
/// @note `__COUNTER__` は展開のたびに増えるので、同じ行に 2 つ書いても衝突しない。
#define CORE_UNIQUE_NAME(Prefix) CORE_DETAIL_CAT(Prefix, __COUNTER__)
