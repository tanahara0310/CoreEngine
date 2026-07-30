#pragma once

#include "Math/Vector/Vector3.h"

/// @brief ライン構造体（汎用的なライン表現）

namespace CoreEngine
{
struct Line {
    Vector3 start;   // 開始点
    Vector3 end;     // 終了点
    Vector3 color;   // 色 (RGB)
    float alpha;     // 透明度

    /// 深度テストを行うか。false にするとモデルに隠れず常に手前へ描かれる
    /// （骨のデバッグ表示のように、メッシュの内側にある線を見せたい場合に使う）。
    bool depthTest;

    /// @brief デフォルトコンストラクタ
    Line()
        : start(0.0f, 0.0f, 0.0f)
        , end(0.0f, 0.0f, 0.0f)
        , color(1.0f, 1.0f, 1.0f)
        , alpha(1.0f)
        , depthTest(true)
    {}

    /// @brief パラメータ付きコンストラクタ
    Line(const Vector3& s, const Vector3& e, const Vector3& c = {1.0f, 1.0f, 1.0f}, float a = 1.0f,
         bool depth = true)
        : start(s), end(e), color(c), alpha(a), depthTest(depth)
    {}
};
}
