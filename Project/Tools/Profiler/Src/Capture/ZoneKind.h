#pragma once

#include <imgui.h>

#include <cstdint>
#include <string_view>

/// @file ZoneKind.h
/// @brief 区間の種類（更新・スクリプト・描画など）を、区間の名前と場所から決める

namespace CoreEngine::Profiler
{
    /// @brief 区間の種類
    enum class ZoneKind : uint8_t
    {
        Update,  ///< シーンとオブジェクトの更新
        Script,  ///< スクリプトの関数
        Render,  ///< 描画の記録と GPU のパス
        Wait,    ///< GPU や垂直同期を待っている間
        Frame,   ///< フレームの大きな枠（BeginFrame・EndFrame）
        Load,    ///< 起動と読み込み（シェーダ・アセット）
        Other,   ///< どれにも当たらないもの
        Count,
    };

    /// @brief 区間の名前と場所（ファイルのパス）から種類を決める
    ZoneKind ClassifyZone(std::string_view name, std::string_view file);

    /// @brief 種類の日本語の名前
    const char* ZoneKindLabel(ZoneKind kind);

    /// @brief 種類の色（表の印と、後の段のタイムラインで使う）
    ImVec4 ZoneKindColor(ZoneKind kind);
}
