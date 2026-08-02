#pragma once

#ifdef USE_IMGUI

#include <string_view>

/// @brief CVar の自動生成 ImGui パネル

namespace CoreEngine
{
    class ICVar;

    /// @brief CVarRegistry の内容から ImGui ウィジェットを自動生成する
    /// @details 各機能クラスが個別に ImGui コードを書く代わりに、ここが代表して
    ///          レジストリを走査し、型に応じたウィジェットを描画する。
    ///
    ///          UI の入口は「機能ごとのパネル」に一本化している（Post Effects タブなど）。
    ///          全 CVar を一覧する横断パネルは、同じ値を触れる場所が 2 つできて
    ///          どちらを使うのか曖昧になるため設けない。
    namespace CVarUI
    {
        /// @brief CVar 1 つ分のウィジェットを描画する
        /// @param cvar 対象（nullptr 可）
        /// @return 値が変更された場合 true（NotifyChanged は内部で呼ばれる）
        bool DrawWidget(ICVar* cvar);

        /// @brief 接頭辞に一致する CVar をドット区切りのツリーとして描画する
        /// @param prefix 表示対象の接頭辞（例 "r.Vignette"。空文字なら全件）
        /// @return いずれかの値が変更された場合 true
        /// @note 機能ごとのパネルからは、この 1 行を呼ぶだけでパラメータ UI が完成する
        bool DrawTree(std::string_view prefix = "");

        /// @brief 接頭辞に一致する CVar をすべてコードデフォルトへ戻す
        /// @param prefix 対象の接頭辞（例 "r.Vignette"）
        /// @note 各機能パネルの「デフォルトに戻す」ボタンから呼ぶ
        void ResetTree(std::string_view prefix);
    }
}

#endif // USE_IMGUI
