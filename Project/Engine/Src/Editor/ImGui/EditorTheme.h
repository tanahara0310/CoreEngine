#pragma once

#include <imgui.h>
#include <cmath>

namespace CoreEngine::Editor::Theme
{
    /// @brief sRGB の 0-255 をリニアの色にする
    /// @details ImGui は sRGB の RTV へ描くため、リニアへ逆変換してから渡す。
    inline ImVec4 FromSrgb(int r, int g, int b, float a = 1.0f)
    {
        const auto toLinear = [](int v8) {
            const float c = static_cast<float>(v8) / 255.0f;
            return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
        return ImVec4(toLinear(r), toLinear(g), toLinear(b), a);
    }

    /// @brief 同じ色の不透明度だけを変える
    inline ImVec4 WithAlpha(const ImVec4& color, float alpha)
    {
        return ImVec4(color.x, color.y, color.z, alpha);
    }

    // ===== 面（奥 → 手前の 8 段） =====
    inline const ImVec4 kDeepest = FromSrgb(20, 20, 21);   ///< 最奥（スクロールバー溝など）
    inline const ImVec4 kField = FromSrgb(26, 26, 28);   ///< 入力欄
    inline const ImVec4 kChild = FromSrgb(30, 30, 33);   ///< 子パネル
    inline const ImVec4 kWindow = FromSrgb(36, 36, 40);   ///< ウィンドウ
    inline const ImVec4 kPanel = FromSrgb(44, 44, 48);   ///< メニューバー・タイトル・ポップアップ
    inline const ImVec4 kControl = FromSrgb(58, 58, 64);   ///< ボタン・選択タブ
    inline const ImVec4 kHover = FromSrgb(74, 74, 82);   ///< ホバー
    inline const ImVec4 kActive = FromSrgb(90, 90, 100);  ///< 押下

    // ===== アクセント（選択・操作中だけに使う） =====
    inline const ImVec4 kAccent = FromSrgb(61, 126, 200);
    inline const ImVec4 kAccentHover = FromSrgb(85, 150, 222);
    inline const ImVec4 kAccentMuted = FromSrgb(42, 63, 85);  ///< 選択行の下地

    // ===== 意味色 =====
    inline const ImVec4 kWarm = FromSrgb(242, 156, 49);  ///< 既定値からの変更
    inline const ImVec4 kOk = FromSrgb(78, 194, 122);
    inline const ImVec4 kWarn = FromSrgb(240, 180, 41);
    inline const ImVec4 kError = FromSrgb(224, 87, 76);
    inline const ImVec4 kScript = FromSrgb(169, 120, 224); ///< スクリプト由来であることの印
    inline const ImVec4 kOnWarm = FromSrgb(32, 21, 3);     ///< 橙の面に載せる文字

    // ===== 軸（X 赤・Y 緑・Z 青） =====
    inline const ImVec4 kAxisX = FromSrgb(224, 87, 76);
    inline const ImVec4 kAxisY = FromSrgb(78, 194, 122);
    inline const ImVec4 kAxisZ = FromSrgb(85, 150, 222);

    // ===== 文字 =====
    inline const ImVec4 kText = FromSrgb(238, 238, 242);
    inline const ImVec4 kTextDim = FromSrgb(168, 168, 180);
    inline const ImVec4 kTextMute = FromSrgb(128, 128, 138);

    // ===== 線 =====
    inline const ImVec4 kBorder = FromSrgb(15, 15, 16);   ///< ウィンドウの外周
    inline const ImVec4 kOutline = FromSrgb(58, 58, 66);   ///< チップ・小ボタンの輪郭
    inline const ImVec4 kTransparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
}
