#pragma once

#ifdef CORE_EDITOR

#include <string_view>

/// @file
/// @brief CVar の自動生成 ImGui パネル

namespace CoreEngine
{
    class ICVar;
    struct CVarRange;

    /// @brief CVarRegistry の内容から ImGui ウィジェットを自動生成する
    /// @note UI の入口は機能ごとのパネルへ一本化している。
    ///       全 CVar を一覧する横断パネルは、同じ値を触れる場所が 2 つできるので設けない。
    namespace CVarUI
    {
        /// @brief 数値ウィジェット（ドラッグ）の速度を求める
        /// @param range CVar の編集範囲
        /// @return 1px のドラッグあたりの変化量
        /// @note 範囲付き CVar を独自ラベルで描く UI から、速度を揃えるために使う
        float DragSpeed(const CVarRange& range);

        /// @brief CVar 1 つ分のウィジェットを描画する
        /// @param cvar 対象（nullptr 可）
        /// @return 値が変更された場合 true（NotifyChanged は内部で呼ばれる）
        bool DrawWidget(ICVar* cvar);

        /// @brief CVar 1 つを、インスペクタと同じ行（左にラベル・右に欄）で描く
        /// @param cvar 対象（nullptr 可）
        /// @param label 左に出す名前（nullptr なら CVar の名前の末尾）
        /// @param format 小数の表示の書式（nullptr なら "%.3f"）
        /// @param componentLabels ベクトルの成分の文字（nullptr なら X・Y・Z）
        /// @return 値が変更された場合 true
        /// @note 既定値から変えていればラベルが橙になり、ラベルか欄の右クリックで既定値へ戻せる。
        ///       範囲のある数は値の位置まで塗り、ドラッグを離したときに Undo を 1 件積む。色の不透明度は出さない
        bool DrawRow(ICVar* cvar, const char* label = nullptr, const char* format = nullptr,
            const char* const* componentLabels = nullptr);

        /// @brief 整数の CVar を、名前の一覧から選ぶ行で描く
        /// @param names 値 0 から順の名前
        /// @return 値が変更された場合 true
        bool DrawEnumRow(ICVar* cvar, const char* label, const char* const* names, int count);

        /// @brief XZ の向き（Vector2 の CVar）を、+X から +Z へ回る角度 [度] の行で描く
        /// @return 値が変更された場合 true
        /// @note 書き戻す値は長さ 1 の向き
        bool DrawDirectionRow(ICVar* cvar, const char* label);

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

#endif // CORE_EDITOR
