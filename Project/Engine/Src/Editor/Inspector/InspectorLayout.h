#pragma once

#ifdef USE_IMGUI

#include "Graphics/Asset/AssetType.h"

#include <imgui.h>

#include <cstddef>
#include <string>

/// @file
/// @brief インスペクタの見た目の部品（セクションの見出し・ラベルが左の行・参照の欄）

namespace CoreEngine::InspectorLayout
{
    /// @brief セクションの出自（見出しの丸の色）
    enum class Origin
    {
        Native, ///< C++ の型（青）
        Script, ///< スクリプトのクラス（紫）
    };

    /// @brief セクションの見出しに並べるもの
    struct SectionHeader
    {
        const char* name = "";          ///< 見出しの文字
        Origin origin = Origin::Native; ///< 丸の色
        bool* enabled = nullptr;        ///< 有効のチェック（nullptr なら出さない）
        const char* tag = nullptr;      ///< 右端の札（nullptr なら出さない）
        const char* menuId = nullptr;   ///< ⋮ と右クリックで開くポップアップの ID（nullptr なら出さない）
    };

    /// @brief セクションの見出しを描く（押すと開閉する）
    /// @param enabledChanged 有効のチェックを切り替えたら true にする
    /// @return 開いていれば true
    /// @note 開閉の状態は今の ID スタックで覚える。呼ぶ側でセクションごとに PushID する。
    ///       `menuId` のポップアップは、呼ぶ側が同じ ID スタックで BeginPopup する。
    bool DrawSectionHeader(const SectionHeader& header, bool& enabledChanged);

    /// @brief 左の列にラベルを描き、次の欄を右の列の幅いっぱいに置く
    /// @param color ラベルの色
    /// @param popupId ラベルを右クリックしたときに開くポップアップの ID（nullptr なら開かない）
    /// @return ラベルにカーソルが乗っていれば true
    bool BeginRow(const char* label, const ImVec4& color, const char* popupId = nullptr);

    /// @brief 参照の欄の中身（種類の記号・名前・ID の頭）を枠の中へ描く
    /// @param drawList 描く先（コンボを開くとポップアップが今の窓になるので、開く前に控えたものを渡す）
    /// @param min 枠の左上
    /// @param max 中身を描いてよい範囲の右下（コンボの矢印を除く）
    /// @param icon 先頭の記号（nullptr なら出さない）
    /// @param name 名前
    /// @param idText 右端に淡く出す ID（nullptr か空なら出さない）
    /// @param error 見つからないなどの異常を赤で出すか
    void DrawReferencePreview(ImDrawList* drawList, const ImVec2& min, const ImVec2& max,
        const char* icon, const char* name, const char* idText, bool error);

    /// @brief 編集できない参照の欄（枠と中身）を描く
    /// @return 欄にカーソルが乗っていれば true
    bool ReferenceField(const char* icon, const char* name, const char* idText);

    /// @brief ID を頭の数文字に縮める（`aa660ef2…`）
    std::string ShortId(const std::string& id, std::size_t length = 8);

    /// @brief アセットの種類を表す記号（アセットブラウザと同じもの）
    const char* AssetGlyph(AssetType type);

    /// @brief 幅 width の項目を、今の行の右端へ寄せる
    void AlignToRight(float width);
}

#endif // USE_IMGUI
